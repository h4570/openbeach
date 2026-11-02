#pragma once

#include "game/objects/object_manager_lifecycle.h"

#include <algorithm>
#include <vector>

namespace obl::object_manager {

// Destroys every object, front to back, each still in its list (a destructor may query the manager; the entries already destroyed read as
// null), then empties the lists.
inline void Destruct(object::Manager& m)
{
    auto destroy = [&](std::vector<object::Base*>& objects) {
        for (uint32_t i = 0; i < objects.size(); ++i) {
            if (objects[i]) m.Destroy(objects[i]);
            objects[i] = nullptr;
        }
    };
    destroy(m.m_buildings);
    destroy(m.m_guests);
    destroy(m.m_others);
    m.m_buildings.clear();
    m.m_guests.clear();
    m.m_others.clear();
    m.m_buildingIds.clear();
    m.m_guestIds.clear();
    m.m_otherIds.clear();
    m.m_byId.clear();
    m.m_owned.clear(); // nothing is left unless a destructor created an object
    // The next id and the keyline survive destruction.
}

// The block's own fields (the "ObjectManager" entry of an obl.save document).
struct SavedHeader {
    uint32_t active = 0;
    uint32_t buildings = 0;
    uint32_t guests = 0;
    uint32_t others = 0;
    std::vector<uint32_t> ids;
    uint32_t nextId = 0;
    uint8_t keyline = 0;
};

// The counts and ids of the block become the manager's lists (the objects are made afterwards, one per entry). A count past the capacity of
// its list (a corrupt save) is cut to the capacity. The saved active count is not stored: it is the sum.
inline void ApplyHeader(object::Manager& m, const SavedHeader& h)
{
    auto assign = [&](std::vector<object::Base*>& objects, std::vector<uint32_t>& ids, uint32_t count, int capacity, size_t firstSlot) {
        count = std::min(count, static_cast<uint32_t>(capacity));
        objects.assign(count, nullptr);
        ids.assign(h.ids.begin() + firstSlot, h.ids.begin() + firstSlot + count);
    };
    assign(m.m_buildings, m.m_buildingIds, h.buildings, object::Manager::kMaxBuildings, 0);
    assign(m.m_guests, m.m_guestIds, h.guests, object::Manager::kMaxGuests, kGuestBase);
    assign(m.m_others, m.m_otherIds, h.others, object::Manager::kMaxOthers, kOtherBase);
    m.m_byId.clear();
    m.m_nextFreeObjectId = h.nextId;
    m.m_globalKeyline = h.keyline;
}

// The objects of an applied header, each constructed from its header (`env.ObjectHeader`), adopted, indexed and loaded.
template<class Env> void LoadObjects(object::Manager& m, Env& env)
{
    auto load = [&](std::vector<object::Base*>& objects, const std::vector<uint32_t>& ids, uint8_t fixedType) {
        for (uint32_t i = 0; i < objects.size(); ++i) {
            uint8_t type = 0;
            char name[128]{};
            env.ObjectHeader(type, name);
            const uint8_t constructorType = fixedType ? fixedType : type;
            // The others switch has no cases for building/guest and no default:
            // an unsupported type leaves the existing slot.
            if (fixedType || (constructorType >= 1 && constructorType <= kLastObjectType && constructorType != 2 && constructorType != 3)) {
                objects[i] = env.Construct(constructorType, name);
                m.Adopt(objects[i], env.Destroyer());
            }
            // Indexed before its Load: a scenery object enters its grid cells by id while it loads.
            m.Index(ids[i], objects[i]);
            env.LoadObject(objects[i]);
        }
    };
    load(m.m_buildings, m.m_buildingIds, 2);
    load(m.m_guests, m.m_guestIds, 3);
    load(m.m_others, m.m_otherIds, 0);
}

} // namespace obl::object_manager
