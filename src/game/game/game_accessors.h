#pragma once

#include "game/game/game_context.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/map/path_state.h"
#include "game/map/path_tiles_state.h"
#include "game/objects/object_manager_state.h"
#include "game/tables/commodity_providers.h"
#include "game/ui/cursor_types.h"
#include "game/weather/weather_state.h"
#include "game/world/world_params.h"

namespace obl::game {

// Null before the object exists (e.g. no World outside gameplay).
inline game::Game* GetGame()
{
    return GetContext().game.get();
}
inline obl::world::World* GetWorld()
{
    return GetContext().world;
}
// Subsystem owners register before their construction helpers run; fixtures use the same accessors.
inline ui::Renderer* GetRenderer()
{
    return GetContext().renderer;
}
inline void SetRenderer(ui::Renderer* value)
{
    GetContext().renderer = value;
}
inline ui::Input* GetInput()
{
    return GetContext().input;
}
inline void SetInput(ui::Input* value)
{
    GetContext().input = value;
}
inline ui::SoftwareCursor* GetCursor()
{
    return GetContext().cursor;
}
inline void SetCursor(ui::SoftwareCursor* value)
{
    GetContext().cursor = value;
}
inline uint16_t& GetBlitHalfMask()
{
    return GetContext().blitHalfMask;
}
inline void SetBlitHalfMask(uint16_t value)
{
    GetContext().blitHalfMask = value;
}
inline uint16_t& GetBlitQuarterMask()
{
    return GetContext().blitQuarterMask;
}
inline void SetBlitQuarterMask(uint16_t value)
{
    GetContext().blitQuarterMask = value;
}
inline uint16_t& GetBlitKeylineColor()
{
    return GetContext().blitKeylineColor;
}
inline void SetBlitKeylineColor(uint16_t value)
{
    GetContext().blitKeylineColor = value;
}
inline uint16_t& GetBlitWaterQuarterWhite()
{
    return GetContext().blitWaterQuarterWhite;
}
inline void SetBlitWaterQuarterWhite(uint16_t value)
{
    GetContext().blitWaterQuarterWhite = value;
}
inline uint16_t& GetBlitWaterHalfWhite()
{
    return GetContext().blitWaterHalfWhite;
}
inline void SetBlitWaterHalfWhite(uint16_t value)
{
    GetContext().blitWaterHalfWhite = value;
}
inline uint16_t& GetBlitWaterThreeQuarterWhite()
{
    return GetContext().blitWaterThreeQuarterWhite;
}
inline void SetBlitWaterThreeQuarterWhite(uint16_t value)
{
    GetContext().blitWaterThreeQuarterWhite = value;
}
inline uint16_t& GetPixelRedMask()
{
    return GetContext().pixelRedMask;
}
inline void SetPixelRedMask(uint16_t value)
{
    GetContext().pixelRedMask = value;
}
inline uint16_t& GetPixelGreenMask()
{
    return GetContext().pixelGreenMask;
}
inline void SetPixelGreenMask(uint16_t value)
{
    GetContext().pixelGreenMask = value;
}
inline uint16_t& GetPixelBlueMask()
{
    return GetContext().pixelBlueMask;
}
inline void SetPixelBlueMask(uint16_t value)
{
    GetContext().pixelBlueMask = value;
}
inline uint16_t& GetPixelRedShift()
{
    return GetContext().pixelRedShift;
}
inline void SetPixelRedShift(uint16_t value)
{
    GetContext().pixelRedShift = value;
}
inline uint16_t& GetPixelGreenShift()
{
    return GetContext().pixelGreenShift;
}
inline void SetPixelGreenShift(uint16_t value)
{
    GetContext().pixelGreenShift = value;
}
inline uint8_t& GetPixelGreenMax()
{
    return GetContext().pixelGreenMax;
}
inline void SetPixelGreenMax(uint8_t value)
{
    GetContext().pixelGreenMax = value;
}
inline int& GetBlitPixelFormat()
{
    return GetContext().blitPixelFormat;
}
inline void SetBlitPixelFormat(int value)
{
    GetContext().blitPixelFormat = value;
}
inline void*& GetBlitSceneBuffer()
{
    return GetContext().blitSceneBuffer;
}
inline void SetBlitSceneBuffer(void* value)
{
    GetContext().blitSceneBuffer = value;
}
inline int& GetBlitSceneWidth()
{
    return GetContext().blitSceneWidth;
}
inline void SetBlitSceneWidth(int value)
{
    GetContext().blitSceneWidth = value;
}
inline ui::RendererFade& GetRendererFade()
{
    return GetContext().rendererFade;
}
inline uint8_t GetWindowedMode()
{
    return GetContext().windowedMode;
}
inline void SetWindowedMode(uint8_t value)
{
    GetContext().windowedMode = value;
}
// The Game owns the GUI manager from before its constructor runs (the constructor's helpers already read it).
inline ui::GuiManager* GetGuiManager()
{
    game::Game* game = GetGame();
    return game ? game->m_guiManager.get() : nullptr;
}
inline game::GameClock* GetGameClock()
{
    game::Game* game = GetGame();
    return game ? &game->m_clock : nullptr;
}
inline obl::object::Manager* GetObjectManager()
{
    return GetContext().objectManager;
}
inline void SetObjectManager(obl::object::Manager* value)
{
    GetContext().objectManager = value;
}
// The world's definitions (World::m_worldParams); null outside a world.
inline tables::WorldParams* GetWorldParams()
{
    return world::world_params::Current();
}
inline map::Grid* GetGrid()
{
    return GetContext().grid;
}
inline void SetGrid(map::Grid* value)
{
    GetContext().grid = value;
}
// The world's route search (World::m_path); path::CreatePath and DestroyPath set it through SetPath.
inline path::Path* GetPath()
{
    return GetContext().path;
}
inline void SetPath(path::Path* value)
{
    GetContext().path = value;
}
// The world's path tiles (World::m_pathTiles); the path tiles' GameEnv sets it through SetPathTiles while they are constructed.
inline path::PathTiles* GetPathTiles()
{
    return GetContext().pathTiles;
}
inline void SetPathTiles(path::PathTiles* value)
{
    GetContext().pathTiles = value;
}
// The scripted sound of the scenario (building, opinion and sprite sounds); scripted_sound_runtime sets it in Construct and clears it in Destruct.
inline audio::ScriptedSound* GetScriptedSound()
{
    return GetContext().scriptedSound;
}
inline void SetScriptedSound(audio::ScriptedSound* value)
{
    GetContext().scriptedSound = value;
}
// The scenario's script host ; script_host::Create sets it and script_host::Destroy clears it.
inline world_script_host::WorldScriptHost* GetWorldScriptHost()
{
    return GetContext().worldScriptHost;
}
inline void SetWorldScriptHost(world_script_host::WorldScriptHost* value)
{
    GetContext().worldScriptHost = value;
}
// The served-beer count (the beer manager's record in the save); the building's beer list and the save read and count it.
inline int32_t& ServedBeerCounter()
{
    return GetContext().servedBeerCounter;
}
// The last time the no-path ticker posted its message (the guest's no-path thought); the desire test resets it through this too.
inline float& LastNoPathTickerTime()
{
    return GetContext().lastNoPathTickerTime;
}
// The guest population: the guests the constructor counted, which the destructor undoes; the guest opinions read it.
inline int32_t& GuestPopulation()
{
    return GetContext().guestPopulation;
}
// The guest definition the last guest construction read through the world's parameters.
inline const tables::GuestDef*& GuestConstructDefinition()
{
    return GetContext().guestConstructDefinition;
}
// The builders' whistling counter, one global shared by all builders (the staff counters and the builder script queries).
// The guests per gender and attractiveness band; the constructor counts a guest in one slot, the destructor takes it out.
inline std::array<int32_t, 6>& GuestAttractionCounts()
{
    return GetContext().guestAttractionCounts;
}
// The drift of the lightning bolt being drawn (the lightning-bolt creation's; the bolt's own Env reads and writes it).
inline int32_t& LightningBoltDrift()
{
    return GetContext().lightningBoltDrift;
}
inline int32_t& NumBuildersWhistling()
{
    return GetContext().numBuildersWhistling;
}
inline weather::Weather* GetWeather()
{
    return GetContext().weather;
}
inline void SetWeather(weather::Weather* value)
{
    GetContext().weather = value;
}
inline commodity::Providers* GetCommodityProviders()
{
    const obl::world::World* world = GetWorld();
    return world ? world->m_commodityProviders.get() : nullptr;
}

// The game's main window (the platform's SDL_Window*, opaque here), null before the renderer 16 constructor.
inline void* GetGameWindow()
{
    const ui::Renderer* renderer = GetRenderer();
    return renderer ? renderer->m_window : nullptr;
}

} // namespace obl::game
