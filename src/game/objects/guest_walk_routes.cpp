#include "game/map/grid_state.h"
#include "game/ui/sprite.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/map_view_math.h"
#include "core/fatal.h"
#include "game/objects/guest_shadow.h"
#include "game/map/grid_math.h"
#include "game/objects/guest_update_opinions.h"
#include "game/objects/guest_walk_path.h"
#include "game/objects/guest_walk_routes.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/world/world_params.h"

#include <cstdint>
#include <string>

// The five guest functions that look along the tile grid for a walking goal (src/game/objects/guest_walk_routes.h, guest walking, docs/architecture/objects.md):
// StartPathWalk (from the beach to the nearest walkable tile), SurfToShore (from the sea to the beach edge),
// WindSurfStraight (a few tiles ahead on the water, or turn round) and GotoDancePoint (a random tile of the dance floor)
// and WalkAlongPath (a random walkable neighbour and the row of such tiles behind it, src/game/objects/guest_walk_path.h, guest walk along the path).
// The script reaches them through the guest's CallFunction. Every one ends in a Post that is delivered at once (the guest's own script).
namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::GetWorldParams;
using game::Rand;

namespace {

using guest_shadow::Post;
using guest_walk::TileXY;
using object::Building;
using object::Guest;
using ui::SpriteInstance;

using PlayAnimationFunc = float (*)(object::Base*, const char*, int32_t, int32_t, int32_t, const char*);

test::CallCounter g_geton("objects.guest-geton-path");
test::CallCounter g_surf("objects.guest-surf-to-beach");
test::CallCounter g_windSurf("objects.guest-wind-surf-straight");
test::CallCounter g_dance("objects.guest-goto-dance-point");
test::CallCounter g_path("objects.guest-walk-along-path");

TileXY TileOf(object::Base& object)
{
    int tile[2] = {};
    ui::MapView::WorldToTile(tile, object.m_WorldXYZ);
    return {static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1])};
}

// The centre of a tile as the game computes it (the output buffer is the caller's).
const float* CentreOf(TileXY tile, float (&buffer)[3])
{
    const grid_math::WorldPoint centre = GetGrid()->TileToWorldCenter(tile.x, tile.y);
    buffer[0] = centre.x;
    buffer[1] = centre.y;
    buffer[2] = centre.z;
    return buffer;
}

SpriteInstance* SpriteOf(object::Base& object)
{
    return static_cast<SpriteInstance*>(object.m_sprite);
}

void PostToSelf(Guest& guest, int32_t type, int32_t flags, int32_t data)
{
    Post(guest.m_id, static_cast<int32_t>(guest.m_id), type, flags, data);
}

// ---- StartPathWalk ---------------------------------------------------------------------------------------------------------------------------

// The guest's walkpoint 0 becomes the centre of the first tile of the four rays that can be walked on (all three coordinates), and the guest walks there.
// False (nothing was changed): no ray ever meets such a tile.
bool RunGetonPath(Guest& guest)
{
    const auto found = guest_walk::FindGetonTile(*GetGrid(), TileOf(guest));
    if (!found) return false;
    float buffer[3];
    const float* centre = CentreOf(*found, buffer);
    for (int axis = 0; axis < 3; ++axis) guest.m_walkpoints[0][axis] = centre[axis];
    PostToSelf(guest, messages::MT_MOVE2POINT, FloatToIntWrap(guest.m_walkpoints[0][0]), FloatToIntWrap(guest.m_walkpoints[0][1]));
    return true;
}

// ---- SurfToShore -------------------------------------------------------------------------------------------------------------------------

void RunSurfToBeach(Guest& guest)
{
    const bool sent = guest_walk::SurfToBeachRays(*GetGrid(), TileOf(guest), [&](TileXY at) {
        float buffer[3];
        const float* centre = CentreOf(at, buffer);
        PostToSelf(guest, messages::MT_SWIM2SEAPOINT, FloatToIntWrap(centre[0]), FloatToIntWrap(centre[1]));
    });
    if (sent) return;
    const float* point = GuestFindNearestSeaPoint(guest.m_WorldXYZ[0], guest.m_WorldXYZ[1]);
    PostToSelf(guest, messages::MT_SWIM2SEAPOINT, FloatToIntWrap(point[0]), FloatToIntWrap(point[1]));
}

