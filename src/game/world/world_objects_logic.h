#pragma once

#include "game/objects/scenery_names.h"
#include "game/objects/object_guest.h"
#include "core/ascii_case.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_scenery.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <cstring>

// The loops over the world's objects and the bookkeeping of a removed object:
// world::UpdateObjects() vtable `Update` of every object that is not waiting for death, buildings, others, guests
// world::ProcessDeletedObjects() the objects marked by MT_KILLOBJECT: bookkeeping, `OnExitState`, the object manager's delete
// world::OnObjectDeleting(object) dialogs, the counters of the World, the feedback of the object
// The calls into the objects and the managers stay behind an `Env` (the game side calls the game's own functions), so the order of events and the
// reading of the live object arrays are unit-tested without the DLL. Each function lists the members of its Env.
namespace obl::world_objects {

constexpr uint8_t kStateLeaving = 0x29; // a guest's script state on the way out
constexpr const char* kLitterName = "Litter"; // any scenery whose name contains it is rubbish

// Every pass reads the count once and then the list element by element from the end, the pointer of each element just before it is used. The lists are
// live: the callee may append objects (they are not visited: the count is old; the capacity is reserved, so the storage does not move) and
// object_manager::DeleteObject shifts the elements behind the one it removed, which the walk from the end does not step on.
template<class Visit> void WalkFromEnd(const std::vector<object::Base*>& list, size_t count, Visit&& visit)
{
    for (; count != 0; --count) visit(*list[count - 1]);
}

// ---- world::UpdateObjects -----------------------------------------------------------------------------------------------------
//   void Update(Entity&)   the object's virtual Update (no arguments; its result is not looked at)
// The order is buildings, "others" (the whole of the staff, vehicles, scenery, ...), guests; the object manager is read again before every pass.
template<class Env> void UpdateObjects(world::World& world, Env& env)
{
    const auto update = [&](object::Base& object) {
        if (object.m_waitingForDeath == 0) env.Update(object);
    };
    WalkFromEnd(world.m_objectManager->m_buildings, world.m_objectManager->m_buildings.size(), update);
    WalkFromEnd(world.m_objectManager->m_others, world.m_objectManager->m_others.size(), update);
    WalkFromEnd(world.m_objectManager->m_guests, world.m_objectManager->m_guests.size(), update);
}

// ---- world::ProcessDeletedObjects ---------------------------------------------------------------------------------------------
//   void OnObjectDeleting(Entity&)   world::OnObjectDeleting of this world (below; the game's dispatch picks the implementation)
//   void OnExitState(Entity&)        the object's virtual OnExitState (no arguments; the building's override for a building)
//   void DeleteObject(uint32_t id)       the object manager's delete; `id` is read from the object only after OnExitState ran
//   void RefreshDialogs()                refresh of all GUI dialogs, once at the end and only when something was removed
// The objects marked `m_waitingForDeath` (by world::ReceiveMessage for MT_KILLOBJECT) go in the same order as the update, one object at a time.
template<class Env> void ProcessDeletedObjects(world::World& world, Env& env)
{
    bool deleted = false;
    const auto remove = [&](object::Base& object) {
        if (object.m_waitingForDeath == 0) return;
        deleted = true;
        env.OnObjectDeleting(object);
        env.OnExitState(object);
        env.DeleteObject(object.m_id);
    };
    WalkFromEnd(world.m_objectManager->m_buildings, world.m_objectManager->m_buildings.size(), remove);
    WalkFromEnd(world.m_objectManager->m_others, world.m_objectManager->m_others.size(), remove);
    WalkFromEnd(world.m_objectManager->m_guests, world.m_objectManager->m_guests.size(), remove);
    if (deleted) env.RefreshDialogs();
}

// ---- world::OnObjectDeleting --------------------------------------------------------------------------------------------------
//   void CloseDialog(Entity&)                    close the GUI dialogs of the object's provider
//   void RecordGuestLeaving(Entity& guest)       the ferry manager's RecordGuestLeaving (the guest's happiness into the ferry's sums)
// void RemoveThoughts(Entity& building, uint32_t objectId) = the building's thought removal: every thought of that object
//                                                                  out of the building's m_ThoughtList
//   void InvalidateFeedback(uint32_t objectId)       feedback::Feedback::InvalidateObject of the feedback manager
// By the object's type: a guest leaves the guest count and, in the leaving state, is added to the day's and the total's departures (the unhappy
// ones too, when it left early); each building is asked to forget the guest's thoughts (the building count is read once; the array is live). A
// building leaves the building count. Scenery: rubbish leaves the rubbish count, anything else takes its rating off the scenic value (the float
// subtraction runs in 53-bit precision, one store of a 53-bit result). The feedback of the object is dropped for every type.
template<class Env> void OnObjectDeleting(world::World& world, object::Base& object, Env& env)
{
    env.CloseDialog(object);
    const object::ObjectType type = object.m_type;
    if (type == object::ObjectType::Guest) {
        --world.m_numGuests;
        if (object.m_state == kStateLeaving) {
            env.RecordGuestLeaving(object);
            const auto& guest = static_cast<const object::Guest&>(object);
            const int gender = guest.m_definition->m_Gender; // not range-checked: the definition's 0 or 1
            if (guest.m_leavingEarly != 0) {
                ++world.m_totalGuestUnhappyDepart[gender];
                ++world.m_todayGuestUnhappyDepart[gender];
            }
            ++world.m_totalGuestDepart[gender];
            ++world.m_todayGuestDepart[gender];
        }
        object::Manager& manager = *world.m_objectManager;
        for (uint32_t count = static_cast<uint32_t>(manager.m_buildings.size()), index = 0; count != 0; --count, ++index)
            env.RemoveThoughts(*manager.m_buildings[index], object.m_id);
    } else if (type == object::ObjectType::Building) {
        --world.m_numBuildings;
    }
    if (type == object::ObjectType::Scenery) {
        if (scenery_names::HasLitterInName(object.m_Name)) {
            --world.m_numLitter;
            env.InvalidateFeedback(object.m_id);
            return;
        }
        const auto& scenery = static_cast<const object::Scenery&>(object);
        world.m_scenicVal =
                static_cast<float>(static_cast<double>(world.m_scenicVal) - static_cast<double>(scenery.m_definition->m_Rating));
    }
    env.InvalidateFeedback(object.m_id);
}

} // namespace obl::world_objects
