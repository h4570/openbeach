#include "dirty_world.h"
#include "unit_test.h"

#include "game/map/grid_state.h"
#include "game/tables/commodity_providers.h"
#include "game/objects/object_manager_state.h"
#include "game/map/path_state.h"
#include "game/map/path_tiles_state.h"
#include "game/cleaning/cleaning_state.h"
#include "game/construction/construction_state.h"
#include "game/feedback/feedback_state.h"
#include "game/events/event_state.h"
#include "game/ferry/ferry_state.h"
#include "game/messages/message_state.h"
#include "game/repair/repair_state.h"
#include "game/world/world_construct_subsystems.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_construct_subsystems;
using world_data::World;

namespace {

struct WaterFailure {};

// Hands out distinct dirty blocks and writes down every call, so the tests see the order, the sizes and the arguments.
struct FakeEnv {
    std::vector<std::string> calls;
    std::vector<std::unique_ptr<uint8_t[]>> blocks;
    obl::feedback::Manager feedback;
    std::unique_ptr<obl::player::Player> player;
    int scriptHost = 0; // stands in for the native script host (only its address is checked)
    obl::construction::Manager construction;
    obl::object::Manager objects;
    std::unique_ptr<obl::tables::WorldParams> params = std::make_unique<obl::tables::WorldParams>();
    obl::map::Grid terrain;
    obl::path::Path route;
    obl::path::PathTiles tiles;
    obl::ferry::Manager ferry;
    obl::disaster::Manager events;
    obl::messages::Handler messages;
    obl::repair::Manager repair;
    obl::cleaning::Manager cleaning;
    obl::commodity::Providers providers;
    bool waterLoads = true;
    void* reuseCleared = nullptr;

    void* Block(const char* name, uint32_t size)
    {
        calls.push_back(std::string(name) + ":" + std::to_string(size));
        blocks.push_back(std::make_unique<uint8_t[]>(size > 0x1000 ? size : 0x1000));
        std::memset(blocks.back().get(), 0xA5, size);
        return blocks.back().get();
    }
    void Note(const std::string& text) { calls.push_back(text); }

    void* MapView(const char* mapFile)
    {
        Note(std::string("map=") + mapFile);
        return Block("MapView", 0x164);
    }
    void ClearReuseTempMap(void* view)
    {
        Note("reuse=0");
        reuseCleared = view;
    }
    bool LoadWaterAnim(void*, const char* path)
    {
        Note(std::string("water=") + path);
        return waterLoads;
    }
    [[noreturn]] void WaterAnimFailed(const char* path)
    {
        Note(std::string("fatal=") + path);
        throw WaterFailure{};
    }
    uint8_t SeaEffects() { return 1; }
    void SetSeaEffects(uint8_t value) { Note("sea=" + std::to_string(value)); }
    obl::messages::Handler* MessageHandler()
    {
        Note("MessageHandler:created");
        return &messages;
    }
    obl::object::Manager* ObjectManager()
    {
        Note("ObjectManager:created");
        return &objects;
    }
    uint8_t Keyline() { return 1; }
    void SetKeyline(obl::object::Manager* manager, uint8_t value)
    {
        Note(std::string("keyline=") + std::to_string(value) + (manager == &objects ? "@om" : "@?"));
    }
    obl::tables::WorldParams* WorldParams(const char* b, const char* g)
    {
        Note(std::string("lists=") + b + "," + g);
        Note("WorldParams:created");
        return params.get();
    }
    obl::ferry::Manager* FerryManager()
    {
        Note("FerryManager:created");
        return &ferry;
    }
    obl::disaster::Manager* EventManager()
    {
        Note("EventManager:created");
        return &events;
    }
    obl::repair::Manager* RepairManager()
    {
        Note("RepairManager:created");
        return &repair;
    }
    obl::cleaning::Manager* CleaningManager()
    {
        Note("CleaningManager:created");
        return &cleaning;
    }
    obl::feedback::Manager* FeedbackManager()
    {
        Note("FeedbackManager:created");
        return &feedback;
    }
    obl::world_script_host::WorldScriptHost* WorldScriptHost()
    {
        Note("WorldScriptHost:created");
        return reinterpret_cast<obl::world_script_host::WorldScriptHost*>(&scriptHost);
    }
    obl::construction::Manager* ConstructionManager()
    {
        Note("ConstructionManager:created");
        return &construction;
    }
    obl::map::Grid* Grid(const char* heightmap, float scale, const char* grid)
    {
        Note(std::string("grid=") + heightmap + "," + std::to_string(scale) + "," + grid);
        Note("Grid:created");
        return &terrain;
    }
    obl::path::PathTiles* PathTiles(const char* pavement, const char* beach)
    {
        Note(std::string("tiles=") + (pavement ? pavement : "-") + "," + (beach ? beach : "-"));
        Note("PathTiles:created");
        return &tiles;
    }
    obl::path::Path* Path(obl::map::Grid* grid)
    {
        Note("Path:created");
        Note(grid == &terrain ? "path-on-grid" : "path-on-?");
        return &route;
    }
    weather::Weather* Weather() { return static_cast<weather::Weather*>(Block("Weather", 1)); }
    obl::player::Player* Player()
    {
        Note("Player:created");
        player = std::make_unique<obl::player::Player>();
        return player.get();
    }
    void* ScriptedSound(uint32_t size) { return Block("ScriptedSound", size); }
    void LoadSounds(void* sound) { Note(sound == blocks.back().get() ? "sounds-loaded" : "sounds-?"); }
    obl::commodity::Providers* CommodityProviders()
    {
        Note("CommodityProviders:created");
        return &providers;
    }
    void RebuildProviders() { Note("rebuild"); }
};

std::unique_ptr<World> DirtyWorld()
{
    auto world = std::make_unique<World>();
    DirtyGroups(*world);
    return world;
}

} // namespace

