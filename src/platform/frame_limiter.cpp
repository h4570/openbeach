#include "frame_limiter.h"

#include "core/config.h"
#include "core/log.h"
#include "core/os.h"
#include "game/game/game_api.h"

#include <SDL3/SDL_timer.h>

namespace obl::platform {

namespace {

constexpr int kStatsIntervalSec = 30;
// Scripted tests (OBL_SCRIPT) read the rate back from `autotest:` lines and cannot wait 30 s per sample.
constexpr int kScriptStatsIntervalSec = 3;
constexpr Uint64 kNsPerSec = 1000000000;
// Animation frames arrive with a jitter of a millisecond or two: a frame that early still counts as due, so MaxFps at
// the display rate keeps every frame instead of every other one.
constexpr Uint64 kBrowserSlackNs = 2000000;

int g_statsIntervalSec = kStatsIntervalSec;
int g_statsReports;

game::GameFrame g_renderFrame;

Uint64 g_frameNs;
Uint64 g_nextFrame;
Uint64 g_statsStart;
Uint64 g_overrunNs;
int g_statsFrames;

// SDL_DelayPrecise sleeps most of the wait and spins the last fraction, as accurate as the old high-resolution timer.
// In the browser the main loop runs once per animation frame and must not block: there is nothing to wait for.
void WaitUntil(Uint64 deadline)
{
#ifdef __EMSCRIPTEN__
    (void)deadline;
#else
    const Uint64 now = SDL_GetTicksNS();
    if (deadline > now) SDL_DelayPrecise(deadline - now);
#endif
}

void RenderLimited(void* game)
{
    g_renderFrame(game);

    Uint64 now = SDL_GetTicksNS();
    if (g_statsStart == 0) // first frame; the intro movie plays before the main loop starts
        g_nextFrame = g_statsStart = now;
    g_nextFrame += g_frameNs;
    // Fell behind by more than a frame (loading, slow frame, debugger): restart the schedule
    // instead of running a burst of unthrottled frames to catch up.
    if (g_nextFrame + g_frameNs < now) {
        g_overrunNs += now - g_nextFrame; // time the limiter could not influence (loaded host)
        g_nextFrame = now;
    } else
        WaitUntil(g_nextFrame);

    ++g_statsFrames;
    now = SDL_GetTicksNS();
    if (now - g_statsStart >= static_cast<Uint64>(g_statsIntervalSec) * kNsPerSec) {
        const double fps = g_statsFrames * static_cast<double>(kNsPerSec) / static_cast<double>(now - g_statsStart);
        OBL_LOG("frame limiter: %.1f FPS", fps);
        if (g_statsIntervalSec == kScriptStatsIntervalSec) {
            // The same rate without the time lost to overrun frames: it stays at the cap on a CPU-starved host
            // (where the wall-clock rate drops below it) and still shows a limiter that waits too long or not at all.
            const Uint64 elapsed = now - g_statsStart;
            const Uint64 paced = elapsed > g_overrunNs ? elapsed - g_overrunNs : 1;
            const double pacedFps = g_statsFrames * static_cast<double>(kNsPerSec) / static_cast<double>(paced);
            OBL_LOG("autotest: limiter-report limiter-n=%d limiter-fps=%.1f limiter-paced-fps=%.1f", ++g_statsReports, fps, pacedFps);
        }
        g_overrunNs = 0;
        g_statsStart = now;
        g_statsFrames = 0;
    }
}

} // namespace

bool BrowserFrameDue()
{
    // Unlimited, or before the first frame: the schedule starts there.
    if (g_frameNs == 0 || g_statsStart == 0) return true;
    return SDL_GetTicksNS() + kBrowserSlackNs >= g_nextFrame;
}

bool ConfigureFrameLimiter()
{
    const int maxFps = config::Get().maxFps;
    if (maxFps <= 0) {
        OBL_LOG("frame limiter: MaxFps=0, unlimited");
        return true;
    }

    g_frameNs = kNsPerSec / static_cast<Uint64>(maxFps);
    if (os::HasEnv("OBL_SCRIPT")) g_statsIntervalSec = kScriptStatsIntervalSec;
    OBL_LOG("frame limiter: %d FPS (SDL time)", maxFps);

    return game::WrapGameRender(&RenderLimited, &g_renderFrame);
}

} // namespace obl::platform
