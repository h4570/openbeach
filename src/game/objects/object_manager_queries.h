#pragma once

#include "core/ascii_case.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "game/objects/object_manager_state.h"

#include <cstdint>
#include <vector>

// Side-effect-free queries of the ObjectManager (docs/architecture/objects.md): look-ups by id, name and class, and the list builders.
// GetObject                 id -> object (FindSlotById)
// FindByName                FilterObjects with NameEquals
// CountByName               CountIf with the same predicate
// ListByClass               the objects of one class
// CollectNonServiceStaff    the staff without the service staff
// CountByClass              CountIf with IsClass
// ObjectsNearTile           guests and staff on the tiles around a tile
// FilterObjects, CountIf    generic walks over the three arrays
// ObjectsInRectangle        objects of a class whose world X/Y lie in a rectangle
// ForEachOfClass, ForEachObject, ForEachStaff   ClearSelection on a subset of objects
// SetKeyline                keyline byte of guest and staff sprites, plus the manager's own flag
// The list builders return a `std::vector` of their own (`ObjectList`), so a result stays valid across later queries.
namespace obl::object_manager {

using object::ObjectType;

// The result of a list query: the objects in list order (buildings, guests, others), a copy of the manager's pointers.
using ObjectList = std::vector<object::Base*>;

constexpr uint32_t kNoSlot = 0xffffffffu;

// Slot numbers address one virtual array: buildings from 0, guests from kMaxBuildings, everything else from kMaxBuildings + kMaxGuests
// (as if the three arrays and their id lists sat back to back).
constexpr uint32_t kGuestBase = object::Manager::kMaxBuildings;
constexpr uint32_t kOtherBase = object::Manager::kMaxBuildings + object::Manager::kMaxGuests;

// FindSlotById: linear search of the three id lists; 0xffffffff when the id is unknown.
inline uint32_t FindSlotById(const object::Manager& m, uint32_t id)
{
    for (uint32_t i = 0; i < m.m_buildingIds.size(); ++i)
        if (m.m_buildingIds[i] == id) return i;
    for (uint32_t i = 0; i < m.m_guestIds.size(); ++i)
        if (m.m_guestIds[i] == id) return kGuestBase + i;
    for (uint32_t i = 0; i < m.m_otherIds.size(); ++i)
        if (m.m_otherIds[i] == id) return kOtherBase + i;
    return kNoSlot;
}

inline object::Base* ObjectAtSlot(const object::Manager& m, uint32_t slot)
{
    if (slot < kGuestBase) return m.m_buildings[slot];
    if (slot < kOtherBase) return m.m_guests[slot - kGuestBase];
    return m.m_others[slot - kOtherBase];
}

inline object::Base* GetObject(const object::Manager& m, uint32_t id)
{
    const uint32_t slot = FindSlotById(m, id);
    return slot == kNoSlot ? nullptr : ObjectAtSlot(m, slot);
}

// NameEquals is `_stricmp(object->m_Name, name) == 0`: ASCII case folding (the game runs in the C locale).
inline bool NameEquals(const object::Base& object, const char* name)
{
    return ascii::EqualsNoCase(object.m_Name, name);
}

inline bool IsClass(const object::Base& object, char objectClass)
{
    return static_cast<char>(object.m_type) == objectClass;
}

// FilterObjects: the matching objects of buildings, guests and others (in that order).
template<class Pred> ObjectList FilterObjects(const object::Manager& m, Pred&& pred)
{
    ObjectList result;
    for (const auto* list : {&m.m_buildings, &m.m_guests, &m.m_others})
        for (object::Base* object : *list)
            if (pred(object)) result.push_back(object);
    return result;
}

// CountIf: the number of matching objects in the three arrays.
template<class Pred> uint32_t CountIf(const object::Manager& m, Pred&& pred)
{
    uint32_t count = 0;
    for (const auto* list : {&m.m_buildings, &m.m_guests, &m.m_others})
        for (object::Base* object : *list)
            if (pred(object)) ++count;
    return count;
}

inline ObjectList FindByName(const object::Manager& m, const char* name)
{
    return FilterObjects(m, [&](const object::Base* o) { return NameEquals(*o, name); });
}
inline uint32_t CountByName(const object::Manager& m, const char* name)
{
    return CountIf(m, [&](const object::Base* o) { return NameEquals(*o, name); });
}
inline uint32_t CountByClass(const object::Manager& m, char objectClass)
{
    return CountIf(m, [&](const object::Base* o) { return IsClass(*o, objectClass); });
}

// A building carries its catalog behavior in its definition (Building::m_definition, set at construction from the key). Only buildings
// have one: a non-building never matches. A building without a definition (a test double) matches nothing.
inline bool HasBehavior(const object::Base& object, BuildingBehavior behavior)
{
    if (!IsClass(object, static_cast<char>(ObjectType::Building))) return false;
    const auto* definition = static_cast<const object::Building&>(object).m_definition;
    return definition != nullptr && definition->m_Behavior == behavior;
}

// The behavior query of the buildings, in list order: the buildings list is walked front to back, so the result keeps the order of the list
// (the name lookups it replaces return the same buildings in the same order, buildings first).
inline ObjectList FindBuildings(const object::Manager& m, BuildingBehavior behavior)
{
    ObjectList result;
    for (object::Base* building : m.m_buildings)
        if (HasBehavior(*building, behavior)) result.push_back(building);
    return result;
}
inline uint32_t CountBuildingsByBehavior(const object::Manager& m, BuildingBehavior behavior)
{
    uint32_t count = 0;
    for (const object::Base* building : m.m_buildings)
        if (HasBehavior(*building, behavior)) ++count;
    return count;
}

// ListByClass: the objects of one class in list order. A copy is returned for every class, so a caller that deletes objects while it walks the list does
// not see the list shift under it.
inline ObjectList ListByClass(const object::Manager& m, char objectClass)
{
    if (objectClass == static_cast<char>(ObjectType::Building)) return m.m_buildings;
    if (objectClass == static_cast<char>(ObjectType::Guest)) return m.m_guests;
    return FilterObjects(m, [&](const object::Base* o) { return IsClass(*o, objectClass); });
}

// CollectNonServiceStaff: the lifeguards, mechanics, reps, builders, cleaners and security
// guards among the "others"; the service staff (type 6) is left out.
inline ObjectList CollectNonServiceStaff(const object::Manager& m)
{
    ObjectList result;
    for (object::Base* object : m.m_others) {
        const auto type = static_cast<uint8_t>(object->m_type);
        if (type >= 7 && type <= 12) result.push_back(object);
    }
    return result;
}

// ObjectsNearTile: the guests (3) and staff (6..12) standing on the (2 * radius + 1) square around the tile, clipped at 0.
// `forEachOnTile(x, y, visit)` calls `visit(Entity*)` for every object in the tile object list.
// Kept quirks (tests pin them): the x test is against the current column, but the "y" test compares x0 - y0 + y (the starting column plus
// the row offset) with 128, so it only ever cuts rows when the window starts far to the right; the grid wraps the y it is given.
template<class ForEachOnTile> ObjectList ObjectsNearTile(int32_t tileX, int32_t tileY, int32_t radius, ForEachOnTile&& forEachOnTile)
{
    auto add = [](int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); };
    auto sub = [](int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); };
    int32_t x0 = sub(tileX, radius);
    int32_t y0 = sub(tileY, radius);
    int32_t width = add(add(radius, radius), 1);
    int32_t height = width;
    if (x0 < 0) {
        width = add(width, x0);
        x0 = 0;
    }
    if (y0 < 0) {
        height = add(height, y0);
        y0 = 0;
    }
    ObjectList result;
    if (height <= 0) return result;
    const int32_t columnMinusRow = sub(x0, y0);
    int32_t y = y0;
    for (int32_t row = height; row != 0; row = sub(row, 1), y = add(y, 1)) {
        if (width <= 0) continue;
        int32_t x = x0;
        for (int32_t column = width; column != 0; column = sub(column, 1), x = add(x, 1)) {
            if (x >= 0x80 || add(columnMinusRow, y) >= 0x80) continue;
            forEachOnTile(static_cast<uint32_t>(x), static_cast<uint32_t>(y), [&](object::Base* object) {
                const auto type = static_cast<uint8_t>(object->m_type);
                const bool wanted = type == 3 || (type > 5 && type <= 12);
                if (wanted) result.push_back(object);
            });
        }
    }
    return result;
}

