#include "dirty_world.h"
#include "game/world/world_state.h"
#include "unit_test.h"

#include "game/world/world_construct_pis.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using world_data::World;

namespace {

struct MissingKeyError {
    std::string key;
};

// A PIS as maps of keys; Duplicate keeps the copies alive and records that each one is a distinct allocation.
struct FakeEnv {
    std::map<std::string, std::string> strings;
    std::map<std::string, float> numbers;
    std::vector<std::string> messages;
    std::vector<std::unique_ptr<std::string>> copies;

    const char* GetString(const char* key)
    {
        const auto it = strings.find(key);
        return it == strings.end() ? nullptr : it->second.c_str();
    }
    const float* GetNumber(const char* key)
    {
        const auto it = numbers.find(key);
        return it == numbers.end() ? nullptr : &it->second;
    }
    const char* Duplicate(const char* text)
    {
        copies.push_back(std::make_unique<std::string>(text));
        return copies.back()->c_str();
    }
    [[noreturn]] void MissingKey(const char* key) { throw MissingKeyError{key}; }
    int BuildingId(const char* name) { return static_cast<int>(std::strlen(name)) * 10; }
    void AddScenarioMessage(const char* text) { messages.push_back(text); }
};

std::unique_ptr<World> DirtyWorld()
{
    auto world = std::make_unique<World>();
    DirtyGroups(*world);
    return world;
}

FakeEnv FullPis()
{
    FakeEnv env;
    for (const char* key : {"WORLD_NAME", "WORLD_MAPFILENAME", "WORLD_SCRIPT", "WORLD_BUILDINGLIST", "WORLD_GUESTLIST",
                 "WORLD_MAPHEIGHTMAP", "WORLD_MAPGRID", "WORLD_PATHTILEINFO"})
        env.strings[key] = std::string("v_") + key;
    env.numbers["STARTMAPX"] = 10.0f;
    env.numbers["STARTMAPY"] = 20.0f;
    env.numbers["WORLD_HEIGHTMAPSCALE"] = 0.25f;
    return env;
}

std::string MissingOf(FakeEnv& env)
{
    try {
        world_construct_pis::ReadHeader(env);
    } catch (const MissingKeyError& e) {
        return e.key;
    }
    return "";
}

} // namespace

OBL_TEST_CASE("world_construct_pis.header_reads_required_and_optional_keys")
{
    FakeEnv env = FullPis();
    env.strings["WORLD_WATERANIM"] = "water";
    env.numbers["WORLD_DEPTH_SLANT_OFFSET"] = 7.9f;
    const auto h = world_construct_pis::ReadHeader(env);
    OBL_CHECK_EQ(std::string(h.name), "v_WORLD_NAME");
    OBL_CHECK_EQ(std::string(h.script), "v_WORLD_SCRIPT");
    OBL_CHECK_EQ(std::string(h.waterAnim), "water");
    OBL_CHECK(h.pavementTileset == nullptr);
    OBL_CHECK(h.beachTileset == nullptr);
    OBL_CHECK_EQ(*h.startMapX, 10.0f);
    OBL_CHECK_EQ(*h.heightScale, 0.25f);
    OBL_CHECK(h.depthPlane == nullptr);
    OBL_CHECK_EQ(world_construct_pis::DepthPlaneOffset(h.depthPlane), 0);
    OBL_CHECK_EQ(world_construct_pis::DepthSlantOffset(h.depthSlant), 7);
    OBL_CHECK_EQ(world_construct_pis::DepthSlantOffset(nullptr), 0x10);
}

OBL_TEST_CASE("world_construct_pis.header_reports_the_first_missing_required_key_in_the_fixed_order")
{
    const char* order[] = {"WORLD_NAME", "WORLD_MAPFILENAME", "WORLD_MAPHEIGHTMAP", "WORLD_MAPGRID", "WORLD_PATHTILEINFO", "STARTMAPX",
            "STARTMAPY", "WORLD_SCRIPT", "WORLD_BUILDINGLIST", "WORLD_GUESTLIST", "WORLD_HEIGHTMAPSCALE"};
    for (const char* key : order) {
        FakeEnv env = FullPis();
        env.strings.erase(key);
        env.numbers.erase(key);
        OBL_CHECK_EQ(MissingOf(env), std::string(key));
    }
    FakeEnv env = FullPis();
    env.strings.erase("WORLD_GUESTLIST");
    env.strings.erase("WORLD_NAME");
    OBL_CHECK_EQ(MissingOf(env), "WORLD_NAME");
    FakeEnv complete = FullPis();
    OBL_CHECK_EQ(MissingOf(complete), "");
}

