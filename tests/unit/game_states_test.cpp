#include "unit_test.h"
#include "game/game/game_states.h"

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <ostream>
#include <string>
#include <vector>

using namespace obl::game_states;
using obl::game::GameStateId;

namespace {
// A distinct type so the failure output can print it.
struct Log : std::vector<std::string> {
    Log() = default;
    Log(std::initializer_list<std::string> lines) : std::vector<std::string>(lines) {}
    Log(std::vector<std::string> lines) : std::vector<std::string>(std::move(lines)) {}
};
std::ostream& operator<<(std::ostream& out, const Log& log)
{
    for (const auto& line : log) out << '[' << line << ']';
    return out << "(" << log.size() << ")";
}

// A state as the functions see it: the state's fields plus the base class's entered flag.
template<class Fields> struct Probe : Fields {
    uint8_t m_entered = 0;
};

// Records every effect as text; queries answer from public fields.
struct Env {
    Log log;
    int creditPages = 9;
    int CreditPageCount() { return creditPages; }
    float step = 0.02f;
    int button = 0, draw = 0, cursorX = 11, cursorY = 22, channel = 7;
    bool returnToMenu = false;

    float FrameStep() { return step; }
    int LeftButton() { return button; }
    void SetNextState(GameStateId id) { log.push_back("next " + std::to_string(static_cast<int>(id))); }
    void ShowDialog(const char* name) { log.push_back(std::string("dialog ") + name); }
    void ChangeResolution(int w, int h) { log.push_back("res " + std::to_string(w) + "x" + std::to_string(h)); }
    void LoadOwnGuiScript() { log.push_back("script"); }
    void SetGuiMode(int mode) { log.push_back("mode " + std::to_string(mode)); }
    int PlaySound(const char* name, int mode, float fade)
    {
        log.push_back(std::string("play ") + name + " " + std::to_string(mode) + " " + std::to_string(fade));
        return channel;
    }
    void SetChannelVolume(int c, float v) { log.push_back("volume " + std::to_string(c) + " " + std::to_string(v)); }
    int Rand() { return draw; }
    int CursorX() { return cursorX; }
    int CursorY() { return cursorY; }
    bool ReturnToMainMenu() { return returnToMenu; }
    void RequestReturnToMainMenu() { log.push_back("return set"); }
    void CancelReturnToMainMenu() { log.push_back("return clear"); }
    void LoadScenarioProgress() { log.push_back("progress"); }
    void StopAllSamples() { log.push_back("stop samples"); }
    void PlayStream(const char* path) { log.push_back(std::string("stream ") + path); }
    void CloseStream() { log.push_back("stream close"); }
    void ClearPages() { log.push_back("clear pages"); }
    void ShowPage() { log.push_back("page"); }
    void CloseAllDialogs() { log.push_back("close dialogs"); }
    void DestroyPageWidgets() { log.push_back("destroy widgets"); }
    void AddText(int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color)
    {
        log.push_back("text " + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1) + " " +
                key + " f" + std::to_string(font) + " c" + std::to_string(color));
    }
};
} // namespace

OBL_TEST_CASE("game_states.add_step_clamps_the_step_and_compares_the_unrounded_sum")
{
    float timer = 14.95f;
    OBL_CHECK_EQ(AddStep(timer, 5.0f, true) > 15.0, true); // a 5 s frame counts as 0.1 s: 15.05
    OBL_CHECK_EQ(timer, 15.05f);
    timer = 0.0f;
    OBL_CHECK_EQ(AddStep(timer, 5.0f, false), 5.0); // LoadGame has no limit
    timer = 1.0f;
    OBL_CHECK_EQ(AddStep(timer, 0.05f, true), static_cast<double>(0.05f) + 1.0);
    timer = 1.0f;
    AddStep(timer, std::nanf(""), true); // NaN passes the clamp and fails every comparison
    OBL_CHECK_EQ(timer != timer, true);
    OBL_CHECK_EQ(AddStep(timer, 0.0f, true) > 0.0, false);
}

