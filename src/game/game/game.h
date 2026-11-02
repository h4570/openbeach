#pragma once

#include "game/ui/sprite.h"
#include "game/game/game_state_base.h"
#include "game/ui/save_slots.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// The top-level Game object (owned by the game context, the game constructor) and the subsystems it owns. Names are
// ours (the game never serializes these objects); docs/architecture/game.md.
namespace obl::ui {
struct GuiManager; // game/ui/gui_manager_types.h
}
namespace obl::text {
struct Table; // game/tables/text_table.h
}
namespace obl::audio {
struct Sound; // game/ui/sound_types.h
struct LoopSounds; // game/ui/loop_sound_types.h
} // namespace obl::audio

namespace obl::ui {
struct Renderer; // game/ui/renderer_types.h
struct Input;
struct SoftwareCursor;
} // namespace obl::ui

namespace obl::game {

struct Settings; // settings_store.h

// A subsystem the Game owns: the game constructor sets the destroy function with the object (the destructor plus the
// heap it came from), so this header needs neither the type nor the game. A plain function-pointer deleter would make
// the Game not default-constructible (layout_test builds one).
template<class T> struct MemberDeleter {
    void (*destroy)(T*) = nullptr;
    void operator()(T* object) const { destroy(object); }
};
template<class T> using Member = std::unique_ptr<T, MemberDeleter<T>>;

struct GameClock {
    uint32_t m_startTick; // GetTickCount at construction
    int m_fps; // frames of the last whole second
    int m_frames; // The game clock update calls
    int m_framesAtSecond; // m_frames when m_fps was last taken
    float m_time; // seconds
    float m_previousTime;
    float m_deltaTime; // frame delta in seconds, read by the world update, GuiManager::Update and Sound::Update
    float m_lastSecond; // whole seconds counted so far (the FPS sample point)
    int m_renderMs; // WinMain: ProfileTimer ticks of the render / 1000
    int m_updateMs; // WinMain: the same for the update
};

// ProfileTimer (Game::m_profileTimer, the profile timer constructor): rdtsc stopwatch around WinMain's update
// and render. The constructor calibrates the cost of one Start/Stop pair, which Stop subtracts.
struct ProfileTimer {
    uint64_t m_start; // rdtsc at Start
    int64_t m_elapsed; // rdtsc at Stop - m_start - m_overhead
    int32_t m_overhead; // average of ten Start/Stop pairs
};

// Order of Game::m_states; the state switch takes these indices.
enum class GameStateId : int { WorldTest, MainMenu, Intro, ExitGame, ResetGame, LoadGame, GameWon, Count };

struct Game {
    GameState* m_currentState;
    GameState* m_nextState;
    std::array<std::unique_ptr<GameState>, static_cast<size_t>(GameStateId::Count)> m_states;
    Member<ui::SoftwareCursor> m_cursor; // GetCursor
    Member<ui::Input> m_input; // GetInput
    GameClock m_clock; // GetGameClock
    ProfileTimer m_profileTimer; // times WinMain's update and render
    Member<ui::Renderer> m_renderer; // GetRenderer
    Member<void> m_debugFont; // GdiFont
    Member<text::Table> m_textTable; // the selected language's table (ui::text_table::Create/Destroy)
    Member<ui::GuiManager> m_guiManager; // GetGuiManager; the gui manager constructor also registers it in g_GuiManager
    Member<audio::Sound> m_sound; // registered in Context (CurrentSound)
    Member<audio::LoopSounds> m_loopSounds; // The loop sounds constructor, registered in game::Context::loopSounds (CurrentLoopSounds)
    Member<Settings> m_settings; // settings_store::Create; the game destructor saves it first
    uint8_t m_guiReloadPending; // deferred GUI reload after a resolution change, served by the game render
    Member<ui::SpriteBank> m_spriteBank; // registered in g_SpriteBank (GetSpriteBank)
    std::vector<std::u16string> m_scenarios;
    int m_scenarioProgress[12]; // sp.bin: 1 locked, 2 unlocked, 4 always available
    int m_lastUnlockedScenario;
    uint8_t m_sandbox;
    std::vector<std::unique_ptr<save_slots::Slot>> m_saveSlots; // headers of savegames\save*.dat, newest first
    int m_loadSlot; // index in m_saveSlots to load from the menu, -1 none
    uint8_t m_loadListFilled;
    const save_slots::Slot* m_selectedSaveSlot; // record selected in LoadInfoBox
    uint8_t m_returnToMainMenu; // after GameWon go to the main menu instead of ScenarioScreen
    uint8_t m_suspended; // The deactivate handler/OnActivate: window lost focus, sound muted and world paused
};

// Index of the current state in m_states, -1 if it is none of them.
inline int CurrentStateIndex(const Game& game)
{
    for (int i = 0; i < static_cast<int>(GameStateId::Count); ++i)
        if (game.m_states[i].get() == game.m_currentState) return i;
    return -1;
}

} // namespace obl::game
