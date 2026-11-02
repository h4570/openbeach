#include "game/objects/scenery_names.h"
#include "game/objects/object_base.h"
#include "core/ascii_case.h"
#include "game/construction/construction_manager.h"

#include "game/construction/construction_footprint.h"
#include "game/map/cell_objects.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/map/grid_state.h"
#include "game/map/path_queries.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/construction/placement_types.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "game/messages/message_handler.h"
#include "game/world/world_messages.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace obl::construction {
namespace {

using construction::TileXY;
using object::Building;
using ui::SpriteInstance;
const auto kGetTilePosition = [](object::Base* object, int* out) { return object->GetTilePosition(out); };
const auto kStatus = &cleaning::cleaning_facade::GetJobStatus;
const auto kSetTopPosition = &object::building_api::SetTopWorldPosition;

object::Building* GetBuilding(int id)
{
    return static_cast<object::Building*>(object::object_manager::ObjectById(game::GetObjectManager(), id));
}

// The building and the grid geometry, as the game gives them: the part of the Env that only queries.
struct GeometryEnv {
    bool Building(int id, construction::ClearBuilding* out)
    {
        object::Building* building = GetBuilding(id);
        if (!building || !building->m_definition) return false;
        const auto* sprite = static_cast<const SpriteInstance*>(building->m_sprite);
        if (!sprite) return false;
        const tables::BuildingDef& def = *building->m_definition;
        int tile[2] = {};
        kGetTilePosition(building, tile);
        out->exists = true;
        out->tileX = tile[0];
        out->tileY = tile[1];
        out->rotation = static_cast<uint32_t>(sprite->m_direction);
        out->gridWidth = def.m_GridWidth;
        out->gridHeight = def.m_GridHeight;
        // Layer 0 of the footprint block (docs/specs/construction.md): two dwords of size, then 25 rows of 25 cells.
        const auto* footprint = def.m_footprint.get();
        out->terrainLayer = footprint ? &footprint->m_layers[0][0][0] : nullptr;
        return true;
    }
    bool Building(int id, construction::ApplyBuilding* out)
    {
        if (!Building(id, static_cast<construction::ClearBuilding*>(out))) return false;
        const auto& def = *GetBuilding(id)->m_definition;
        if (out->terrainLayer) {
            const auto* footprint = def.m_footprint.get();
            out->workLayer = &footprint->m_layers[1][0][0];
            out->depthLayer = &footprint->m_layers[2][0][0];
            out->bodyLayer = &footprint->m_layers[3][0][0];
        }
        out->surfaceCount = static_cast<uint32_t>(def.m_NumBasePlates);
        out->surfaces = def.m_NumBasePlates ? def.m_basePlateTypes.data() : nullptr;
        return true;
    }
    bool Building(int id, construction::BuildingToGrid* out)
    {
        if (!Building(id, static_cast<construction::ClearBuilding*>(out))) return false;
        const object::Building& building = *GetBuilding(id);
        const tables::BuildingDef& def = *building.m_definition;
        out->defWidth = static_cast<uint32_t>(def.m_GridWidth);
        out->defHeight = static_cast<uint32_t>(def.m_GridHeight);
        out->hasFootprint = def.m_footprint != nullptr;
        out->serveOffsetX = def.m_ServeOffset[0];
        out->serveOffsetY = def.m_ServeOffset[1];
        out->objectGridWidth = building.m_gridW;
        out->objectGridHeight = building.m_gridH;
        return true;
    }
    int Rand() { return game::Rand(); }
    void FootprintOffsetPoint(TileXY tile, uint32_t width, uint32_t height, float offsetX, float offsetY, uint32_t rotation, float* out)
    {
        const grid_math::PathPoint point = game::GetGrid()->GetFootprintOffsetPoint(
                tile.x, tile.y, static_cast<int>(width), static_cast<int>(height), offsetX, offsetY, rotation);
        out[0] = point.x;
        out[1] = point.y;
    }
    TileXY WorldToTile(float x, float y)
    {
        const grid_math::TileXY tile = game::GetGrid()->WorldPointToTile(x, y);
        return {tile.x, tile.y};
    }
    void TileToWorld(TileXY tile, float* out)
    {
        const grid_math::WorldPoint point = game::GetGrid()->TileToWorldPoint(tile.x, tile.y, 0);
        out[0] = point.x;
        out[1] = point.y;
        out[2] = point.z;
    }
    bool IsLitter(const object::Base* object) { return scenery_names::IsLitter(object->m_Name); }
    void Orient(TileXY* origin, uint32_t rows, uint32_t columns, int* columnDirection, int* rowDirection, uint32_t rotation)
    {
        game::GetGrid()->OrientFootprintTraversal(
                &origin->x, static_cast<int>(rows), static_cast<int>(columns), columnDirection, rowDirection, static_cast<int>(rotation));
    }
    TileXY Neighbour(TileXY tile, int direction)
    {
        const grid_math::TileXY next = game::GetGrid()->GetNeighbour(tile.x, tile.y, direction);
        return {next.x, next.y};
    }
};

struct GameEnv : GeometryEnv {
    construction::ClearCell Cell(TileXY tile)
    {
        map::GridCell& cell = game::GetGrid()->Cell(tile.x, tile.y);
        return {&cell.m_flags, &cell.m_zOffset};
    }
    void RebuildTile(TileXY tile) { game::GetPathTiles()->RebuildTile(tile.x, tile.y, 0); }
    void RebuildTile(TileXY tile, int removeNeighbours) { game::GetPathTiles()->RebuildTile(tile.x, tile.y, removeNeighbours); }
    void SetSurface(TileXY tile, int type) { game::GetPathTiles()->SetSurface(tile.x, tile.y, type); }
    std::vector<object::Base*> Objects(TileXY tile)
    {
        // The list is copied before messages are delivered synchronously.
        std::vector<object::Base*> objects;
        cell_objects::ForEach(
                *game::GetObjectManager(), game::GetGrid()->Cell(tile.x, tile.y), [&](object::Base* object) { objects.push_back(object); });
        return objects;
    }
    int JobStatus(object::Base* object) { return kStatus(game::GetWorld()->m_cleaningManager, static_cast<int>(object->m_id)); }
    void RemoveLitterObject(object::Base* object)
    {
        messages::Message message{};
        message.sender = message.receiver = static_cast<int>(object->m_id);
        message.type = messages::MT_STATECHANGE;
        message.data = 3;
        messages::message_facade::Post(game::GetWorld()->m_messageHandler, &message);
        object->m_waitingForDeath = 1;
        message = {};
        message.sender = static_cast<int>(object->m_id);
        message.receiver = -1;
        message.type = messages::MT_BUILDING_BEING_REMOVED;
        world::PostMessageToWorldClass(*game::GetWorld(), message, static_cast<int>(object::ObjectType::Cleaner));
    }
    void RemoveLitter(TileXY tile) { construction::RemoveFootprintLitter(tile, *this); }
};

struct GridGameEnv : GameEnv {
    construction::Manager* manager = nullptr;
    void ApplyFootprint(TileXY origin, int phase, int mode, int id)
    {
        runtime::ApplyFootprint(manager, origin.x, origin.y, phase, mode, id);
    }
    void SetTopWorldPosition(const float* point) { kSetTopPosition(top, point[0], point[1], point[2]); }
    object::Building* top = nullptr;
};

} // namespace

namespace runtime {

// construction::ClearFootprint: the building id.
void ClearFootprint(construction::Manager*, int id)
{
    GameEnv env;

    construction::ClearFootprint(id, env);
}

void ApplyFootprint(construction::Manager*, uint32_t x, uint32_t y, int phase, int mode, int id)
{
    GameEnv env;

    construction::ApplyFootprint({x, y}, phase, mode, id, env);
}

// construction::ApplyBuildingToGrid: the building id; the manager is not used.
void ApplyBuildingToGrid(construction::Manager* manager, int id)
{
    GridGameEnv env;
    env.manager = manager;

    env.top = GetBuilding(id);
    construction::ApplyBuildingToGrid(id, env);
}

} // namespace runtime

} // namespace obl::construction
