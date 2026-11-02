#include "game/objects/object_moving.h"
#include "game/objects/object_base.h"
#include "game/objects/object_scenery.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "game/objects/building_api.h"
#include "game/map/grid_state.h"
#include "game/objects/mover_env.h"
#include "game/map/path_queries.h"
#include "game/map/path_route.h"
#include "game/objects/entity_messages.h"
#include "game/objects/object_base.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_building.h"
#include "game/map/grid_math.h"
#include "game/map/path_state.h"
#include "game/objects/mover_messages.h"
#include "game/map/path_tiles.h"
#include "core/call_counter.h"

#include <cstring>
#include "game/ui/map_view_math.h"
#include "game/game/diagnostics.h"

// Message handling of every moving class: the moving message families (mover_messages.h) and the messages every object
// answers (entity_messages.h), which static objects reach through Base::ReceiveCommonMessage.
namespace obl::object {
using game::FatalError;
using game::GetGrid;
using game::GetPath;
using game::GetWorld;

namespace {
using messages::Message;
using object::Mover;

// A message type the moving families do not handle, answered by the common handler.
test::CallCounter g_default("objects.moving-receive-common");

// The virtual calls and the fatal reports of the common handler.
struct BaseEnv {
    bool HasState(object::Base& object, uint32_t state) { return object.HasState(state); }
    bool ExitState(object::Base& object) { return object.OnExitState(); }
    bool EnterState(object::Base& object, uint32_t state) { return object.OnEnterState(state); }
    void Fatal(entity::Fatal kind, const object::Base& object, const Message& message)
    {
        const auto fatal = &FatalError;
        const int type = static_cast<int>(object.m_type);
        switch (kind) {
        case entity::Fatal::UnsupportedState:
            fatal("State %u is not supported (object type %d, sender %d)", static_cast<uint32_t>(message.data), type, message.sender);
            break;
        case entity::Fatal::ExitState: fatal("Leaving the current state failed"); break;
        case entity::Fatal::EnterState:
            fatal("Entering state %d failed (object type %d, sender %d)", message.data, type, message.sender);
            break;
        case entity::Fatal::InvalidOwner: fatal("An owner change named an object that cannot own others"); break;
        }
    }
};

struct ReceiveEnv : MoverEnv, BaseEnv {
    void FaceTowards(const object::Base& who, const float from[3], const float to[3])
    {
        (static_cast<ui::SpriteInstance*>(who.m_sprite))->FaceTowards(from[0], from[1], from[2], to[0], to[1], to[2]);
    }
    // A missing building is treated as having no points.
    uint32_t BuildingWalkPointCount(int32_t id)
    {
        const object::Base* building = Object(id);
        return building ? static_cast<const object::Building*>(building)->m_numWalkPoints : 0;
    }
    float GameTime() { return GetWorld()->m_gameTime; }
    // The facades fill the buffer and return it.
    void ServePosition(const object::Base& building, float out[3]) { BuildingPosition(&building_api::GetServePosition, building, out); }
    void QueueTailPosition(const object::Base& building, float out[3])
    {
        BuildingPosition(&building_api::GetQueueTailPosition, building, out);
    }
    static void BuildingPosition(float* (*get)(const object::Building*, float*), const object::Base& building, float out[3])
    {
        float buffer[3] = {0, 0, 0};
        const float* position = get(static_cast<const object::Building*>(&building), buffer);
        std::memcpy(out, position, 3 * sizeof(float));
    }
    void MapTile(const float (&world)[3], uint32_t out[2])
    {
        uint32_t tile[2] = {0, 0};
        ui::MapView::WorldToTile(tile, world);
        out[0] = tile[0];
        out[1] = tile[1];
    }
    path::RouteCursor* Land(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found, uint32_t option)
    {
        return GetPath()->CreateLandRoute(sx, sy, tx, ty, found, option);
    }
    // MT_PATH_THROUGH_SCENERY[2OBJECT]: the "land or blocked" route and the calls around it, all through the path_queries facades.
    bool TileAllowed(uint32_t x, uint32_t y, int routeType) { return GetPath()->IsTileAllowed(x, y, routeType); }
    // MT_PAVEMENT2*: the route BuildRoute builds, and the pure neighbour test of the "no route" report.
    path::RouteCursor* Pavement(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found)
    {
        return GetPath()->CreatePavementRoute(sx, sy, tx, ty, found);
    }
    bool Adjacent(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty) { return GetGrid()->AreTilesAdjacent(sx, sy, tx, ty); }
    bool Prepare(uint32_t* start, uint32_t* stop, uint32_t mask) { return path::PrepareRouteEndpoints(*GetGrid(), start, stop, mask); }
    // The route mask does not change which temporary tiles are cleared.
    void Clear(uint32_t* start, uint32_t* stop, uint32_t) { path::ClearTemporaryRouteTiles(*GetGrid(), start, stop); }
    path::RouteCursor* Scenery(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found, uint32_t option)
    {
        return GetPath()->CreateLandOrBlockedRoute(sx, sy, tx, ty, found, option);
    }
    // MT_SWIM2SEAPOINT / MT_SEAVEHICLE_MOVE2POINT: the sea route and the world point of a route tile.
    path::RouteCursor* Sea(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found)
    {
        return GetPath()->CreateSeaRoute(sx, sy, tx, ty, found);
    }
    void TileOrigin(uint32_t x, uint32_t y, float out[2])
    {
        const grid_math::WorldPoint point = grid_math::TileToWorldPoint(GetGrid()->m_tileSize, x, y, 0);
        out[0] = point.x;
        out[1] = point.y;
    }
    void ApproachPosition(const object::Base& scenery, float out[3])
    {
        const_cast<object::Scenery&>(static_cast<const object::Scenery&>(scenery)).GetApproachPosition(out);
    }
    void TilePosition(const object::Base& object, uint32_t out[2])
    {
        int32_t tile[4] = {0, 0, 0, 0};
        const int32_t* result = object.GetTilePosition(tile);
        out[0] = static_cast<uint32_t>(result[0]);
        out[1] = static_cast<uint32_t>(result[1]);
    }
};

} // namespace

bool Mover::ReceiveMessage(messages::Message* message)
{
    ReceiveEnv env;
    if (!moving::Handles(message->type)) g_default.Count();
    return moving::ReceiveWithDefault(*this, *message, env);
}

bool Base::ReceiveCommonMessage(messages::Message* message)
{
    BaseEnv env;
    return entity::ReceiveMessage(*this, *message, env);
}
} // namespace obl::object
