#include "core/char16.h"
#include "game/game/startup_tables.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/scripted_sound_runtime.h"
#include "game/world/world_script.h"
#include "game/game/diagnostics.h"
#include "game/ui/renderer_api.h"
#include "core/pack_path.h"
#include "game/game/settings_store.h"
#include "game/player/player_state.h"
#include "game/weather/weather.h"
#include "game/world/world_factory.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/object_manager_state.h"
#include "game/feedback/feedback_manager.h"
#include "game/events/event_manager.h"
#include "game/construction/construction_manager.h"
#include "game/messages/message_handler.h"
#include "game/ferry/ferry.h"
#include "game/repair/repair_manager.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/objects/commodity_providers.h"
#include "game/map/grid_lifecycle.h"
#include "game/map/path_queries.h"
#include "game/world/world_params.h"

#include "game/map/grid_state.h"
#include "game/game/game_accessors.h"
#include "game/world/world_script_host_game.h"
#include "game/game/game.h"
#include "game/ui/map_view.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/call_counter.h"
#include "game/world/scenario_load.h"
#include "game/world/world_construct.h"
#include "game/world/world_construct_pis.h"
#include "game/world/world_construct_scenario.h"
#include "game/world/world_construct_subsystems.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "game/ui/map_view_blocks.h"
#include "game/game/game_api.h"

namespace obl::world {
namespace {

using layout::World;
namespace fields = world_construct;
namespace pis = world_construct_pis;
namespace subsystems = world_construct_subsystems;
namespace world_construct_scenario = obl::world_construct_scenario;
namespace scenario_load = obl::scenario_load;

// Worlds built (`call-report world.construct`).
test::CallCounter g_constructCalls("world.construct");

// The installed-data constant the scenario does not carry: the building list the scenario header names. The
// engine reads the catalog from the pack and no longer opens it (the world params ignore it).
constexpr const char* kBuildingList = "data/Script/BuildingList.pis";

[[noreturn]] void Fatal(const char* message)
{
    game::FatalError("%s", message);
    std::abort(); // FatalError exits the process
}

// The game side of the Envs of src/common/world_construct*.h.
class Env {
public:
    explicit Env(World& world) : m_world(world) {}

    // ---- world_construct_scenario.h
    // A scenario without one of the texts gets an empty text.
    std::string Duplicate(const char* text) { return text ? text : ""; }
    int BuildingId(const char* name) { return world_params::GetBuildingId(game::GetWorldParams(), name); }
    void AddScenarioMessage(const char* text) { feedback::Feedback(m_world.m_feedbackManager).AddScenarioMessage(text); }

    // ---- world_construct_subsystems.h
    void* MapView(const char* mapFile)
    {
        return (new ui::MapView)->Construct(game::GetGame()->m_debugFont.get(), game::GetRenderer(), mapFile);
    }
    void ClearReuseTempMap(void* mapView) { static_cast<ui::MapView*>(mapView)->m_reuseTempMap = 0; }
    bool LoadWaterAnim(void* mapView, const char* path) { return (static_cast<ui::MapView*>(mapView))->LoadWaterAnim(path) != 0; }
    [[noreturn]] void WaterAnimFailed(const char* path)
    {
        char message[256];
        pis::WaterAnimMessage(message, sizeof(message), path);
        Fatal(message);
    }
    uint8_t SeaEffects() { return game::settings_store::Current().m_seaEffects; }
    void SetSeaEffects(uint8_t value) { obl::ui::renderer::SetSeaEffects(game::GetRenderer(), value); }
    messages::Handler* MessageHandler() { return messages::message_facade::Create(); }
    object::Manager* ObjectManager() { return object::CreateManager(); }
    uint8_t Keyline() { return game::settings_store::Current().m_keyline; }
    void SetKeyline(object::Manager* manager, uint8_t value) { object::object_manager::SetKeyline(manager, value); }
    tables::WorldParams* WorldParams(const char*, const char* guestList) { return world_params::Construct(guestList); }
    ferry::Manager* FerryManager() { return ferry::ferry_facade::Create(); }
    disaster::Manager* EventManager() { return disaster::event_facade::Create(); }
    repair::Manager* RepairManager() { return repair::repair_facade::Create(); }
    cleaning::Manager* CleaningManager() { return cleaning::cleaning_facade::Create(); }
    feedback::Manager* FeedbackManager() { return feedback::Feedback::Create(); }
    world_script_host::WorldScriptHost* WorldScriptHost() { return world_script_host::WorldScriptHost::Create(); }
    construction::Manager* ConstructionManager() { return construction::runtime::Create(); }
    map::Grid* Grid(const char* heightmap, float heightScale, const char* grid)
    {
        return map::CreateGrid(subsystems::kGridWidth, subsystems::kGridHeight, subsystems::kGridTileSize, heightmap, heightScale, grid);
    }
    path::PathTiles* PathTiles(const char* pavement, const char* beach) { return path::CreatePathTiles(pavement, beach); }
    path::Path* Path(map::Grid* grid) { return path::CreatePath(grid); }
    weather::Weather* Weather() { return weather::runtime::Create(); }
    player::Player* Player() { return new player::Player(u"Test Player"); }
    audio::ScriptedSound* ScriptedSound(uint32_t)
    {
        return static_cast<audio::ScriptedSound*>(::operator new(sizeof(audio::ScriptedSound)))->Construct();
    }
    void LoadSounds(void* sound) { static_cast<audio::ScriptedSound*>(sound)->Load(""); }
    commodity::Providers* CommodityProviders() { return object::commodity_providers::Create(); }
    void RebuildProviders() { object::commodity_providers::Rebuild(m_world.m_commodityProviders); }

