#include "game/map/grid_state.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/map_view_math.h"
#include "core/fatal.h"
#include "game/objects/guest_beach_points.h"
#include "game/objects/guest_walk_targets.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_vehicle.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <cstdint>

// The four guest functions that pick a walking goal and post it to the guest itself (src/game/objects/guest_walk_targets.h, guest walking goals, docs/architecture/objects.md):
// HeadForEntryPoint (swim to walkpoint 0), GetSeaEntry (fills walkpoints 0 and 1 from the nearest sea point, then walks
// to walkpoint 0), GotoWorldUnloadPoint (the unload point of the vehicle in m_ActivityPerson) and WalkToWindsurf (the
// footprint spot of the rental building in m_ActivityPerson). The script reaches them through the guest's CallFunction. Every one ends in a Post that
// the message handler delivers at once (the guest's own script).
namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::Rand;

namespace {

using object::Building;
using object::Guest;
using object::Vehicle;
using ui::SpriteInstance;

test::CallCounter g_headForEntry("objects.guest-head-for-entry-point");
test::CallCounter g_seaEntry("objects.guest-get-sea-entry");
test::CallCounter g_unload("objects.guest-goto-world-unload-point");
test::CallCounter g_windSurf("objects.guest-walk-to-wind-surf");
test::CallCounter g_headForBin("objects.guest-head-for-bin");
test::CallCounter g_wander("objects.guest-wander-beach");

// A message from the guest to itself. Fields past data that the caller does not set are zero.
void PostToSelf(const Guest& guest, int32_t type, const guest_walk::Goal& goal, int32_t extra = 0)
{
    messages::Message message{};
    message.sender = static_cast<int>(guest.m_id);
    message.receiver = static_cast<int>(guest.m_id);
    message.type = type;
    message.flags = goal.x;
    message.data = goal.y;
    message.extra = extra;
    messages::message_facade::Post(messages::message_facade::Current(), &message);
}

SpriteInstance* SpriteOf(object::Base& object)
{
    return static_cast<SpriteInstance*>(object.m_sprite);
}

// ---- HeadForEntryPoint -----------------------------------------------------------------------------------------------------

void HeadForEntry(Guest& guest)
{
    PostToSelf(guest, messages::MT_SWIM2SEAPOINT, guest_walk::TruncatedGoal(guest.m_walkpoints[0][0], guest.m_walkpoints[0][1]));
}

// ---- GetSeaEntry -----------------------------------------------------------------------------------------------------------

// Walkpoint 0 is the nearest sea point's first pair and walkpoint 1 its second (x, y of each; the heights stay), walkpoint 2's x and y and the single
// walkpoint index are cleared, and the guest walks to walkpoint 0 on the pavement.
void SeaEntry(Guest& guest)
{
    const float* point = GuestFindNearestSeaPoint(guest.m_WorldXYZ[0], guest.m_WorldXYZ[1]);
    guest.m_walkpoints[0][0] = point[0];
    guest.m_walkpoints[0][1] = point[1];
    guest.m_walkpoints[1][0] = point[3];
    guest.m_walkpoints[1][1] = point[4];
    guest.m_walkpoints[2][0] = 0.0f;
    guest.m_walkpoints[2][1] = 0.0f;
    guest.m_walkpoint = 0;
    PostToSelf(guest, messages::MT_PAVEMENT2POINT, guest_walk::TruncatedGoal(guest.m_walkpoints[0][0], guest.m_walkpoints[0][1]));
}

// ---- GotoWorldUnloadPoint --------------------------------------------------------------------------------------------------

// A vehicle of the unload kind sends the guest walking to its (moved) unload point; any other object, whatever it is, gets an MT_MOVEIMHERE with state
// 0x29 in extra. A missing vehicle sprite or object ends in UnhandledInput.
bool UnloadDeclined(const Guest& guest)
{
    object::Base* target = object_manager::ObjectById(GetObjectManager(), guest.m_ActivityPerson);
    if (!target) return true;
    return static_cast<Vehicle*>(target)->m_vehicleType == guest_walk::kVehicleWithUnloadPoint && !target->m_sprite;
}

void Unload(Guest& guest)
{
    object::Base* target = object_manager::ObjectById(GetObjectManager(), guest.m_ActivityPerson);
    if (static_cast<Vehicle*>(target)->m_vehicleType == guest_walk::kVehicleWithUnloadPoint) {
        const guest_walk::Goal goal = guest_walk::UnloadGoal(target->m_WorldXYZ[0], target->m_WorldXYZ[1], SpriteOf(*target)->m_direction);
        PostToSelf(guest, messages::MT_MOVE2POINT, goal);
        return;
    }
    PostToSelf(guest, messages::MT_MOVEIMHERE, {0, 0}, guest_walk::kStateAfterMoveHere);
}

// ---- WalkToWindsurf --------------------------------------------------------------------------------------------------------

// The building in m_ActivityPerson is read without a check (object, sprite, definition), and the rotated footprint offset point handles only the rotations 0..3: a
// direction past 3 returned whatever its stack held. Such a call ends in UnhandledInput.
constexpr uint32_t kFootprintRotations = 4;

bool WindSurfDeclined(const Guest& guest)
{
    auto* building = static_cast<Building*>(object_manager::ObjectById(GetObjectManager(), guest.m_ActivityPerson));
    return !building || !building->m_sprite || !building->m_definition ||
            static_cast<uint32_t>(SpriteOf(*building)->m_direction) >= kFootprintRotations;
}

void WindSurf(Guest& guest)
{
    auto* building = static_cast<Building*>(object_manager::ObjectById(GetObjectManager(), guest.m_ActivityPerson));
    const uint32_t rotation = static_cast<uint32_t>(SpriteOf(*building)->m_direction);
    int tile[2];
    ui::MapView::WorldToTile(tile, building->m_WorldXYZ);
    const grid_math::PathPoint spot = GetGrid()->GetFootprintOffsetPoint(static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]),
            building->m_definition->m_GridWidth, building->m_definition->m_GridHeight, guest_walk::kWindSurfOffsetX,
            guest_walk::kWindSurfOffsetY, rotation);
    PostToSelf(guest, messages::MT_PAVEMENT2POINT, guest_walk::TruncatedGoal(spot.x, spot.y));
}