// ---- WindSurfStraight --------------------------------------------------------------------------------------------------------------------

// Water ahead: one of the free tiles (a draw) is the goal. None: a "turn" message, the action state 5, the guest's own turn animation whose length delays
// the next action, and the board sprite (the attached sprite, read again after the messages) turns too. A guest without a sprite, or with no
// water without an attached sprite, ends the game with a fatal error (nothing was changed or drawn before the decision).
bool RunWindSurfStraight(Guest& guest)
{
    SpriteInstance* sprite = SpriteOf(guest);
    if (!sprite) return false;
    map::Grid* grid = GetGrid();
    const TileXY start = TileOf(guest);
    const int direction = sprite->m_direction;
    const int free = guest_walk::WaterAhead(*grid, start, direction);
    if (free != 0) {
        const int steps = guest_walk::WindSurfSteps(Rand(), free);
        float buffer[3];
        const float* centre = CentreOf(guest_walk::TileAhead(*grid, TileOf(guest), direction, steps), buffer);
        PostToSelf(guest, messages::MT_SWIM2SEAPOINT, FloatToIntWrap(centre[0]), FloatToIntWrap(centre[1]));
        return true;
    }
    if (!guest.m_attachedSprite) return false;
    PostToSelf(guest, guest_walk::kWindSurfTurnMessage, 1, guest.m_ActionState);
    guest.m_ActionState = guest_walk::kWindSurfBlockedState;
    const float length = guest.PlayAnimation(guest_walk::kWindSurfTurnGuestAnim, 0, -1, 0, nullptr);
    messages::Message next{};
    next.sender = next.receiver = static_cast<int>(guest.m_id);
    next.type = guest_walk::kWindSurfNextActionMessage;
    next.extra = guest_walk::kWindSurfNextActionState;
    messages::message_facade::PostDelayed(messages::message_facade::Current(), &next, length);
    auto* board = static_cast<SpriteInstance*>(guest.m_attachedSprite);
    board->m_animType = guest_walk::kWindSurfAnimPlayOnce;
    (board)->SetAnimByName(guest_walk::kWindSurfTurnBoardAnim, GetWorld()->m_gameTime, guest.m_WorldXYZ);
    return true;
}

// ---- GotoDancePoint ----------------------------------------------------------------------------------------------------------------------

// The dance floor is the building the guest is heading for (m_DestObjectID), and a missing object, sprite or definition ends the game
// with a fatal error, as does a floor no candidate can ever reach (instead of drawing forever).
bool RunGotoDancePoint(Guest& guest)
{
    auto* floorBuilding = static_cast<Building*>(object_manager::ObjectById(GetObjectManager(), guest.m_DestObjectID));
    if (!floorBuilding || !floorBuilding->m_sprite || !floorBuilding->m_definition) return false;
    map::Grid* grid = GetGrid();
    const guest_walk::DanceFloor floor = guest_walk::DanceFloorOf(TileOf(*floorBuilding), SpriteOf(*floorBuilding)->m_direction,
            floorBuilding->m_definition->m_GridWidth, floorBuilding->m_definition->m_GridHeight);
    const auto tile = guest_walk::FindDanceTile(*grid, TileOf(guest), floor, [] { return Rand(); });
    if (!tile) return false;
    const grid_math::WorldPoint point = grid_math::TileToWorldPoint(
            grid->m_tileSize, static_cast<uint32_t>(tile->x), static_cast<uint32_t>(tile->y), guest_walk::kDanceTilePoint);
    const int32_t drawX = Rand();
    const int32_t drawY = Rand();
    const guest_walk::Goal goal = guest_walk::DanceGoal(point.x, point.y, drawX, drawY);
    PostToSelf(guest, messages::MT_PAVEMENT2POINT, goal.x, goal.y);
    return true;
}

