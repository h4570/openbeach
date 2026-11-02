#pragma once

#include "game/ui/view_state.h"
#include "game/ui/renderer_types.h"
#include "game/game/game.h"
#include "game/world/world_state.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace obl::messages {
struct Handler;
} // namespace obl::messages

namespace obl::game {
struct GameParameters;
} // namespace obl::game

namespace obl::path {
struct Path;
struct PathTiles;
} // namespace obl::path

namespace obl::disaster {
struct Manager;
} // namespace obl::disaster

namespace obl::script {
struct ScriptCommand;
} // namespace obl::script

namespace obl::tables {
struct GuestDef;
} // namespace obl::tables

namespace obl::audio {
struct ScriptedSound;
} // namespace obl::audio

namespace obl::world_script_host {
class WorldScriptHost;
} // namespace obl::world_script_host

namespace obl::ui {
struct MapView;
struct SpriteBank;
} // namespace obl::ui

namespace obl::game {

// The objects the host reaches through one accessor.
// The World is the first: world::Create sets it while the world is being built (its subsystems read it back) and world::Destroy clears it.
// The owner is the WorldTest state. The Game is owned here: game_api::Create makes it and game_api::Destroy ends it.
struct Context {
    obl::world::World* world = nullptr;
    std::unique_ptr<obl::game::Game> game;
    // Set at the end of the game constructor, so focus messages before then are ignored; cleared by game_api::Destroy.
    bool gameConstructed = false;
    // The window later error boxes belong to (SetErrorWindow; the Game keeps no window handle of its own).
    void* errorWindow = nullptr; // the platform's window handle (SDL_Window* on the windowed host), opaque here
    ui::ViewState view;
    obl::object::Manager* objectManager = nullptr;
    obl::map::Grid* grid = nullptr;
    obl::weather::Weather* weather = nullptr;
    ui::MapView* mapView = nullptr;
    ui::SpriteBank* spriteBank = nullptr;
    obl::audio::Sound* sound = nullptr;
    int spriteStaleCursor = 0; // persistent position of the bank's incremental stale-animation sweep
    // Renderer registration and pixel state are shared by owners and temporary contract fixtures.
    ui::Renderer* renderer = nullptr;
    ui::Input* input = nullptr;
    ui::SoftwareCursor* cursor = nullptr;
    ui::RendererFade rendererFade{};
    bool appActive = false; // WM_ACTIVATEAPP (g_AppActive); nothing reads it yet
    uint8_t windowedMode = 1; // both platform entry points always start windowed
    uint16_t blitHalfMask{};
    uint16_t blitQuarterMask{};
    uint16_t blitKeylineColor{};
    uint16_t blitWaterQuarterWhite{};
    uint16_t blitWaterHalfWhite{};
    uint16_t blitWaterThreeQuarterWhite{};
    uint16_t pixelRedMask{};
    uint16_t pixelGreenMask{};
    uint16_t pixelBlueMask{};
    uint16_t pixelRedShift{};
    uint16_t pixelGreenShift{};
    uint8_t pixelGreenMax{};
    int blitPixelFormat{};
    void* blitSceneBuffer{};
    int blitSceneWidth{};
    // The message handler every post goes through (the message handler's post's g_MessageHandler) and the game's parameter list
    // (g_GameParameters); the same owner rule as above: Create/Destroy and the Game's construction set them.
    obl::messages::Handler* messageHandler = nullptr;
    obl::game::GameParameters* gameParameters = nullptr;
    obl::path::Path* path = nullptr;
    obl::path::PathTiles* pathTiles = nullptr;
    obl::disaster::Manager* eventManager = nullptr;
    obl::audio::LoopSounds* loopSounds = nullptr;
    obl::audio::ScriptedSound* scriptedSound = nullptr;
    obl::world_script_host::WorldScriptHost* worldScriptHost = nullptr;
    // The served-beer count (the beer manager's save record; g_ServedBeerCounter), the last time the no-path ticker posted
    // (g_LastNoPathTickerTime), and the If stacks of the object and world script parsers (g_ScriptProgramIfStack,
    // g_WorldScriptIfStack). Only their own files read them; the stacks are cleared by each parser's construct.
    int32_t servedBeerCounter = 0;
    float lastNoPathTickerTime = 0;
    std::array<obl::script::ScriptCommand*, 10> objectScriptIfStack{};
    uint8_t objectScriptIfDepth = 0;
    std::array<void*, 10> worldScriptIfStack{};
    // GUI state the image's .data held: the rotating index of each staff icon button that the staff-by-class selection advances
    // (g_StaffSelectIndex, -1 = nobody selected yet) and the POWER_USE bar's over-capacity blink (g_PowerBarBlinkActive/Next/
    // Visible). Only gui_actions and gui_variable_image read them; like the image, nothing resets them between scenarios.
    std::array<uint32_t, 6> staffSelectIndex{0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    // The filter id of the table whose filter cell is open (g_GuiTableActiveFilter, zero BSS); gui_tables writes it, the
    // filter heading variable reads it.
    uint32_t guiTableActiveFilter = 0;
    bool powerBarBlinkActive = false;
    float powerBarBlinkNext = 0;
    bool powerBarBlinkVisible = true;
    // The guest population (g_GuestPopulation: the guests counted by the constructor, undone by the destructor), the definition the
    // last guest construction read (g_GuestConstructDefinition) and the builders whistling at once (g_NumBuildersWhistling, shared
    // by all builders). Only their own files read them.
    int32_t guestPopulation = 0;
    const obl::tables::GuestDef* guestConstructDefinition = nullptr;
    int32_t numBuildersWhistling = 0;
    // The staff counters the staff code keeps per profession (g_StaffBuilderCount, g_StaffCleanerCount, g_StaffMechanicCount,
    // g_StaffLifeguardCount, g_StaffRepCount, g_StaffGuardCount). The save record writes them under their names.
    int32_t staffBuilderCount = 0;
    int32_t staffCleanerCount = 0;
    int32_t staffMechanicCount = 0;
    int32_t staffLifeguardCount = 0;
    int32_t staffRepCount = 0;
    int32_t staffGuardCount = 0;
    // The guests per gender and attractiveness band (g_GuestAttractionCounts[6]), kept by the guest constructor and destructor.
    std::array<int32_t, 6> guestAttractionCounts{};
    // The 16 step offsets of the building queues (g_QueueStepDirections), written by each queue's construction.
    std::array<float, 16> queueStepDirections{};
    // The random drift of the lightning bolt being drawn (g_LightningBoltDrift), kept within weather_strike.h's kDriftLimit.
    int32_t lightningBoltDrift = 0;
    // The height map name the grid was last loaded from (g_HeightMapPathCopy).
    std::string heightMapPathCopy;
};

inline Context& GetContext()
{
    static Context context;
    return context;
}

} // namespace obl::game