OBL_TEST_CASE("world_construct_pis.fatal_error_texts_are_exact")
{
    const char* fatalTexts[] = {"no WORLD_NAME in world script - %s", "no WORLD_MAPFILENAME in world script - %s",
            "no WORLD_MAPHEIGHTMAP in world script - %s", "no WORLD_MAPGRID in world script - %s",
            "no WORLD_PATHTILEINFO in world script - %s", "no STARTMAPX in world script - %s", "no STARTMAPY in world script - %s",
            "no WORLD_SCRIPT in world script - %s", "no WORLD_BUILDINGLIST in world script - %s", "no WORLD_GUESTLIST in world script - %s",
            "no WORLD_HEIGHTMAPSCALE in world script - %s"};
    const char* order[] = {"WORLD_NAME", "WORLD_MAPFILENAME", "WORLD_MAPHEIGHTMAP", "WORLD_MAPGRID", "WORLD_PATHTILEINFO", "STARTMAPX",
            "STARTMAPY", "WORLD_SCRIPT", "WORLD_BUILDINGLIST", "WORLD_GUESTLIST", "WORLD_HEIGHTMAPSCALE"};
    const char* path = "data\\Scenario\\1.pis";
    for (int i = 0; i < 11; ++i) {
        char expected[256], actual[256];
        std::snprintf(expected, sizeof(expected), fatalTexts[i], path);
        world_construct_pis::MissingKeyMessage(actual, sizeof(actual), order[i], path);
        OBL_CHECK_EQ(std::string(actual), std::string(expected));
    }
    char text[256];
    world_construct_pis::WaterAnimMessage(text, sizeof(text), "water.anm");
    OBL_CHECK_EQ(std::string(text), "Couldn't load water animation - water.anm");
    char small[16];
    // The truncation is the point of this case; GCC sees the constant sizes after inlining and warns about it.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
    world_construct_pis::MissingKeyMessage(small, sizeof(small), "WORLD_NAME", path);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    OBL_CHECK_EQ(std::string(small), "no WORLD_NAME i");
}

OBL_TEST_CASE("world_construct_pis.map_name_is_copied_and_bounded")
{
    auto world = DirtyWorld();
    world_construct_pis::CopyMapId(*world, "maps/island1");
    OBL_CHECK_EQ(std::string(world->m_mapId), "maps/island1");
    const std::string longName(400, 'x');
    world_construct_pis::CopyMapId(*world, longName.c_str());
    OBL_CHECK_EQ(std::strlen(world->m_mapId), sizeof(world->m_mapId) - 1);
}

OBL_TEST_CASE("world_construct_pis.texts_scenario_objectives_and_title")
{
    FakeEnv env = FullPis();
    env.strings["WORLD_STATUS_TITLE"] = "t";
    env.strings["WORLD_STATUS_AREA"] = "area";
    env.strings["WORLD_STATUS_WEATHER"] = "weather";
    env.strings["WORLD_STATUS_TEMP"] = "temp";
    env.strings["WORLD_STATUS_HAZARDS"] = "hazards";
    env.strings["SCENARIO_OBJ_DESCRIPTION"] = "desc";
    env.strings["SCENARIO_OBJ_OBJECTIVES"] = "obj";
    env.strings["SCENARIO_OBJ_TITLE"] = "title";
    env.numbers["SCENARIO_OBJ_NUMBER"] = 2.9f; // truncates
    env.strings["SCENARIO_OBJ_0"] = "first";
    env.strings["SCENARIO_OBJ_1"] = "second";
    env.strings["SCENARIO_OBJ_2"] = "not read";
    auto world = DirtyWorld();
    world_construct_pis::ReadTexts(*world, env);
    OBL_CHECK_EQ(std::string(world->m_islandTitle), "t");
    OBL_CHECK_EQ(std::string(world->m_islandHazards), "hazards");
    OBL_CHECK_EQ(std::string(world->m_scenarioDescription), "desc");
    OBL_CHECK_EQ(std::string(world->m_scenarioObjectives), "obj");
    OBL_CHECK_EQ(world->m_numObjectives, 2u);
    OBL_CHECK_EQ(std::string(world->m_objectiveText[0]), "first");
    OBL_CHECK_EQ(std::string(world->m_objectiveText[1]), "second");
    OBL_CHECK(world->m_objectiveText[2].empty());
    OBL_CHECK(world->m_objectiveText[3].empty());
    for (int i = 0; i < 4; ++i) OBL_CHECK_EQ(world->m_objectiveStatus[i], 0u);
    OBL_CHECK_EQ(std::string(world->m_scenarioTitle), "title");
}

