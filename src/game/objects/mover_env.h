#pragma once

#include "game/map/grid_state.h"
#include "game/ui/sprite.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/map/path_queries.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_building.h"
#include "game/messages/message_handler.h"
#include "game/game/startup_tables.h"

#include <cstdint>
#include "game/ui/map_view_math.h"

// The game calls of the moving-object target and event logic, shared by mover.cpp and mover_receive.cpp.
namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::PositionOffset;
using game::Rand;
using ui::GetMapView;

struct MoverEnv {
    static messages::Handler* Handler() { return messages::message_facade::Current(); }
    static object::Base* Object(int32_t id) { return object_manager::ObjectById(GetObjectManager(), id); }

    void Post(const messages::Message& message)
    {
        messages::Message copy = message;
        messages::message_facade::Post(Handler(), &copy);
    }
    void PostDelayed(const messages::Message& message, float delay)
    {
        messages::Message copy = message;
        messages::message_facade::PostDelayed(Handler(), &copy, delay);
    }
    void SendDelayed(object::Mover& object, const messages::Message& message, float delay) { object.SendDelayed(message, delay); }
    void ReleaseCursor(path::RouteCursor* cursor) { path::ReleaseCursor(cursor); }
    int Rand() { return game::Rand(); }
    float TerrainHeight(int32_t x, int32_t y) { return ui::GetMapView()->GetTerrainHeightF(x, y); }
    // A missing object (a stale id) reads as "stopped" / the origin.
    bool ObjectMoving(int32_t id)
    {
        const object::Base* object = Object(id);
        return object && static_cast<const object::Mover*>(object)->m_moving != 0;
    }
    void ObjectPosition(int32_t id, float out[3])
    {
        const object::Base* object = Object(id);
        for (int i = 0; i < 3; ++i) out[i] = object ? object->m_WorldXYZ[i] : 0.0f;
    }
    bool BuildingWalkPoint(int32_t building, uint32_t index, float out[2])
    {
        object::Base* object = Object(building);
        if (!object || object->m_type != object::ObjectType::Building) return false;
        if (index >= static_cast<object::Building*>(object)->m_numWalkPoints) return false;
        float point[2] = {0, 0};
        building_api::GetWalkPoint(static_cast<object::Building*>(object), point, static_cast<int32_t>(index));
        out[0] = point[0];
        out[1] = point[1];
        return true;
    }
    int32_t TileSize() { return GetGrid()->m_tileSize; }
    uint32_t TileFlags(uint32_t x, uint32_t y)
    {
        const map::Grid* grid = GetGrid();
        return grid->m_cells[(y % grid->m_height) * grid->m_width + x % grid->m_width].m_flags;
    }
    // z is not an input of the tile lookup; the env keeps it because the target contract passes the full position.
    void WorldToTile(float x, float y, float, uint32_t out[2])
    {
        const grid_math::TileXY tile = GetGrid()->WorldPointToTile(x, y);
        out[0] = tile.x;
        out[1] = tile.y;
    }
    void PositionOffset(int variant, float out[2])
    {
        const float* offset = game::PositionOffset(variant);
        out[0] = offset[0];
        out[1] = offset[1];
    }
    float WorldDelta() { return GetWorld()->m_gameDeltaTime; }
    void Teleport(object::Mover& object, float x, float y, float z) { object.Teleport(x, y, z); }
    // Placing scenery in the grid: the object's cell lists, base position and terrain height; `xyz` is the three raw words of the Teleport call.
    void ScenerySetPosition(object::Mover& object, const uint32_t xyz[3]) { object.Base::SetPosition(reinterpret_cast<const float*>(xyz)); }
    void SetSpriteDirection(void* sprite, float x, float y) { static_cast<ui::SpriteInstance*>(sprite)->SetDirectionFromVector(x, y); }
};
} // namespace obl::object
