#pragma once

#include "game/objects/object_base.h"
#include "game/objects/object_manager_queries.h"
#include "game/objects/object_manager_state.h"

#include <cstdint>

// Creating and removing objects in the ObjectManager (docs/architecture/objects.md, "creating and removing").
// CreateObject(name, uchar type): allocates and constructs the object, appends it to the
//                                                   building / guest / "others" array with the next free id
// DeleteObject(id) -> bool: virtual destructor, then closes the gap
// InsertLastBuildingByY: insertion-sorts the newest building into the by-Y order
// What the manager does not own goes through an Env (the handler binds it to the game; the unit test to fakes):
//   Entity* Construct(uint8_t type, const char* name)   operator_new + the class constructor; null when the allocation failed
// void SetId(Entity*, uint32_t id) the object's SetId
//   object::DestroyFunc Destroyer()                           `delete` through the virtual destructor; the manager owns the
//                                                           object with it (object::Manager::Adopt)
//   void Fatal(const char* message)                         FatalError: it never returns in the game
namespace obl::object_manager {

constexpr const char* kUnrecognisedType = "Cannot create an object of an unrecognized type";
constexpr const char* kOutOfIds = "No free object id left";
constexpr const char* kOutOfBuildingSlots = "No free slot left for a new building";
constexpr const char* kOutOfGuestSlots = "No free slot left for a new guest";
constexpr const char* kOutOfOtherSlots = "No free slot left for a new object";

constexpr uint32_t kLastObjectType = static_cast<uint32_t>(ObjectType::Aerobic);
constexpr uint32_t kUnusableId = 0xfffffffeu;

// The object goes into the list of its class *after* its constructor ran (the constructor may create objects itself, so the count is read
// late), receives the next id, and the checks run after the bookkeeping: a full list (not only an overflowing one) is fatal. An unknown
// type is fatal before anything is allocated; if Fatal returns (fakes in tests), CreateObject returns null.
template<class Env> object::Base* CreateObject(object::Manager& m, const char* name, uint8_t type, Env& env)
{
    if (type < static_cast<uint8_t>(ObjectType::Scenery) || type > kLastObjectType) {
        env.Fatal(kUnrecognisedType);
        return nullptr;
    }
    object::Base* object = env.Construct(type, name);
    m.Adopt(object, env.Destroyer());
    const uint32_t id = m.m_nextFreeObjectId;
    if (type == static_cast<uint8_t>(ObjectType::Guest))
        m.AddGuest(object, id);
    else if (type == static_cast<uint8_t>(ObjectType::Building))
        m.AddBuilding(object, id);
    else
        m.AddOther(object, id);
    env.SetId(object, id);
    if (id == kUnusableId) env.Fatal(kOutOfIds);
    ++m.m_nextFreeObjectId;
    if (m.m_buildings.size() == object::Manager::kMaxBuildings) env.Fatal(kOutOfBuildingSlots);
    if (m.m_guests.size() == object::Manager::kMaxGuests) env.Fatal(kOutOfGuestSlots);
    if (m.m_others.size() == object::Manager::kMaxOthers) env.Fatal(kOutOfOtherSlots);
    return object;
}

// Buildings keep their order (the list is sorted by Y and drawn in that order): the gap is closed by shifting. Guests and others fill the
// gap with the last element. The destructor runs first, with the object still in its slot.
inline bool DeleteObject(object::Manager& m, uint32_t id)
{
    const uint32_t slot = FindSlotById(m, id);
    if (slot == kNoSlot) return false;
    object::Base* object = ObjectAtSlot(m, slot);
    if (object) m.Destroy(object);
    m.Unindex(id, object);
    if (slot < kGuestBase) {
        m.m_buildings.erase(m.m_buildings.begin() + slot);
        m.m_buildingIds.erase(m.m_buildingIds.begin() + slot);
    } else if (slot < kOtherBase) {
        const uint32_t index = slot - kGuestBase;
        m.m_guests[index] = m.m_guests.back();
        m.m_guestIds[index] = m.m_guestIds.back();
        m.m_guests.pop_back();
        m.m_guestIds.pop_back();
    } else {
        const uint32_t index = slot - kOtherBase;
        m.m_others[index] = m.m_others.back();
        m.m_otherIds[index] = m.m_otherIds.back();
        m.m_others.pop_back();
        m.m_otherIds.pop_back();
    }
    return true;
}

// One insertion-sort pass for the building just appended: it moves left while its Y is below its left neighbour's. The
// compare is "below or unordered": a NaN on either side keeps the walk going (`!(a >= b)`, not `a < b`).
inline void InsertLastBuildingByY(object::Manager& m)
{
    const uint32_t n = static_cast<uint32_t>(m.m_buildings.size());
    if (n <= 1) return;
    object::Base* const object = m.m_buildings[n - 1];
    const uint32_t id = m.m_buildingIds[n - 1];
    int32_t i = static_cast<int32_t>(n) - 2;
    for (; i >= 0; --i) {
        if (object->m_WorldXYZ[1] >= m.m_buildings[i]->m_WorldXYZ[1]) break;
        m.m_buildings[i + 1] = m.m_buildings[i];
        m.m_buildingIds[i + 1] = m.m_buildingIds[i];
    }
    m.m_buildings[i + 1] = object;
    m.m_buildingIds[i + 1] = id;
}

} // namespace obl::object_manager
