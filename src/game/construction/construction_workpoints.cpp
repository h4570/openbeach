#include "game/objects/object_base.h"
#include "game/construction/construction_manager.h"
#include "game/map/grid_state.h"

#include "game/construction/construction_points.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <cstdint>

namespace obl::construction {
namespace {

using object::Building;
using ui::SpriteInstance;
const auto kGetTilePosition = [](object::Base* object, int* out) { return object->GetTilePosition(out); };

constexpr int kWorkPointWorldKind = 4; // the Grid::TileToWorldPoint point CollectWorkPoints asks for

test::CallCounter g_answered("construction.work-points");

object::Building* GetBuilding(int id)
{
    return static_cast<object::Building*>(object::object_manager::ObjectById(game::GetObjectManager(), id));
}

// The game side of the Env contracts in src/game/construction/construction_points.h.
struct WorkPointEnv {
    bool MaxBuilders(int id, float* limit)
    {
        const object::Building* building = GetBuilding(id);
        if (!building || !building->m_definition) return false;
        *limit = building->m_definition->m_MaxBuilders;
        return true;
    }

    construction::EntryBuilding Building(int id)
    {
        construction::EntryBuilding out;
        object::Building* building = GetBuilding(id);
        if (!building || !building->m_definition) return out;
        int tile[2] = {};
        kGetTilePosition(building, tile);
        out = {true, building->m_definition->m_footprint != nullptr, tile[0], tile[1]};
        return out;
    }
    construction::Position JobPosition(int id, int token)
    {
        return construction::GetJobPosition(*game::GetWorld()->m_constructionManager, id, token, *this);
    }

    bool Building(int id, construction::CollectBuilding* out)
    {
        object::Building* building = GetBuilding(id);
        if (!building || !building->m_definition) return false;
        const tables::BuildingDef& def = *building->m_definition;
        int tile[2] = {};
        kGetTilePosition(building, tile);
        const auto* sprite = static_cast<const SpriteInstance*>(building->m_sprite);
        out->exists = true;
        out->tileX = tile[0];
        out->tileY = tile[1];
        out->rotation = sprite ? static_cast<uint32_t>(sprite->m_direction) : 0;
        out->gridWidth = def.m_GridWidth;
        out->gridHeight = def.m_GridHeight;
        // Layer 1 of the footprint block (docs/specs/construction.md): two dwords of size, then 25 rows of 25 cells per layer.
        out->workLayer = def.m_footprint ? &def.m_footprint->m_layers[1][0][0] : nullptr;
        return true;
    }
    void Orient(construction::TileXY* origin, uint32_t rows, uint32_t columns, int* columnDirection, int* rowDirection, uint32_t rotation)
    {
        game::GetGrid()->OrientFootprintTraversal(
                &origin->x, static_cast<int>(rows), static_cast<int>(columns), columnDirection, rowDirection, static_cast<int>(rotation));
    }
    construction::TileXY Neighbour(construction::TileXY tile, int direction)
    {
        const grid_math::TileXY next = game::GetGrid()->GetNeighbour(tile.x, tile.y, direction);
        return {next.x, next.y};
    }
    void TileToWorld(construction::TileXY tile, float* xy)
    {
        const grid_math::WorldPoint point = game::GetGrid()->TileToWorldPoint(tile.x, tile.y, kWorkPointWorldKind);
        xy[0] = point.x;
        xy[1] = point.y;
    }
};

} // namespace

namespace runtime {

// construction::CollectWorkPoints: the building id. It writes the job's work points.
void CollectWorkPoints(construction::Manager* manager, int buildingId)
{
    WorkPointEnv env;
    g_answered.Count();
    construction::CollectWorkPoints(*manager, buildingId, env);
}

// BuildEntryPoint: the builder script asks it for BuildEntPointX/Y, with the builder's fallback position. The claim
// changes the world's manager, as in GetJobPosition.
void BuildEntryPoint(int buildingId, int builderToken, const float fallback[3], float out[3])
{
    WorkPointEnv env;
    g_answered.Count();
    const construction::Position position =
            construction::BuildEntryPoint(buildingId, builderToken, construction::Position{fallback[0], fallback[1], fallback[2]}, env);
    out[0] = position.x;
    out[1] = position.y;
    out[2] = position.z;
}

} // namespace runtime

} // namespace obl::construction