// ---- WalkAlongPath -----------------------------------------------------------------------------------------------------------------------

// The walk a guest takes from where it stands (src/game/objects/guest_walk_path.h): with a walkable tile around, one draw picks the direction and the guest goes to the
// last tile of that row (all three coordinates of its centre become walkpoint 0), posting the goal to itself; a guest in the desperate state first thinks
// "no path" about the building, the sea or the beach and misses its purchase. With none it misses the purchase, thinks "no path at all", stands and asks itself again
// in two seconds. A guest in the desperate state then loses the variety effect of its definition from its variety opinion. A guest without a
// definition ends the game with a fatal error at that step (before anything was drawn or written).
bool RunWalkAlongPath(Guest& guest)
{
    const bool desperate = guest.m_state == guest_path::kStateDesperate;
    if (desperate && !guest.m_definition) return false;
    const TileXY here = TileOf(guest);
    guest.m_ActionState = guest_path::ActionStateOf(guest.m_state, guest.m_WalkAnimation, sizeof(guest.m_WalkAnimation));
    const bool walks = guest_path::WalkTiles(
            *GetGrid(), here.x, here.y, [] { return Rand(); },
            [&](TileXY tile) {
                float buffer[3];
                const float* centre = CentreOf(tile, buffer);
                for (int axis = 0; axis < 3; ++axis) guest.m_walkpoints[0][axis] = centre[axis];
            });
    if (walks) {
        guest.m_walkpoint = 0;
        if (desperate) {
            guest.m_missedPurchase = 1;
            if (guest.m_activity == 0) {
                const tables::BuildingDef* def = world::world_params::GetBuildingDef(GetWorldParams(), guest.m_CurTypeID);
                const int text = GuestThought(guest, std::string(guest_path::kThoughtBuilding) + def->m_key);
                GuestBuildingThought(guest, static_cast<Building*>(object_manager::ObjectById(GetObjectManager(), guest.m_CurID)), text);
            } else {
                GuestThought(
                        guest, guest.m_desired_commod == guest_path::kSeaCommodity ? guest_path::kThoughtSea : guest_path::kThoughtBeach);
            }
        }
        PostToSelf(guest, messages::MT_PAVEMENT2POINT, FloatToIntWrap(guest.m_walkpoints[0][0]), FloatToIntWrap(guest.m_walkpoints[0][1]));
    } else {
        guest.m_missedPurchase = 1;
        GuestThought(guest, guest_path::kThoughtAtAll);
        guest.PlayAnimation(guest_path::kStandAnimation, 0, -1, 1, nullptr);
        messages::Message retry{};
        retry.sender = retry.receiver = static_cast<int>(guest.m_id);
        retry.type = guest_path::kRetryMessage;
        retry.extra = guest.m_state;
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &retry, guest_path::kRetryDelay);
    }
    // The state and the definition are read again: the goal message may have been delivered at once.
    if (guest.m_state == guest_path::kStateDesperate)
        guest_opinions::Lower(guest.m_opinion[tables::Variety], guest.m_definition->m_OpinionEffect[tables::Variety][1]);
    return true;
}

} // namespace

void Guest::StartPathWalk()
{
    if (!RunGetonPath(*this)) fatal::UnhandledInput("objects.guest-geton-path", "no ray meets a walkable tile");
    g_geton.Count();
}

void Guest::SurfToShore()
{
    g_surf.Count();
    RunSurfToBeach(*this);
}

void Guest::WindSurfStraight()
{
    if (!RunWindSurfStraight(*this)) fatal::UnhandledInput("objects.guest-wind-surf-straight", "no sprite or board sprite");
    g_windSurf.Count();
}

void Guest::GotoDancePoint()
{
    if (!RunGotoDancePoint(*this)) fatal::UnhandledInput("objects.guest-goto-dance-point", "no dance floor or no reachable tile");
    g_dance.Count();
}

void Guest::WalkAlongPath()
{
    if (!RunWalkAlongPath(*this)) fatal::UnhandledInput("objects.guest-walk-along-path", "the guest has no definition");
    g_path.Count();
}

} // namespace obl::object
