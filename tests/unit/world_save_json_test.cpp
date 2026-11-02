#include "unit_test.h"
#include "game/world/world_save_json.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>

using namespace obl;

namespace {

world_save::JsonContext Context()
{
    world_save::JsonContext context;
    context.narrowToUtf8 = [](std::string_view text) { return std::string(text); };
    context.buildingKey = [](int32_t id) { return id == 3 ? std::string("beachbar") : id == 5 ? std::string("hotel") : std::string(); };
    return context;
}

} // namespace

OBL_TEST_CASE("world_save_json.writes_the_obl_save_keys_and_drops_the_old_only_records")
{
    auto world = std::make_unique<world::World>();
    std::snprintf(world->m_mapId, sizeof(world->m_mapId), "island1");
    world->m_gameTime = 12.5f;
    world->m_gameDay = 4;
    world->m_startup = 1;
    world->m_numValidBuildings = 2;
    world->m_validBuildingIDs[0] = 5;
    world->m_validBuildingIDs[1] = 3;
    world->m_validBuildingIDs[2] = 7; // past the count: not written
    world->m_numBoats = 1;
    world->m_objectiveStatus[1] = 2;
    world->m_profitRecord[3] = std::numeric_limits<float>::quiet_NaN();
    nlohmann::ordered_json out;
    std::string error;
    OBL_REQUIRE(world_save::WriteWorldJson(*world, 6, Context(), out, error));
    OBL_CHECK_EQ(error, std::string());
    OBL_CHECK_EQ(out["map_id"].get<std::string>(), std::string("island1"));
    OBL_CHECK(out["game_time"].get<double>() == 12.5);
    OBL_CHECK_EQ(out["game_day"].get<int>(), 4);
    OBL_CHECK(out["startup"].get<bool>());
    OBL_CHECK_EQ(out["available_buildings"].dump(), std::string(R"(["hotel","beachbar"])"));
    OBL_CHECK_EQ(out["objective_status"].dump(), std::string("[0,2,0,0]"));
    OBL_CHECK_EQ(out["profit_history"][3].get<std::string>(), std::string("f32:7fc00000"));
    OBL_CHECK_EQ(out["profit_history"].size(), size_t(0x80));
    OBL_CHECK_EQ(out["script_state"].get<int>(), 6);
    OBL_CHECK(!out.contains("boat_list"));
    OBL_CHECK(!out.contains("boat_count"));
    // The record order of the world save, available_buildings in the place of the valid-building records.
    OBL_CHECK_EQ(out.begin().key(), std::string("map_id"));
    OBL_CHECK_EQ((--out.end()).key(), std::string("script_state"));
}

OBL_TEST_CASE("world_save_json.refuses_values_obl_save_cannot_hold")
{
    auto world = std::make_unique<world::World>();
    world->m_numValidBuildings = 1;
    world->m_validBuildingIDs[0] = 9;
    nlohmann::ordered_json out;
    std::string error;
    OBL_CHECK(!world_save::WriteWorldJson(*world, 0, Context(), out, error));
    OBL_CHECK_EQ(error, std::string("World.available_buildings[0]: no catalog building 9"));

    world->m_numValidBuildings = 2;
    world->m_validBuildingIDs[0] = 3;
    world->m_validBuildingIDs[1] = 3;
    OBL_CHECK(!world_save::WriteWorldJson(*world, 0, Context(), out, error));
    OBL_CHECK_EQ(error, std::string("World.available_buildings: building beachbar listed twice"));

    world->m_numValidBuildings = 0;
    world->m_paused = 2;
    OBL_CHECK(!world_save::WriteWorldJson(*world, 0, Context(), out, error));
    OBL_CHECK_EQ(error, std::string("World.paused: bool value 2"));
}
