#include "game/objects/lifeguard_fns.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "core/fatal.h"
#include "game/map/grid_math.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/staff_lifeguard_fns.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include "game/ui/map_view_math.h"

namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::Rand;

namespace {

using object::Lifeguard;
namespace fns = staff::lifeguard;

enum Which { FindBeach, SetDrawPosBuild, SetDrawPosServe, FindBeachPoint, MoveToTower, Move2SeaEntry, HeadForShore, Count };
const char* const kNames[Count] = {
        "objects.lifeguard-find-beach",
        "objects.lifeguard-set-draw-pos-build",
        "objects.lifeguard-set-draw-pos-serve",
        "objects.lifeguard-find-beach-point",
        "objects.lifeguard-move2-tower",
        "objects.lifeguard-move2-sea-entry",
        "objects.lifeguard-head-for-shore",
};

// Calls of the helpers (`call-report objects.lifeguard-actions`), and the calls refused because the input cannot be handled.
test::CallCounter g_counter("objects.lifeguard-actions"), g_declined("objects.lifeguard-actions-declined");
const auto kServePosition = &building_api::GetServePosition;

// The game side of the Env in src/common/lifeguard_fns.h.
struct GameEnv {
    Lifeguard& lifeguard;
    int Rand() { return game::Rand(); }
    void Post(const messages::Message& message)
    {
        messages::Message copy = message; // Post writes the delivery time into the record
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    object::Base* Object(int32_t id) { return object_manager::ObjectById(GetObjectManager(), id); }
    void ServePosition(const object::Base* building, float out[3]) { kServePosition(static_cast<const object::Building*>(building), out); }
    void WorldToTile(const float world[3], int32_t tile[2]) { ui::MapView::WorldToTile(tile, world); }
    void Teleport(float x, float y, float z) { lifeguard.Teleport(x, y, z); }
};

// false = the input cannot be handled and nothing was written or sent; Run then ends the game with fatal::UnhandledInput.
bool Perform(Which which, Lifeguard& lifeguard)
{
    GameEnv env{lifeguard};
    const map::Grid& grid = *GetGrid();
    switch (which) {
    case FindBeach: return fns::FindBeach(grid, lifeguard);
    case SetDrawPosBuild: return fns::SetDrawPos(grid, lifeguard, env, true);
    case SetDrawPosServe: return fns::SetDrawPos(grid, lifeguard, env, false);
    case FindBeachPoint: return fns::FindBeachPoint(grid, lifeguard, env);
    case MoveToTower: return fns::MoveToTower(lifeguard, env);
    case Move2SeaEntry: return fns::GoToShore(grid, lifeguard, env, true);
    case HeadForShore: return fns::GoToShore(grid, lifeguard, env, false);
    case Count: break;
    }
    return false;
}

void Run(Which which, Lifeguard& lifeguard)
{
    g_counter.Count();
    if (!Perform(which, lifeguard)) {
        g_declined.Count();
        fatal::UnhandledInput(kNames[which], "the input cannot be handled");
    }
}

} // namespace

void LifeguardFindBeach(object::Lifeguard& lifeguard)
{
    Run(FindBeach, lifeguard);
}
void LifeguardSetDrawPosBuild(object::Lifeguard& lifeguard)
{
    Run(SetDrawPosBuild, lifeguard);
}
void LifeguardSetDrawPosServe(object::Lifeguard& lifeguard)
{
    Run(SetDrawPosServe, lifeguard);
}
void LifeguardFindBeachPoint(object::Lifeguard& lifeguard)
{
    Run(FindBeachPoint, lifeguard);
}
void LifeguardMove2Tower(object::Lifeguard& lifeguard)
{
    Run(MoveToTower, lifeguard);
}
void LifeguardMove2SeaEntry(object::Lifeguard& lifeguard)
{
    Run(Move2SeaEntry, lifeguard);
}
void LifeguardHeadForShore(object::Lifeguard& lifeguard)
{
    Run(HeadForShore, lifeguard);
}

} // namespace obl::object