OBL_TEST_CASE("world_construct_subsystems.map_view_clears_reuse_flag_and_loads_water")
{
    auto world = DirtyWorld();
    FakeEnv env;
    BuildMapView(*world, env, "island", "water.anm");
    OBL_CHECK_EQ(env.calls[0], std::string("map=island"));
    OBL_CHECK_EQ(env.calls[1], std::string("MapView:356"));
    OBL_CHECK_EQ(env.calls[2], std::string("reuse=0"));
    OBL_CHECK_EQ(env.calls[3], std::string("water=water.anm"));
    OBL_CHECK_EQ(env.calls[4], std::string("sea=1"));
    OBL_CHECK_EQ(env.calls.size(), 5u);
    OBL_CHECK(env.reuseCleared == world->m_mapView.get());
}

OBL_TEST_CASE("world_construct_subsystems.map_view_without_water_skips_the_sea_effects")
{
    auto world = DirtyWorld();
    FakeEnv env;
    BuildMapView(*world, env, "island", nullptr);
    OBL_CHECK_EQ(env.calls.size(), 3u);
}

OBL_TEST_CASE("world_construct_subsystems.water_failure_ends_before_the_sea_effects")
{
    auto world = DirtyWorld();
    FakeEnv env;
    env.waterLoads = false;
    bool thrown = false;
    try {
        BuildMapView(*world, env, "island", "bad.anm");
    } catch (const WaterFailure&) {
        thrown = true;
    }
    OBL_CHECK(thrown);
    OBL_CHECK_EQ(env.calls.back(), std::string("fatal=bad.anm"));
}

OBL_TEST_CASE("world_construct_subsystems.pairs_store_each_pointer_in_its_own_field_in_dependency_order")
{
    auto world = DirtyWorld();
    FakeEnv env;
    BuildMessagesAndObjects(*world, env);
    BuildWorldParams(*world, env, "b.ble", "g.ble");
    BuildManagers(*world, env);
    const std::vector<std::string> expected = {"MessageHandler:created", "ObjectManager:created", "keyline=1@om", "lists=b.ble,g.ble",
            "WorldParams:created", "FerryManager:created", "EventManager:created", "RepairManager:created", "CleaningManager:created",
            "FeedbackManager:created", "WorldScriptHost:created", "ConstructionManager:created"};
    OBL_CHECK_EQ(env.calls.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < env.calls.size(); ++i) OBL_CHECK_EQ(env.calls[i], expected[i]);
    OBL_CHECK_EQ(world->m_messageHandler, &env.messages);
    OBL_CHECK_EQ(world->m_objectManager, &env.objects);
    OBL_CHECK_EQ(world->m_worldParams, env.params.get());
    OBL_CHECK_EQ(world->m_ferryManager, &env.ferry);
    OBL_CHECK_EQ(world->m_eventManager, &env.events);
    OBL_CHECK_EQ(world->m_repairManager, &env.repair);
    OBL_CHECK_EQ(world->m_cleaningManager, &env.cleaning);
    OBL_CHECK_EQ(world->m_feedbackManager, &env.feedback);
    OBL_CHECK_EQ(static_cast<void*>(world->m_worldScriptHost), static_cast<void*>(&env.scriptHost));
    OBL_CHECK_EQ(world->m_constructionManager, &env.construction);
}

OBL_TEST_CASE("world_construct_subsystems.terrain_builds_grid_tiles_and_path_on_the_grid")
{
    auto world = DirtyWorld();
    FakeEnv env;
    BuildTerrain(*world, env, "h.bmp", 2.5f, "g.grid", nullptr, "beach");
    OBL_CHECK_EQ(env.calls[1], std::string("Grid:created"));
    OBL_CHECK_EQ(env.calls[2], std::string("tiles=-,beach"));
    OBL_CHECK_EQ(env.calls[3], std::string("PathTiles:created"));
    OBL_CHECK_EQ(env.calls[4], std::string("Path:created"));
    OBL_CHECK_EQ(env.calls[5], std::string("path-on-grid"));
    OBL_CHECK_EQ(world->m_grid, &env.terrain);
    OBL_CHECK_EQ(world->m_pathTiles, &env.tiles);
    OBL_CHECK_EQ(world->m_path, &env.route);
}

OBL_TEST_CASE("world_construct_subsystems.weather_player_sound_and_providers_set_their_side_fields")
{
    auto world = DirtyWorld();
    FakeEnv env;
    BuildWeather(*world, env);
    OBL_CHECK_EQ(world->m_breakDownsAllowed, 1);
    OBL_CHECK_EQ(world->m_allowLitter, 1);
    OBL_CHECK_EQ(static_cast<void*>(world->m_weather), static_cast<void*>(env.blocks[0].get()));

    world->m_numBoats = 7;
    BuildPlayer(*world, env);
    OBL_CHECK_EQ(world->m_player->m_cash, 25000.0f);
    OBL_CHECK_EQ(world->m_numBoats, 0);

    BuildSound(*world, env);
    OBL_CHECK_EQ(env.calls.back(), std::string("sounds-loaded"));
    OBL_CHECK_EQ(world->m_scriptedSound, static_cast<void*>(env.blocks[1].get()));

    BuildCommodityProviders(*world, env);
    OBL_CHECK_EQ(env.calls[env.calls.size() - 2], std::string("CommodityProviders:created"));
    OBL_CHECK_EQ(env.calls.back(), std::string("rebuild"));
    OBL_CHECK_EQ(world->m_commodityProviders, &env.providers);
}
