#include "game/ui/sprite.h"
#include "core/ascii_case.h"
#include "game/map/grid_state.h"
#include "game/objects/object_base.h"
#include "game/world/world_create.h"
#include "world_create_building.h"

#include "game/objects/object_building.h"
#include "core/log.h"
#include "test_util.h"
#include "world_state.h"

#include <algorithm>
#include <cstring>
#include "game/objects/object_create_params.h"
#include "game/ui/map_view_math.h"

namespace obl::test {
namespace {
void Check(bool condition, const char* detail)
{
    OBL_LOG("autotest: %s world-create-building %s", condition ? "PASS" : "FAILED", detail);
}

void CheckPoint(const float (&point)[3], const object::BuildingCreateParams& record, const tables::BuildingDef& def,
        const int16_t (&offset)[2], const char* detail)
{
    // These fields are omitted from world-hash. Query the grid helper as the oracle
    // for the factory's selected offset/rotation, and check the stored terrain height too.
    float expected[3]{};
    auto* grid = game::GetGrid();
    const grid_math::PathPoint at = grid->GetFootprintOffsetPoint(static_cast<uint32_t>(record.m_tileX),
            static_cast<uint32_t>(record.m_tileY), def.m_GridWidth, def.m_GridHeight, static_cast<float>(offset[0]),
            static_cast<float>(offset[1]), static_cast<uint32_t>(record.m_orientation));
    expected[0] = at.x;
    expected[1] = at.y;
    const float height =
            static_cast<float>(World()->m_mapView->GetTerrainHeightF(static_cast<int>(expected[0]), static_cast<int>(expected[1])));
    Check(point[0] == expected[0] && point[1] == expected[1] && point[2] == height, detail);
}
} // namespace

bool ExecuteWorldCreateBuildingCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "world-create-building") return false;
    auto* world = World();
    auto* params = game::GetWorldParams();
    if (!args.empty() || !world || !params || GameStateIndex() != 0 || Game()->m_guiReloadPending) {
        Check(false, "requires settled gameplay and no arguments");
        return true;
    }
    const tables::BuildingDef* def = nullptr;
    for (int i = 0; i < params->m_numBuildings; ++i)
        if (ascii::EqualsNoCase(params->m_buildings[i].m_key, "BAR2")) def = &params->m_buildings[i];
    if (!def || def->m_NumCommodities < 1) {
        Check(false, "BAR2 definition with commodities");
        return true;
    }
    int beerId = -1;
    for (int i = 0; i < params->m_numBeers; ++i)
        if (params->m_beers[i].m_scriptFlag != 0) {
            beerId = i;
            break;
        }
    if (beerId == -1) {
        Check(false, "enabled beer definition");
        return true;
    }

    // Descending Y requires the final insertion to move the newly placed building. The four
    // records cover all rotations and optional prices, beer overrides, age and initial damage.
    for (int rotation = 0; rotation < 4; ++rotation) {
        object::BuildingCreateParams record{};
        std::strcpy(record.m_key, "BAR2");
        record.m_tileX = 80;
        record.m_tileY = 80 - rotation * 15;
        record.m_orientation = rotation;
        for (float& price : record.m_prices) price = -1.0f;
        record.m_prices[0] = 7.25f;
        record.m_beerType = rotation == 0 ? -1 : beerId;
        record.m_beerStrength = 0.75f;
        record.m_age = 17 + rotation;
        record.m_broken = rotation == 3 ? 1 : 0;
        auto& beer = params->m_beers[beerId];
        const uint8_t enabled = beer.m_scriptFlag;
        if (rotation == 2) beer.m_scriptFlag = 0;
        const int count = world->m_numBuildings;
        auto* building = static_cast<object::Building*>(world::CreateObject(*world, object::ObjectType::Building, &record));
        beer.m_scriptFlag = enabled;
        if (!building) {
            Check(false, "factory return");
            return true;
        }
        Check(building->m_type == object::ObjectType::Building && building->m_definition == def && world->m_numBuildings == count + 1,
                "class, definition, world counter");
        Check(building->m_gridW == ((rotation & 1) ? def->m_GridHeight : def->m_GridWidth) &&
                        building->m_gridH == ((rotation & 1) ? def->m_GridWidth : def->m_GridHeight),
                "rotated footprint");
        auto* sprite = static_cast<ui::SpriteInstance*>(building->m_sprite);
        Check(sprite->m_direction == std::min(rotation, static_cast<int>(sprite->m_sprite->m_anims[sprite->m_animIndex]->m_directions) - 1),
                "sprite direction");
        Check(building->m_BuildingAge == record.m_age && building->m_PowerConsumption == static_cast<uint32_t>(def->m_Power),
                "age and power");
        CheckPoint(building->m_serve_point, record, *def, def->m_ServeOffset, "serve point and terrain height");
        constexpr float kQueueDirections[4][2]{{16.0f, 16.0f}, {16.0f, -16.0f}, {-16.0f, -16.0f}, {-16.0f, 16.0f}};
        Check(building->m_queue_direction[0] == kQueueDirections[rotation][0] &&
                        building->m_queue_direction[1] == kQueueDirections[rotation][1],
                "queue direction");
        if (rotation == 0 || rotation == 2)
            Check(building->m_Commod_Price[0] == 7.25f, "explicit price and skipped beer override");
        else
            Check(building->m_BeerType == beerId && building->m_BeerStrength == 0.75f, "enabled beer type and strength");
        if (rotation == 3) Check(building->m_RepairState == 3, "initial broken state");
        const auto* manager = game::GetObjectManager();
        bool sorted = true;
        for (uint32_t i = 1; i < manager->m_buildings.size(); ++i)
            sorted &= manager->m_buildings[i - 1]->m_WorldXYZ[1] <= manager->m_buildings[i]->m_WorldXYZ[1];
        Check(sorted, "global building list sorted by Y");
    }
    auto* generator = SpawnBuilding("POWERGEN", 65, 30);
    Check(generator && static_cast<int32_t>(generator->m_PowerConsumption) < 0, "generator negative power");
    auto* cafe = SpawnBuilding("CAFE1", 65, 50);
    // Tables are spawned on exit from construction state 1, after the factory returns.
    Check(cafe && cafe->m_definition->m_NumTables > 0, "cafe with dependent table definitions");
    // Scenario 1 creates HOTEL before gameplay; it exercises the nonzero entrance offsets.
    bool hotelFound = false;
    const auto* manager = game::GetObjectManager();
    for (uint32_t i = 0; i < manager->m_buildings.size(); ++i) {
        const auto* hotel = static_cast<const object::Building*>(manager->m_buildings[i]);
        if (!ascii::EqualsNoCase(hotel->m_definition->m_key, "HOTEL")) continue;
        hotelFound = true;
        Check(hotel->m_Entrance != 0, "scenario hotel entrance enabled");
        uint32_t tile[2]{};
        hotel->GetTilePosition(reinterpret_cast<int32_t*>(tile));
        object::BuildingCreateParams record{};
        record.m_tileX = static_cast<int>(tile[0]);
        record.m_tileY = static_cast<int>(tile[1]);
        record.m_orientation = static_cast<const ui::SpriteInstance*>(hotel->m_sprite)->m_direction;
        CheckPoint(hotel->m_EntrancePoint, record, *hotel->m_definition, hotel->m_definition->m_EntranceOffset,
                "scenario hotel entrance point and terrain height");
    }
    Check(hotelFound, "scenario hotel exercises entrance branch");
    return true;
}
} // namespace obl::test
