// The Linux obl-sim's stand-ins for the windowed modules that src/host_linux/CMakeLists.txt leaves out of it (WINDOWED_MODULES):
// window, presentation, input and the intro movie (the Input object is headless_input.cpp). A headless run reaches none of
// them: InitGame skips their init (OBL_WINDOWS_ONLY), so nothing calls their entries.

#include "game/game/game_accessors.h"
#include "game/ui/input.h"
#include "platform/intro_movie.h"
#include "platform/sdl_present.h"
#include "platform/test_tools/presenter.h"
#include "core/log.h"

#include <windows.h>

#include <cstdint>

namespace obl::platform {

// intro_movie.cpp without a player: Start skips as with PlayMovie=0 (headless always sets it), so no frame runs.
IntroMovieStats g_movieStats;

void IntroMovieStart(void*, const char*)
{
    OBL_LOG("intro movie: skipped (PlayMovie=0)");
}

bool IntroMovieFrame()
{
    return false;
}

void IntroMovieFinish()
{
    ++g_movieStats.destructCalls;
}

const IntroMovieStats& GetIntroMovieStats()
{
    return g_movieStats;
}

// sdl_present.cpp: a headless run has no message loop to end.
void RequestSdlQuit(int code)
{
    OBL_LOG("obl-sim: PostQuitMessage(%d) ignored, no message loop", code);
}
void PostSdlGameWindowClose() {}
// No SDL window: window placement (window_placement.cpp) leaves the absent window alone.
SDL_Window* SdlGameWindow(void*)
{
    return nullptr;
}
// Never reached: a headless present returns before it (renderer_display.cpp).
bool PresentBackBuffer(const uint16_t*, int, int, int, bool)
{
    return false;
}

void InjectMouseButton(int, bool) {}
void SetSyntheticCursor(int, int) {}

} // namespace obl::platform

namespace obl::test {

// test_tools/presenter.cpp (windowed only): its commands and probes answer "not mine".
bool ExecutePresenterCommand(const std::string&, const std::vector<std::string>&)
{
    return false;
}
bool ReadPresenterProbe(const std::string&, double&)
{
    return false;
}

} // namespace obl::test