OBL_TEST_CASE("game_states.exit_counts_to_fifteen_seconds_then_returns_to_the_intro")
{
    Probe<ExitGameFields> state{};
    Env env;
    env.step = 0.1f;
    state.m_time = 14.85f;
    OBL_CHECK_EQ(ExitUpdate(state, env), 0);
    OBL_CHECK_EQ(env.log, Log{}); // 14.95
    ExitUpdate(state, env);
    OBL_CHECK_EQ(env.log, Log{"next 2"}); // 15.05 > 15
    env.log.clear();
    env.button = 1;
    state.m_time = 3.0f;
    ExitUpdate(state, env);
    OBL_CHECK_EQ(state.m_time, 15.0f); // a click jumps to the limit, the next frame leaves
    OBL_CHECK_EQ(env.log, Log{});
    env.button = 2; // only the value 1 is a fresh press
    state.m_time = 3.0f;
    ExitUpdate(state, env);
    OBL_CHECK_EQ(state.m_time > 3.0f && state.m_time < 3.2f, true);
}

OBL_TEST_CASE("game_states.exit_enter_opens_the_dialog_in_800x600")
{
    Probe<ExitGameFields> state{};
    state.m_time = 9.0f;
    Env env;
    ExitEnter(state, env);
    OBL_CHECK_EQ(state.m_entered, 1);
    OBL_CHECK_EQ(state.m_time, 0.0f);
    OBL_CHECK_EQ(env.log, (Log{"res 800x600", "script", "mode 6", "dialog ExitGame"}));
}

OBL_TEST_CASE("game_states.intro_shows_the_front_screen_then_the_menu")
{
    Probe<IntroFields> state{};
    Env env;
    IntroEnter(state, env);
    OBL_CHECK_EQ(state.m_cursorX, 11);
    OBL_CHECK_EQ(state.m_cursorY, 22);
    OBL_CHECK_EQ(env.log, (Log{"script", "mode 6", "play waves_loop 2 0.000000", "volume 7 0.800000", "dialog DeepRedLogo"}));
    env.log.clear();
    env.button = 1;
    IntroUpdate(state, env);
    OBL_CHECK_EQ(state.m_time, 5.0f);
    env.button = 0;
    IntroUpdate(state, env);
    OBL_CHECK_EQ(state.m_phase, 1);
    OBL_CHECK_EQ(state.m_time, 0.0f);
    OBL_CHECK_EQ(env.log, Log{"dialog FrontScreen"});
    env.log.clear();
    state.m_time = 5.0f;
    IntroUpdate(state, env);
    OBL_CHECK_EQ(env.log, Log{"next 1"});
    env.log.clear();
    state.m_phase = 2; // neither phase: the timer resets and nothing happens
    state.m_time = 6.0f;
    IntroUpdate(state, env);
    OBL_CHECK_EQ(env.log, Log{});
    OBL_CHECK_EQ(state.m_time, 0.0f);
}

OBL_TEST_CASE("game_states.load_game_hands_over_after_a_tenth_of_a_second")
{
    Probe<LoadGameFields> state{};
    Env env;
    LoadGameEnter(state);
    OBL_CHECK_EQ(state.m_entered, 1);
    env.step = 0.1f;
    LoadGameUpdate(state, env); // 0.1f is not above the 0.1f limit
    OBL_CHECK_EQ(env.log, Log{});
    env.step = 0.02f;
    LoadGameUpdate(state, env);
    OBL_CHECK_EQ(env.log, Log{"next 0"});
    state.m_time = 0.0f;
    env.log.clear();
    env.step = 3.0f; // no clamp: one long frame is enough
    LoadGameUpdate(state, env);
    OBL_CHECK_EQ(env.log, Log{"next 0"});
}

