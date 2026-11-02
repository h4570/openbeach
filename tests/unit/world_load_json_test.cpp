#include "unit_test.h"
#include "game/world/world_load_json.h"
#include "game/world/world_save_json.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

using namespace obl;

namespace {

world_save::JsonContext WriteContext()
{
    world_save::JsonContext context;
    context.narrowToUtf8 = [](std::string_view text) { return std::string(text); };
    context.buildingKey = [](int32_t id) { return id == 3 ? std::string("beachbar") : id == 5 ? std::string("hotel") : std::string(); };
    return context;
}

world_save::ReadContext ReadContext()
{
    world_save::ReadContext context;
    context.utf8ToNarrow = [](std::string_view text, std::string& out) {
        out = text;
        return text.find('~') == std::string_view::npos; // a stand-in for a character outside the code page
    };
    context.buildingId = [](std::string_view key) { return key == "beachbar" ? 3 : key == "hotel" ? 5 : -1; };
    return context;
}

} // namespace

OBL_TEST_CASE("world_load_json.reads_back_what_the_writer_wrote")
{
    auto world = std::make_unique<world::World>();
    std::snprintf(world->m_mapId, sizeof(world->m_mapId), "island1");
    world->m_gameTime = 12.5f;
    world->m_gameDay = 4;
    world->m_startup = 1;
    world->m_numValidBuildings = 2;
    world->m_validBuildingIDs[0] = 5;
    world->m_validBuildingIDs[1] = 3;
    world->m_objectiveStatus[1] = 2;
    world->m_scriptVars[7] = -9;
    world->m_todayGuestDepart[1] = 11;
    world->m_profitRecord[3] = std::numeric_limits<float>::quiet_NaN();
    world->m_costRecord[127] = -0.0f;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteWorldJson(*world, 6, WriteContext(), written, error));

    auto loaded = std::make_unique<world::World>();
    loaded->m_numBoats = 3; // read as the zero the bridge encodes for the dropped record
    loaded->m_boatList[2] = 7;
    uint32_t scriptState = 0;
    OBL_REQUIRE(world_save::ReadWorldJson(nlohmann::json(written), ReadContext(), *loaded, scriptState, error));
    OBL_CHECK_EQ(error, std::string());
    OBL_CHECK_EQ(scriptState, 6u);
    OBL_CHECK_EQ(std::string(loaded->m_mapId), std::string("island1"));
    OBL_CHECK_EQ(loaded->m_gameDay, 4);
    OBL_CHECK_EQ(loaded->m_numValidBuildings, 2);
    OBL_CHECK_EQ(loaded->m_validBuildingIDs[0], 5);
    OBL_CHECK_EQ(loaded->m_validBuildingIDs[1], 3);
    OBL_CHECK_EQ(loaded->m_numBoats, 0);
    OBL_CHECK_EQ(loaded->m_boatList[2], 0);
    OBL_CHECK_EQ(loaded->m_objectiveStatus[1], 2u);
    OBL_CHECK_EQ(loaded->m_scriptVars[7], -9);
    OBL_CHECK_EQ(loaded->m_todayGuestDepart[1], 11);
    OBL_CHECK(std::memcmp(&loaded->m_profitRecord[3], &world->m_profitRecord[3], sizeof(float)) == 0);
    OBL_CHECK(std::memcmp(&loaded->m_costRecord[127], &world->m_costRecord[127], sizeof(float)) == 0);
    nlohmann::ordered_json again;
    OBL_REQUIRE(world_save::WriteWorldJson(*loaded, scriptState, WriteContext(), again, error));
    OBL_CHECK_EQ(again.dump(), written.dump());
}

OBL_TEST_CASE("world_load_json.missing_optional_keys_read_as_the_bridge_defaults")
{
    auto world = std::make_unique<world::World>();
    world->m_gameDay = 9;
    world->m_paused = 1;
    world->m_numValidBuildings = 4;
    world->m_validBuildingIDs[0] = 8;
    world->m_objectiveStatus[3] = 1;
    world->m_incomeRecord[0] = 2.0f;
    std::snprintf(world->m_mapId, sizeof(world->m_mapId), "old");
    uint32_t scriptState = 5;
    std::string error;
    OBL_REQUIRE(world_save::ReadWorldJson(nlohmann::json::object(), ReadContext(), *world, scriptState, error));
    OBL_CHECK_EQ(world->m_gameDay, 0);
    OBL_CHECK_EQ(world->m_paused, 0);
    OBL_CHECK_EQ(world->m_numValidBuildings, 0);
    OBL_CHECK_EQ(world->m_validBuildingIDs[0], 0);
    OBL_CHECK_EQ(world->m_objectiveStatus[3], 0u);
    OBL_CHECK(world->m_incomeRecord[0] == 0.0f);
    OBL_CHECK_EQ(world->m_mapId[0], '\0');
    OBL_CHECK_EQ(scriptState, 0u);
}

OBL_TEST_CASE("world_load_json.refuses_values_the_world_cannot_hold")
{
    auto world = std::make_unique<world::World>();
    uint32_t scriptState = 0;
    std::string error;
    OBL_CHECK(!world_save::ReadWorldJson(
            nlohmann::json{{"available_buildings", {"hotel", "pier"}}}, ReadContext(), *world, scriptState, error));
    OBL_CHECK_EQ(error, std::string("World.available_buildings: building \"pier\" not in the catalog"));
    OBL_CHECK(!world_save::ReadWorldJson(nlohmann::json{{"map_id", "a~b"}}, ReadContext(), *world, scriptState, error));
    OBL_CHECK_EQ(error, std::string("World.map_id: text outside the pack's code page"));
    OBL_CHECK(!world_save::ReadWorldJson(nlohmann::json{{"script_variables", {1, 2}}}, ReadContext(), *world, scriptState, error));
    OBL_CHECK_EQ(error, std::string("World.script_variables: not a list of 32"));
    OBL_CHECK(!world_save::ReadWorldJson(nlohmann::json{{"game_time", "soon"}}, ReadContext(), *world, scriptState, error));
    OBL_CHECK_EQ(error, std::string("World.game_time: not a float"));
    OBL_CHECK(!world_save::ReadWorldJson(nlohmann::json{{"objective_status", {0, 1}}}, ReadContext(), *world, scriptState, error));
    OBL_CHECK_EQ(error, std::string("World.objective_status: no element 2"));
}
