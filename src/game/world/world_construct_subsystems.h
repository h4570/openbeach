#pragma once

#include "game/tables/commodity_providers.h"
#include "game/player/player_state.h"
#include "game/weather/weather_state.h"
#include "game/map/grid_state.h"

#include "game/tables/world_params_tables.h"
#include "game/world/world_state.h"

#include <cstdint>

// ConstructWorld step 3: the subsystems the constructor creates, in the order below and grouped in pairs/triples that follow
// each other without other code in between. Every subsystem is `operator_new(size)` +
// its constructor, whose return value is stored in the World (all of them return `this`). The allocation, the constructor
// calls and the few game globals they read stay behind an `Env`, so the order, the sizes and the fields written are unit-tested without the game:
//   void* MapView(mapFile)                             the map view's constructor (owner: the game, g_Renderer, mapFile)
//   void  ClearReuseTempMap(mapView)                   clears the "reuse temp.map" flag (the constructor sets it)
//   bool  LoadWaterAnim(mapView, path)                 the map view's water animation loader; [[noreturn]] WaterAnimFailed(path) = FatalError
//   uint8_t SeaEffects()  /  SetSeaEffects(v)          the sea-effects option of the game, set on the renderer
//   messages::Handler* MessageHandler()                the native message queues (new; no game heap block)
//   object::Manager* ObjectManager()                   the native object lists (new; no game heap block)
//                                                      uint8_t Keyline() (the video option), SetKeyline(objectManager, v)
//   tables::WorldParams* WorldParams(buildingList, guestList)   the native definitions (new; world_params::Current())
//   disaster::Manager* EventManager()                  the native disaster schedule (new; no game heap block)
//   world_script_host::WorldScriptHost* WorldScriptHost()   the native script host (new; no game heap block)
//   construction::Manager* ConstructionManager()       the native building sites and builder table (new; no game heap block)
//   ferry::Manager* FerryManager()                     the native ferry schedule (new; no game heap block)
//   repair::Manager* RepairManager()                   the native repair queues (new; no game heap block)
//   cleaning::Manager* CleaningManager()               the native cleaning queues (new; no game heap block)
//   feedback::Manager* FeedbackManager()               the native message bar (new; no game heap block)
//   map::Grid* Grid(heightmap, heightScale, grid)      the native terrain grid, 128 x 128 tiles of 64 (new; no game heap block)
//   path::PathTiles* PathTiles(pavement, beach)        path::Path* Path(grid): the native path tiles and route facade (new)
//   weather::Weather* Weather() / player::Player* Player()   the native player named "Test Player" (new; no game heap block)
//   void* ScriptedSound(size)  /  LoadSounds(sound)    loads the data file Data/Sounds/Sounds.dat
//   commodity::Providers* CommodityProviders()  /  RebuildProviders()   the native provider lists (new; no game heap block)
// The script host's `LoadScript` is not here: it returns the size in bytes of the script's `.ble` file (see world_construct_pis.h `LoadFraction`).
namespace obl::world_construct_subsystems {

// Allocation sizes, one per subsystem object the constructor allocates.
inline constexpr uint32_t kScriptedSoundSize = 0x578;

// The new Player's cash.
inline constexpr float kStartCash = 25000.0f;
// The grid is always 128 x 128 tiles of 64 units.
inline constexpr uint32_t kGridWidth = 128;
inline constexpr uint32_t kGridHeight = 128;
inline constexpr int kGridTileSize = 64;

// 3a, right after the map name is copied: the map view with its "reuse temp.map" flag cleared (MapView::Construct sets it), then the
// optional water animation: it must load (else `WaterAnimFailed`), and the sea-effects setting of the game options goes to the renderer.
template<class Env> void BuildMapView(world::World& world, Env& env, const char* mapFile, const char* waterAnim)
{
    void* view = env.MapView(mapFile);
    world.m_mapView.reset(static_cast<ui::MapView*>(view));
    env.ClearReuseTempMap(view);
    if (waterAnim) {
        if (!env.LoadWaterAnim(world.m_mapView, waterAnim)) env.WaterAnimFailed(waterAnim);
        env.SetSeaEffects(env.SeaEffects());
    }
}

// 3b, after the scenario texts: the message handler and the object manager, whose keyline flag comes from the video options.
template<class Env> void BuildMessagesAndObjects(world::World& world, Env& env)
{
    world.m_messageHandler.reset(env.MessageHandler());
    world.m_objectManager.reset(env.ObjectManager());
    env.SetKeyline(world.m_objectManager, env.Keyline());
}

// 3c: the building and guest definitions from WORLD_BUILDINGLIST/WORLD_GUESTLIST.
template<class Env> void BuildWorldParams(world::World& world, Env& env, const char* buildingList, const char* guestList)
{
    world.m_worldParams.reset(env.WorldParams(buildingList, guestList));
}

// 3d, the managers, in the order below (the order is visible: each constructor may read globals the previous ones set):
// ferries, events, repairs, cleaning, feedback, script host, construction.
template<class Env> void BuildManagers(world::World& world, Env& env)
{
    world.m_ferryManager.reset(env.FerryManager());
    world.m_eventManager.reset(env.EventManager());
    world.m_repairManager.reset(env.RepairManager());
    world.m_cleaningManager.reset(env.CleaningManager());
    world.m_feedbackManager.reset(env.FeedbackManager());
    world.m_worldScriptHost.reset(env.WorldScriptHost());
    world.m_constructionManager.reset(env.ConstructionManager());
}

// 3e, after BuildTempMap and the depth offsets: the terrain grid, the path tiles (the two tilesets are optional and passed on
// as they are) and the path finder that works on the grid.
template<class Env>
void BuildTerrain(world::World& world, Env& env, const char* heightmap, float heightScale, const char* grid, const char* pavementTileset,
        const char* beachTileset)
{
    world.m_grid.reset(env.Grid(heightmap, heightScale, grid));
    world.m_pathTiles.reset(env.PathTiles(pavementTileset, beachTileset));
    world.m_path.reset(env.Path(world.m_grid));
}

// 3f, after the day clock: the weather; breakdowns and litter are allowed from the start.
template<class Env> void BuildWeather(world::World& world, Env& env)
{
    world.m_weather.reset(env.Weather());
    world.m_breakDownsAllowed = 1;
    world.m_allowLitter = 1;
}

// 3g, after the camera: the player with the start cash; the boat count of the save block is cleared here too.
template<class Env> void BuildPlayer(world::World& world, Env& env)
{
    world.m_player.reset(env.Player());
    world.m_player->m_cash = kStartCash;
    world.m_numBoats = 0;
}

// 3h, after the radar and the progress dialog: the scripted sounds and their data file.
template<class Env> void BuildSound(world::World& world, Env& env)
{
    world.m_scriptedSound.reset(static_cast<audio::ScriptedSound*>(env.ScriptedSound(kScriptedSoundSize)));
    env.LoadSounds(world.m_scriptedSound);
}

// 3i, the last subsystem, after the unbuilt-building count: the commodity providers and their first rebuild.
template<class Env> void BuildCommodityProviders(world::World& world, Env& env)
{
    world.m_commodityProviders.reset(env.CommodityProviders());
    env.RebuildProviders();
}

} // namespace obl::world_construct_subsystems