OBL_TEST_CASE("game_states.main_menu_shows_the_menu_first_and_the_scenario_screen_after_a_game")
{
    Probe<MainMenuFields> state{};
    state.m_showMenu = 1;
    Env env;
    const Log head = {"play waves_loop 2 0.000000", "volume 7 0.800000", "res 800x600", "script", "mode 6"};
    MainMenuEnter(state, env);
    Log expected = head;
    expected.insert(expected.end(), {"dialog MainMenu", "return clear"});
    OBL_CHECK_EQ(env.log, expected);
    OBL_CHECK_EQ(state.m_showMenu, 0);
    env.log.clear();
    MainMenuEnter(state, env);
    expected = head;
    expected.insert(expected.end(), {"progress", "dialog ScenarioScreen"});
    OBL_CHECK_EQ(env.log, expected);
    OBL_CHECK_EQ(state.m_showMenu, 0);
    env.log.clear();
    env.returnToMenu = true; // coming back from GameWon
    MainMenuEnter(state, env);
    expected = head;
    expected.insert(expected.end(), {"dialog MainMenu", "return clear"});
    OBL_CHECK_EQ(env.log, expected);
}

OBL_TEST_CASE("game_states.reset_game_picks_a_loading_screen_by_the_parity_of_the_draw")
{
    Env env;
    env.draw = 4;
    OBL_CHECK_EQ(ResetUpdate(env), 0);
    OBL_CHECK_EQ(env.log, (Log{"dialog LoadingScreen2", "next 5"}));
    env.log.clear();
    env.draw = 32767;
    ResetUpdate(env);
    OBL_CHECK_EQ(env.log, (Log{"dialog LoadingScreen1", "next 5"}));
    Probe<ResetGameFields> state{};
    env.log.clear();
    ResetEnter(state, env);
    OBL_CHECK_EQ(env.log, (Log{"res 800x600", "script", "mode 6"}));
}

OBL_TEST_CASE("game_states.game_won_turns_nine_pages_then_returns_to_the_menu")
{
    Probe<GameWonFields> state{};
    Env env;
    GameWonEnter(state, env);
    OBL_CHECK_EQ(env.log, (Log{"stop samples", "clear pages", "res 800x600", "script", "mode 6", "stream night6.mp3", "page"}));
    env.log.clear();
    env.step = 0.1f;
    state.m_time = 19.85f;
    GameWonUpdate(state, env);
    OBL_CHECK_EQ(env.log, Log{}); // 19.95
    GameWonUpdate(state, env); // 20.05 > 20: the next page
    OBL_CHECK_EQ(state.m_page, 1);
    OBL_CHECK_EQ(state.m_time, 0.0f);
    OBL_CHECK_EQ(env.log, Log{"page"});
    env.log.clear();
    state.m_page = 8; // the last page waits for a click
    state.m_time = 100.0f;
    GameWonUpdate(state, env);
    OBL_CHECK_EQ(env.log, Log{});
    env.button = 1;
    GameWonUpdate(state, env);
    OBL_CHECK_EQ(state.m_page, 9);
    OBL_CHECK_EQ(env.log, (Log{"next 1", "return set"}));
    env.log.clear();
    state.m_entered = 1;
    GameWonExit(state, env);
    OBL_CHECK_EQ(env.log, Log{"stream close"});
    OBL_CHECK_EQ(state.m_entered, 0);
}

OBL_TEST_CASE("game_states.game_won_credits_use_computed_page_geometry")
{
    using obl::gui_content::Credit;
    std::vector<Credit> rows(37, Credit{2, 123, 255, false, 0});
    Probe<GameWonFields> state{};
    Env env;
    GameWonShowPage(state, env, rows);
    OBL_CHECK_EQ(env.log.size(), size_t{40});
    OBL_CHECK_EQ(env.log[4], "text 40,160,264,190 CREDIT_ENTRY_123 f2 c255");
    OBL_CHECK_EQ(env.log[16], "text 288,160,512,190 CREDIT_ENTRY_123 f2 c255");
    OBL_CHECK_EQ(env.log.back(), "text 536,490,760,520 CREDIT_ENTRY_123 f2 c255");
    env.log.clear();
    state.m_page = 1;
    GameWonShowPage(state, env, rows);
    OBL_CHECK_EQ(env.log.size(), size_t{5});
    OBL_CHECK_EQ(env.log.back(), "text 40,160,264,190 CREDIT_ENTRY_123 f2 c255");
    OBL_CHECK_EQ(obl::game::CreditPageCount(0), 1);
    OBL_CHECK_EQ(obl::game::CreditPageCount(6), 1);
    OBL_CHECK_EQ(obl::game::CreditPageCount(36), 1);
    OBL_CHECK_EQ(obl::game::CreditPageCount(37), 2);
    OBL_CHECK_EQ(obl::game::CreditPageCount(211), 6);
    env.log.clear();
    GameWonShowPage(state, env, {});
    OBL_CHECK_EQ(env.log.size(), size_t{4});
}