OBL_TEST_CASE("world_construct_pis.objectives_stop_at_the_four_slots")
{
    FakeEnv env = FullPis();
    for (const char* key : {"WORLD_STATUS_TITLE", "WORLD_STATUS_AREA", "WORLD_STATUS_WEATHER", "WORLD_STATUS_TEMP", "WORLD_STATUS_HAZARDS",
                 "SCENARIO_OBJ_DESCRIPTION", "SCENARIO_OBJ_OBJECTIVES", "SCENARIO_OBJ_TITLE"})
        env.strings[key] = "x";
    env.numbers["SCENARIO_OBJ_NUMBER"] = 6.0f;
    for (int i = 0; i < 6; ++i) env.strings["SCENARIO_OBJ_" + std::to_string(i)] = "o" + std::to_string(i);
    auto world = DirtyWorld();
    world_construct_pis::ReadTexts(*world, env);
    OBL_CHECK_EQ(world->m_numObjectives, 6u);
    OBL_CHECK_EQ(std::string(world->m_objectiveText[3]), "o3");
}

OBL_TEST_CASE("world_construct_pis.messages_stop_at_the_first_gap")
{
    FakeEnv env;
    env.strings["MESSAGE_0"] = "a";
    env.strings["MESSAGE_1"] = "b";
    env.strings["MESSAGE_3"] = "after the gap";
    world_construct_pis::ReadMessages(env);
    OBL_CHECK_EQ(env.messages.size(), 2u);
    OBL_CHECK_EQ(env.messages[1], "b");
}

OBL_TEST_CASE("world_construct_pis.buildings_fill_the_valid_list_and_clear_the_rest")
{
    FakeEnv env;
    env.strings["BUILDING_0"] = "ab";
    env.strings["BUILDING_1"] = "abcd";
    env.strings["BUILDING_5"] = "beyond the gap";
    auto world = DirtyWorld();
    world_construct_pis::ReadBuildings(*world, env);
    OBL_CHECK_EQ(world->m_numValidBuildings, 2);
    OBL_CHECK_EQ(world->m_validBuildingIDs[0], 20);
    OBL_CHECK_EQ(world->m_validBuildingIDs[1], 40);
    OBL_CHECK_EQ(world->m_validBuildingIDs[2], 0);
    OBL_CHECK_EQ(world->m_validBuildingIDs[0x7f], 0);
}

OBL_TEST_CASE("world_construct_pis.buildings_are_bounded_by_the_array")
{
    FakeEnv env;
    for (int i = 0; i < 0x90; ++i) env.strings["BUILDING_" + std::to_string(i)] = "a";
    auto world = DirtyWorld();
    world_construct_pis::ReadBuildings(*world, env);
    OBL_CHECK_EQ(world->m_numValidBuildings, 0x80);
}

OBL_TEST_CASE("world_construct_pis.load_fraction")
{
    OBL_CHECK_EQ(world_construct_pis::LoadFraction(12345, true), 0.5f);
    OBL_CHECK_EQ(world_construct_pis::LoadFraction(0, false), 1.0f);
    OBL_CHECK_EQ(world_construct_pis::LoadFraction(10000000, false), 0.5f); // clamped
    const float f = world_construct_pis::LoadFraction(50000, false);
    OBL_CHECK(f > 0.82f && f < 0.83f); // 1 - 50000 * 6.873372e-06 * 0.5 = 0.8282
}