    // ---- world_construct.h (CountUnbuilt)
    int BuildingCount() { return static_cast<int>(m_world.m_worldParams->m_numBuildings); }
    int Category(int id) { return world_params::GetBuildingDef(m_world.m_worldParams, id)->m_Category; }
    int ObjectsOf(int id)
    {
        const char* key = world_params::GetBuildingKey(m_world.m_worldParams, id);
        return object::object_manager::CountByName(m_world.m_objectManager, key);
    }

private:
    World& m_world;
};

// The order below matters: constructors read globals earlier ones set (g_WorldParams, g_MapView, g_World itself), and the
// scenario script started near the end creates objects through every subsystem. The header values come from the loaded scenario (scenario_load.h).
World* ConstructWorld(World& world, ui::Cursor* cursor, ui::Input* input, const char* scenarioId, bool loadingSave)
{
    game::GetContext().world = &world;
    fields::InitFields(world, game::GetContext().view, cursor, input, loadingSave, game::GameSpeedTable()[0]);

    scenario_load::Setup setup;
    std::string error;
    if (!scenario_load::LoadPack(pack_path::Selected(), scenarioId, setup, error)) {
        const std::string message = "Scenario: " + error + ". Import the game data again";
        Fatal(message.c_str());
    }
    Env env(world);
    pis::CopyMapId(world, setup.mapId.c_str());
    subsystems::BuildMapView(world, env, setup.mapFile.c_str(), setup.waterAnim.c_str());
    world_construct_scenario::ApplyTexts(world, setup, env);
    subsystems::BuildMessagesAndObjects(world, env);
    subsystems::BuildWorldParams(world, env, kBuildingList, setup.guestSet.c_str());
    subsystems::BuildManagers(world, env);
    world_construct_scenario::ApplyMessages(setup, env);
    world_construct_scenario::ApplyBuildings(world, setup, env);

    // WorldScriptHost::LoadScript leaves the size of the script's .ble file as its result; it sets where the loading bar starts.
    const int scriptSize = static_cast<int>(world.m_worldScriptHost->Load(setup.script.c_str()));
    const float fraction = pis::LoadFraction(scriptSize, world.m_loadingSave != 0);
    (world.m_mapView)->BuildTempMap(0x20, 0x20, 0x80, fraction);
    (world.m_mapView)->SetDepthPlaneOffset(setup.depthPlane);
    (world.m_mapView)->SetDepthSlantOffset(setup.depthSlant);

    subsystems::BuildTerrain(world, env, setup.heightMap.c_str(), setup.heightScale, setup.grid.c_str(),
            world_construct_scenario::Text(setup.pavement), world_construct_scenario::Text(setup.beachEdge));

    int dayLength = 0;
    game::game_parameters::GetInt(game::game_parameters::Current(), &dayLength, "iDAY_LENGTH_IN_SECONDS");
    uint32_t frameStat;
    std::memcpy(&frameStat, &game::GetGameClock()->m_time, sizeof(frameStat));
    fields::InitDayClock(world, dayLength, frameStat);

    subsystems::BuildWeather(world, env);
    (world.m_mapView)->SetCameraPos(setup.startX, setup.startY);
    subsystems::BuildPlayer(world, env);

    // An absent radar is a null image at (0, 0): the result for a scenario without the keys.
    ui::GuiManager* const gui = game::GetGuiManager();
    const char* radarImage = setup.radar ? setup.radar->image.c_str() : nullptr;
    const int radarPosX = setup.radar ? setup.radar->posX : 0;
    const int radarPosY = setup.radar ? setup.radar->posY : 0;
    gui->CreateRadar(radarPosX, radarPosY, radarImage);
    gui->SetProgressDialog(world_construct_scenario::Text(setup.progressDialog));
    subsystems::BuildSound(world, env);

    world.m_worldScript = world.m_worldScriptHost->WorldScript();
    if (world.m_worldScript) {
        if (world.m_loadingSave == 0)
            WorldScriptStartNewGame(*world.m_worldScript, 1.0f - fraction);
        else
            WorldScriptStartLoadedGame(*world.m_worldScript);
    }

    fields::CountUnbuilt(world, env);
    subsystems::BuildCommodityProviders(world, env);
    fields::FinishFields(world, game::GetContext().view, game::GetRenderer()->m_zoomLevel);
    return &world;
}

} // namespace

World* Create(ui::Cursor* cursor, ui::Input* input, const char* scenarioId, bool loadingSave)
{
    g_constructCalls.Count();
    return ConstructWorld(*new World(), cursor, input, scenarioId, loadingSave);
}

} // namespace obl::world