namespace {
struct StateEnv : Env {
    using Widgets = std::vector<int>;
    static inline Log g_events;
    static inline int g_calls = 0;
    explicit StateEnv(obl::game::GameState&, Widgets* = nullptr) { log = g_events; }
    ~StateEnv() { g_events = log; }
    static void Count() { ++g_calls; }
    static const char* Script(const char* key) { return key; }
    static std::span<const obl::gui_content::Credit> CreditRows() { return {}; }
    void RenderPages() { log.push_back("render pages"); }
};

template<class State> void CheckFrontEndLifecycle(const char* name, const char* script)
{
    StateEnv::g_events.clear();
    StateEnv::g_calls = 0;
    State state;
    OBL_CHECK_EQ(state.Name(), std::string(name));
    OBL_CHECK_EQ(state.GetGuiScript(), script);
    obl::game::GameState& base = state;
    base.Enter();
    OBL_CHECK_EQ(state.m_entered, 1);
    OBL_CHECK_EQ(base.Update(), 0);
    base.Exit();
    OBL_CHECK_EQ(state.m_entered, 0);
    OBL_CHECK_EQ(StateEnv::g_calls, 4);
}
} // namespace

OBL_TEST_CASE("game_states.template__dispatch_all_front_end_lifecycles")
{
    CheckFrontEndLifecycle<obl::game::ExitGameState<StateEnv>>("ExitGame", nullptr);
    CheckFrontEndLifecycle<obl::game::IntroState<StateEnv>>("Intro", kIntroLayout);
    CheckFrontEndLifecycle<obl::game::LoadGameState<StateEnv>>("LoadGame", nullptr);
    CheckFrontEndLifecycle<obl::game::MainMenuState<StateEnv>>("MainMenu", kMainMenuLayout);
    CheckFrontEndLifecycle<obl::game::ResetGameState<StateEnv>>("ResetGame", kResetLayout);
}

OBL_TEST_CASE("game_states.game_won_destructor_closes_only_an_entered_stream")
{
    using State = obl::game::GameWonState<StateEnv>;
    StateEnv::g_events.clear();
    {
        State state;
    }
    OBL_CHECK_EQ(StateEnv::g_events, Log{});
    {
        State state;
        OBL_CHECK_EQ(state.Name(), std::string("GameWon"));
        OBL_CHECK_EQ(state.GetGuiScript(), kGameWonLayout);
        state.Enter();
        state.PostRender();
        OBL_CHECK_EQ(state.m_entered, 1);
    }
    OBL_CHECK_EQ(StateEnv::g_events.back(), std::string("stream close"));
    OBL_CHECK_EQ(StateEnv::g_events[StateEnv::g_events.size() - 2], std::string("render pages"));
    StateEnv::g_events.clear();
    {
        State state;
        state.Enter();
        state.Exit();
        OBL_CHECK_EQ(state.m_entered, 0);
        StateEnv::g_events.clear();
    }
    OBL_CHECK_EQ(StateEnv::g_events, Log{});
}

OBL_TEST_CASE("game_states.game_won_uses_content_page_count")
{
    Probe<GameWonFields> state{};
    Env env;
    env.creditPages = 1;
    state.m_time = 100.0f;
    GameWonUpdate(state, env);
    OBL_CHECK(env.log.empty()); // the final page waits for a click
    env.button = 1;
    GameWonUpdate(state, env);
    OBL_CHECK_EQ(env.log, (Log{"next 1", "return set"}));
}