// ---- HeadForBin ------------------------------------------------------------------------------------------------------------

constexpr const char* kBinName = "Bin";

// The nearest bin within 160 units that is neither claimed by a vandal nor full; with one, a draw against GuestDef::m_Litter decides whether the guest
// walks to it (the bin becomes m_ActivityPerson before the message, the action state is set after it, so a delivered message cannot undo it).
void RunHeadForBin(Guest& guest)
{
    const auto list = object_manager::FindObjectsByName(GetObjectManager(), kBinName);
    const auto count = static_cast<uint32_t>(list.size());
    guest_walk::NearestBin nearest(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* bin = static_cast<object::Scenery*>(list[i]);
        if (bin->m_vandalised || !guest_walk::BinHasRoom(bin->m_NumItems, bin->m_definition->m_Capacity)) continue;
        nearest.Offer(i, guest_walk::BinDistance(guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], bin->m_WorldXYZ[0], bin->m_WorldXYZ[1]));
    }
    if (nearest.index < count) {
        auto* bin = static_cast<object::Scenery*>(list[nearest.index]);
        // The room is asked once more before the draw (the same answer); the draw is made only for a bin that is there.
        if (guest_walk::BinHasRoom(bin->m_NumItems, bin->m_definition->m_Capacity) &&
                guest_walk::UsesBin(Rand(), guest.m_definition->m_Litter)) {
            bin = static_cast<object::Scenery*>(list[nearest.index]); // read again after the draw
            guest.m_ActivityPerson = static_cast<int32_t>(bin->m_id);
            PostToSelf(guest, messages::MT_MOVE2POINT, guest_walk::TruncatedGoal(bin->m_WorldXYZ[0], bin->m_WorldXYZ[1]));
            guest.m_ActionState = guest_walk::kActionStateToBin;
            return;
        }
    }
    guest.m_ActionState = guest_walk::kActionStateNoBin;
}

// ---- WanderBeach -----------------------------------------------------------------------------------------------------------

// The guest walks to a random free tile of the beach node nearest to it (guest_beach::WanderTarget): the tile's middle becomes walkpoint 0, the walkpoint
// index is cleared and the goal is posted to the guest itself. No beach or an empty first node ends in UnhandledInput.
bool WanderDeclined()
{
    return guest_beach::WanderDeclined(*GetGrid());
}

void Wander(Guest& guest)
{
    const float position[3] = {guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]};
    const grid_math::WorldPoint target = guest_beach::WanderTarget(*GetGrid(), position, [] { return Rand(); });
    guest.m_walkpoints[0][0] = target.x;
    guest.m_walkpoints[0][1] = target.y;
    guest.m_walkpoints[0][2] = target.z;
    guest.m_walkpoint = 0;
    PostToSelf(guest, messages::MT_PAVEMENT2POINT, guest_walk::TruncatedGoal(guest.m_walkpoints[0][0], guest.m_walkpoints[0][1]));
}

// ---- handlers -------------------------------------------------------------------------------------------------------------------------

} // namespace

void Guest::HeadForEntryPoint()
{
    g_headForEntry.Count();
    HeadForEntry(*this);
}

void Guest::GetSeaEntry()
{
    g_seaEntry.Count();
    SeaEntry(*this);
}

void Guest::GotoWorldUnloadPoint()
{
    if (UnloadDeclined(*this)) fatal::UnhandledInput("objects.guest-goto-world-unload-point", "no vehicle object or sprite");
    g_unload.Count();
    Unload(*this);
}

void Guest::WalkToWindsurf()
{
    if (WindSurfDeclined(*this)) fatal::UnhandledInput("objects.guest-walk-to-wind-surf", "no building, sprite or a rotation past 3");
    g_windSurf.Count();
    WindSurf(*this);
}

void Guest::HeadForBin()
{
    g_headForBin.Count();
    RunHeadForBin(*this);
}

void Guest::WanderBeach()
{
    if (WanderDeclined()) fatal::UnhandledInput("objects.guest-wander-beach", "no beach chain to walk");
    g_wander.Count();
    Wander(*this);
}

} // namespace obl::object
