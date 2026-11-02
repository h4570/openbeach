// Native process startup: the host (src/launcher) calls RunGame after InitGame; startup.cpp registers the game's
// static destructors, which run at process exit.
// SDL creates the window and its events drive the loop; the window procedure keeps only Windows chrome (sdl_present.cpp).

#include "platform_main.h"
#include "game/game/diagnostics.h"
#include "game/game/game_accessors.h"
#include "platform/runtime.h"
#include "game/game/game_api.h"

#include "game/game/game_host.h"
#include "platform/frame_limiter.h"
#include "platform/headless_main.h"
#include "platform/input.h"
#include "platform/instance_lock.h"
#include "platform/sdl_present.h"
#include "game/game/startup.h"

#include "core/config.h"
#include "game/game/game.h"
#include "game/game/game_context.h"
#include "game/game/game_parameters.h"
#include "game/ui/renderer_types.h"
#include "core/log.h"
#include "core/platform_main.h"
#include "core/call_counter.h"

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
extern "C" IMAGE_DOS_HEADER __ImageBase; // the game executable (MSVC linker symbol)
#endif

namespace obl::platform {

namespace {

// Static destructors the game registers (RunStartup): they run, last first, when the process exits normally
// (main returns or platform::Exit calls std::exit).
std::vector<game::ExitHandler>& ExitFunctions()
{
    static std::vector<game::ExitHandler> functions;
    return functions;
}

void RunExitFunctions()
{
    auto& functions = ExitFunctions();
    while (!functions.empty()) {
        const game::ExitHandler function = functions.back();
        functions.pop_back();
        function();
    }
}

game::ExitHandler __cdecl RegisterOnExit(game::ExitHandler function)
{
    if (ExitFunctions().empty()) std::atexit(RunExitFunctions);
    ExitFunctions().push_back(function);
    return function;
}

// Calls of the functions.
uint32_t g_cppEntryCalls{};
uint32_t g_cppWinMainCalls{};

test::CallCounter g_platformMainCalls("PlatformMainCalls");
void ReportCalls(int)
{
    OBL_LOG("autotest: platform-main cpp-entry=%u cpp-winmain=%u", g_cppEntryCalls, g_cppWinMainCalls);
}

int RunWinMain(void* instance, int show);

} // namespace

int RunGame()
{
    ++g_cppEntryCalls;
    OBL_LOG("platform-main: C++ entry");
    game::RunStartup(&RegisterOnExit);
#ifdef _WIN32
    STARTUPINFOA info{};
    GetStartupInfoA(&info);
    const int show = platform_main::ShowCommand(info.dwFlags, info.wShowWindow);
    // The window class is registered for this module; no resource is read from it (icon and cursors are files under remaster/).
    void* const instance = &__ImageBase;
#else
    const int show = platform_main::ShowCommand(0, 0);
    void* const instance = nullptr;
#endif
    game::SnapshotStartupData();
    const int code = RunWinMain(instance, show);
    OBL_LOG("platform-main: WinMain returned %d", code);
    return code;
}

namespace {

// ---- WinMain ----

void ProfiledCall(void (*function)(game::Game*), void* game, int game::GameClock::* field)
{
    game::game_api::StartProfileTimer();
    function(static_cast<game::Game*>(game));
    const int ms = game::game_api::StopProfileTimerMs();
    game::GetGameClock()->*field = ms;
}

// Headless mode (docs/architecture/headless/index.md): the game without a window or message loop (headless_main.cpp, shared with
// the Linux obl-sim); the headless driver runs the replay instead of the loop and its result is the exit code.
int RunHeadlessMain(void* instance)
{
    const int code = RunHeadlessGame(instance);
    ReleaseInstanceLock();
    return code;
}

// The windowed game between its start and its teardown; one RunFrame call is one pass of the main loop.
struct WindowedGame {
    game::Game* game = nullptr;
    game::GameParameters* parameters = nullptr;
};
WindowedGame g_windowed;

// The host's events come from SDL (PumpSdlEvents): the Win32 PeekMessage/DispatchMessage loop and WM_QUIT,
// WM_CLOSE and WM_ACTIVATEAPP are SDL's quit, close and focus events; input stays queued for the input adapter.
// False once the game should end.
bool RunFrame()
{
    const SdlPumpResult pump = PumpSdlEvents();
    game::GetContext().appActive = pump.active;
    if (pump.quit || !pump.pumped) return false;
    game::game_api::StartStopProfileTimer();
    ProfiledCall(game::game_api::Update, g_windowed.game, &game::GameClock::m_updateMs);
    ProfiledCall(game::game_api::Render, g_windowed.game, &game::GameClock::m_renderMs);
    return true;
}

// The intro movie before the game's first frame (game_host.h): true while it plays; its last pass builds the rest of the
// Game, so native and browser hosts share the same startup state.
bool RunStartupFrame()
{
    if (platform::IntroMovieFrame()) return true;
    g_windowed.game = game::game_api::FinishCreate();
    return false;
}

void FinishWindowedGame()
{
    DestroyGame(g_windowed.parameters);
    DestroySdlGameWindow(); // the Win32 window was destroyed in WM_CLOSE; SDL's lives until the game is gone
    ReleaseInstanceLock();
    g_windowed = {};
}

#ifdef __EMSCRIPTEN__
// A browser tab must not block (docs/architecture/browser-build.md): the browser calls one frame per animation frame.
void RunBrowserFrame()
{
    if (!g_windowed.game) {
        RunStartupFrame(); // the movie keeps its own clock, so the MaxFps schedule starts with the game
        return;
    }
    if (!BrowserFrameDue()) return; // MaxFps below the display rate
    if (RunFrame()) return;
    emscripten_cancel_main_loop();
    FinishWindowedGame();
    OBL_LOG("platform-main: browser main loop ended");
    // The page replaces the canvas with its "game ended" layer (web/index.html).
    EM_ASM({
        if (Module["onGameEnded"]) Module["onGameEnded"]();
    });
}
#endif

int RunWinMain(void* instance, int show)
{
    ++g_cppWinMainCalls;
    const auto fatal = &game::FatalError;
    // One game per game directory (instance_lock.cpp): a second one quits silently.
    if (!AcquireInstanceLock()) {
        OBL_LOG("platform-main: WinMain quits (another instance holds the lock)");
        return 0;
    }

    if (config::Get().headless) return RunHeadlessMain(instance);
    auto* parameters = new game::GameParameters{};
    game::game_parameters::Construct(parameters, "data/script/gameparameters.pis");
    const bool windowed = true; // the fullscreen path is not used
    game::SetWindowedMode(1);

    char title[256];
    std::snprintf(title, sizeof(title), "%s", "Beach Life");
    SDL_Window* window = CreateSdlGameWindow(instance, 800, 600, title);
    if (!window) fatal("WinMain - error creating window");
    ShowSdlGameWindow(show);
    g_windowed = {nullptr, parameters};
    game::game_api::BeginCreate(instance, window);
    OBL_LOG("platform-main: C++ WinMain running the message loop (windowed %d, show %d, SDL window)", windowed, show);

#ifdef __EMSCRIPTEN__
    // Frames at the display's rate (requestAnimationFrame); simulate_infinite_loop unwinds this stack, so the call does
    // not return and the game ends in RunBrowserFrame.
    emscripten_set_main_loop(RunBrowserFrame, 0, true);
#else
    while (RunStartupFrame()) {}
    while (RunFrame()) {}
    FinishWindowedGame();
#endif
    return 0;
}

} // namespace

bool RegisterPlatformMainReports()
{
    g_platformMainCalls.SetDriver(ReportCalls);
    game::RegisterStartupReport();
    return true;
}

} // namespace obl::platform