// ---- Cursor, rectangle and selection (docs/architecture/objects.md) ---------------------------------------------------------------------------
// GetObjectsUnderCursor / GetObjectsUnderCursorUnselectable are FilterObjects with the selectable cursor hit test /
// The cursor hit test without selectability filtering; the predicates never read the context they pass.

// ObjectsInRectangle: the rectangle is [min, min + (max - min)] on world X and Y; the corner is computed in
// extended precision and rounded to float once (subtract, add, float store). The comparisons treat NaN as below: the minimum test is
// "x >= min" (a NaN position or minimum fails it), the maximum test is "not x > max" (a NaN maximum passes). Buildings (2) and guests (3) walk
// their own lists, any other class walks the "others" and also tests the class.
inline ObjectList ObjectsInRectangle(const object::Manager& m, uint8_t objectClass, const float* minPoint, const float* maxPoint)
{
    const float minX = minPoint[0];
    const float minY = minPoint[1];
    const float maxX = static_cast<float>((static_cast<double>(maxPoint[0]) - minX) + minX);
    const float maxY = static_cast<float>((static_cast<double>(maxPoint[1]) - minY) + minY);
    ObjectList result;
    auto scan = [&](const std::vector<object::Base*>& objects, bool matchClass) {
        for (object::Base* o : objects) {
            if (matchClass && static_cast<uint8_t>(o->m_type) != objectClass) continue;
            const float x = o->m_WorldXYZ[0];
            const float y = o->m_WorldXYZ[1];
            if (!(x >= minX) || x > maxX || !(y >= minY) || y > maxY) continue;
            result.push_back(o);
        }
    };
    if (objectClass == static_cast<uint8_t>(ObjectType::Building))
        scan(m.m_buildings, false);
    else if (objectClass == static_cast<uint8_t>(ObjectType::Guest))
        scan(m.m_guests, false);
    else
        scan(m.m_others, true);
    return result;
}

