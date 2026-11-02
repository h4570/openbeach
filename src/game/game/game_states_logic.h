#pragma once

#include "game/game/game.h"
#include "game/game/credits.h"
#include "formats/gui_content.h"
#include <span>

#include <cstdint>
#include <cstdio>

namespace obl::game_states {

using game::GameStateId;

inline constexpr float kStepLimit = 0.1f; // largest frame step the timed states count
inline constexpr float kExitSeconds = 15.0f; // ExitGame
inline constexpr float kIntroSeconds = 5.0f; // each Intro phase
inline constexpr float kLoadSeconds = 0.1f; // LoadGame
inline constexpr float kPageSeconds = 20.0f; // a GameWon page
inline constexpr int kFrontEndWidth = 800;
inline constexpr int kFrontEndHeight = 600;
inline constexpr int kGuiModeFrontEnd = 6;
inline constexpr float kWavesVolume = 0.8f;
inline constexpr int kLoopForever = 2; // Sound::Play mode of "waves_loop"
inline constexpr int kLanguagePolish = 5;
// The converted soundtrack's file (media_pack.h kTrackDirectory): the track the playlist also has as night6.mp3. The Env opens it
// through the pack layers.
inline constexpr const char* kNightStream = "night6.mp3";

// 53 bits: the step (clamped to kStepLimit when `clamp`) and the stored timer are added in double, the sum is stored as a float and
// the comparison that follows uses the unrounded sum. NaN never reaches the clamp (an unordered compare fails) and never passes a comparison.
inline double AddStep(float& timer, float step, bool clamp)
{
    if (clamp && step > kStepLimit) step = kStepLimit;
    const double total = static_cast<double>(step) + static_cast<double>(timer);
    timer = static_cast<float>(total);
    return total;
}

// The fields of each state besides the base class's entered flag.
struct ExitGameFields {
    float m_time = 0.0f; // seconds shown; a click sets it to the limit
};
struct LoadGameFields {
    float m_time = 0.0f;
};
struct IntroFields {
    int m_phase = 0; // 0 DeepRedLogo, 1 FrontScreen
    float m_time = 0.0f; // seconds in the phase; a click sets it to the limit
    int m_cursorX = 0; // the cursor position Enter saw (never read)
    int m_cursorY = 0;
};
struct ResetGameFields {};
struct MainMenuFields {
    uint8_t m_showMenu = 1; // until Enter has shown the MainMenu dialog once (after that a return shows the scenario screen)
};
struct GameWonFields {
    float m_time = 0.0f; // seconds on the page; 20 s or a click turns it
    int m_page = 0; // 0..8
};

// ---- GetGuiScript ----
// The GUI layout of each state (gui/layouts.json, gui_layouts.h): the caller picks the layout's file for the language. The exit screen
// and LoadGame have no converted layout (the installation has no exit script; LoadGame builds its GUI from the world).
inline constexpr const char* kIntroLayout = "intro";
inline constexpr const char* kMainMenuLayout = "main_menu";
inline constexpr const char* kResetLayout = "reset_menu";
inline constexpr const char* kGameWonLayout = "game_won";
inline constexpr const char* kInGameLayout = "in_game";

// ---- ExitGame ----

template<class State, class Env> int ExitUpdate(State& state, Env& env)
{
    if (AddStep(state.m_time, env.FrameStep(), true) > kExitSeconds) env.SetNextState(GameStateId::Intro);
    if (env.LeftButton() == 1) state.m_time = kExitSeconds;
    return 0;
}

template<class State, class Env> void ExitEnter(State& state, Env& env)
{
    state.m_entered = 1;
    state.m_time = 0.0f;
    env.ChangeResolution(kFrontEndWidth, kFrontEndHeight);
    env.LoadOwnGuiScript();
    env.SetGuiMode(kGuiModeFrontEnd);
    env.ShowDialog("ExitGame");
}

// ---- Intro ----

template<class State, class Env> int IntroUpdate(State& state, Env& env)
{
    if (AddStep(state.m_time, env.FrameStep(), true) > kIntroSeconds) {
        state.m_time = 0.0f;
        if (state.m_phase == 0) {
            state.m_phase = 1;
            env.ShowDialog("FrontScreen");
        } else if (state.m_phase == 1) {
            env.SetNextState(GameStateId::MainMenu);
        }
    }
    if (env.LeftButton() == 1) state.m_time = kIntroSeconds;
    return 0;
}

template<class State, class Env> void IntroEnter(State& state, Env& env)
{
    state.m_entered = 1;
    state.m_phase = 0;
    state.m_time = 0.0f;
    env.LoadOwnGuiScript();
    env.SetGuiMode(kGuiModeFrontEnd);
    state.m_cursorX = env.CursorX();
    state.m_cursorY = env.CursorY();
    env.SetChannelVolume(env.PlaySound("waves_loop", kLoopForever, 0.0f), kWavesVolume);
    if (state.m_phase == 0)
        env.ShowDialog("DeepRedLogo");
    else if (state.m_phase == 1)
        env.ShowDialog("FrontScreen");
}

// ---- LoadGame ----

template<class State, class Env> int LoadGameUpdate(State& state, Env& env)
{
    // No step limit here: a long first frame hands over at once.
    if (AddStep(state.m_time, env.FrameStep(), false) > kLoadSeconds) env.SetNextState(GameStateId::WorldTest);
    return 0;
}

template<class State> void LoadGameEnter(State& state)
{
    state.m_entered = 1;
    state.m_time = 0.0f;
}

// ---- MainMenu ----

template<class State, class Env> void MainMenuEnter(State& state, Env& env)
{
    state.m_entered = 1;
    env.SetChannelVolume(env.PlaySound("waves_loop", kLoopForever, 0.0f), kWavesVolume);
    env.ChangeResolution(kFrontEndWidth, kFrontEndHeight);
    env.LoadOwnGuiScript();
    env.SetGuiMode(kGuiModeFrontEnd);
    if (state.m_showMenu == 0 && !env.ReturnToMainMenu()) {
        env.LoadScenarioProgress();
        env.ShowDialog("ScenarioScreen");
        return;
    }
    env.ShowDialog("MainMenu");
    state.m_showMenu = 0;
    env.CancelReturnToMainMenu();
}

// ---- ResetGame ----

template<class Env> int ResetUpdate(Env& env)
{
    // rand() % 2 as the compiler wrote it for a signed value: an even draw picks LoadingScreen2.
    env.ShowDialog(env.Rand() % 2 == 0 ? "LoadingScreen2" : "LoadingScreen1");
    env.SetNextState(GameStateId::LoadGame);
    return 0;
}

template<class State, class Env> void ResetEnter(State& state, Env& env)
{
    state.m_entered = 1;
    env.ChangeResolution(kFrontEndWidth, kFrontEndHeight);
    env.LoadOwnGuiScript();
    env.SetGuiMode(kGuiModeFrontEnd);
}

// ---- GameWon ----

// Returns 0 like every state's Update.
template<class State, class Env> int GameWonUpdate(State& state, Env& env)
{
    bool turn = false;
    if (AddStep(state.m_time, env.FrameStep(), true) > kPageSeconds && state.m_page != env.CreditPageCount() - 1) turn = true;
    if (env.LeftButton() == 1 || turn) {
        const int page = state.m_page + 1;
        state.m_time = 0.0f;
        state.m_page = page;
        if (page >= env.CreditPageCount()) {
            env.SetNextState(GameStateId::MainMenu);
            env.RequestReturnToMainMenu();
            return 0;
        }
        env.ShowPage();
    }
    return 0;
}

// Victory credits use imported rows plus our own block. Geometry is computed
// from our front-end canvas, rather than from content-specific page tables.
template<class State, class Env> void GameWonShowPage(State& state, Env& env, std::span<const gui_content::Credit> rows)
{
    env.CloseAllDialogs();
    char name[128];
    // Packs currently provide nine background dialogs; additional pages reuse them.
    std::snprintf(name, sizeof(name), "GameWon%d", state.m_page % 9);
    env.ShowDialog(name);
    env.DestroyPageWidgets();
    env.ClearPages();
    if (state.m_page < 0) return;
    const size_t first = static_cast<size_t>(state.m_page) * game::kCreditsPerPage;
    for (size_t slot = 0; slot < game::kCreditsPerPage && first + slot < rows.size(); ++slot) {
        const auto& row = rows[first + slot];
        const int x = game::kCreditLeft + static_cast<int>(slot / game::kCreditRowsPerColumn) * game::kCreditColumnPitch;
        const int y = game::kCreditTop + static_cast<int>(slot % game::kCreditRowsPerColumn) * game::kCreditLineHeight;
        char key[128];
        std::snprintf(key, sizeof(key), "CREDIT_ENTRY_%d", row.line);
        env.AddText(x, y, x + game::kCreditColumnWidth, y + game::kCreditLineHeight, key, row.font, row.color);
    }
}

template<class State, class Env> void GameWonEnter(State& state, Env& env)
{
    state.m_entered = 1;
    state.m_time = 0.0f;
    state.m_page = 0;
    env.StopAllSamples();
    env.ClearPages();
    env.ChangeResolution(kFrontEndWidth, kFrontEndHeight);
    env.LoadOwnGuiScript();
    env.SetGuiMode(kGuiModeFrontEnd);
    env.PlayStream(kNightStream);
    env.ShowPage();
}

template<class State, class Env> void GameWonExit(State& state, Env& env)
{
    env.CloseStream();
    state.m_entered = 0;
}

} // namespace obl::game_states