// ClearSelectionByClass: `notify(object)` calls ClearSelection for every building (2) / guest (3), or for the "others" of exactly that type.
// The walks are by index over the live list, so a notify that changes the list is seen.
template<class Notify> void ForEachOfClass(object::Manager& m, uint8_t objectClass, Notify&& notify)
{
    if (objectClass == static_cast<uint8_t>(ObjectType::Building)) {
        for (uint32_t i = 0; i < m.m_buildings.size(); ++i) notify(m.m_buildings[i]);
    } else if (objectClass == static_cast<uint8_t>(ObjectType::Guest)) {
        for (uint32_t i = 0; i < m.m_guests.size(); ++i) notify(m.m_guests[i]);
    } else {
        for (uint32_t i = 0; i < m.m_others.size(); ++i)
            if (static_cast<uint8_t>(m.m_others[i]->m_type) == objectClass) notify(m.m_others[i]);
    }
}

// NotifyAllObjects: every building, then every guest, then everything else.
template<class Notify> void ForEachObject(object::Manager& m, Notify&& notify)
{
    for (uint32_t i = 0; i < m.m_buildings.size(); ++i) notify(m.m_buildings[i]);
    for (uint32_t i = 0; i < m.m_guests.size(); ++i) notify(m.m_guests[i]);
    for (uint32_t i = 0; i < m.m_others.size(); ++i) notify(m.m_others[i]);
}

inline bool IsStaffType7To12(const object::Base& o)
{
    const auto type = static_cast<uint8_t>(o.m_type);
    return type >= 7 && type <= 12;
}

// ClearStaffSelection: the "others" of type 7..12 only (no service staff, guests or buildings).
template<class Notify> void ForEachStaff(object::Manager& m, Notify&& notify)
{
    for (uint32_t i = 0; i < m.m_others.size(); ++i)
        if (IsStaffType7To12(*m.m_others[i])) notify(m.m_others[i]);
}

// SetKeyline: guests, then staff 7..12, get the sprite's keyline byte; the manager keeps the value for later sprites.
inline void SetKeyline(object::Manager& m, uint8_t value)
{
    auto set = [&](object::Base* o) { static_cast<ui::SpriteInstance*>(o->m_sprite)->m_keyline = value; };
    for (uint32_t i = 0; i < m.m_guests.size(); ++i) set(m.m_guests[i]);
    ForEachStaff(m, set);
    m.m_globalKeyline = value;
}

} // namespace obl::object_manager
