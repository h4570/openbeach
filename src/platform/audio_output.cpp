// The audio output around long frames (game_host.h). SDL's Emscripten backend feeds Web Audio from a ScriptProcessorNode
// on the main thread; while one frame builds a world the node gets no new samples and the browser repeats its last buffer.
// Suspending the AudioContext takes effect on the audio thread, so it holds even while the main thread is blocked.

#include "platform/audio_output.h"

#include "core/log.h"
#include "game/game/game_host.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace obl::platform {

namespace {

AudioOutputStats g_stats;

} // namespace

void SuspendAudioOutput()
{
    if (g_stats.suspended) return;
    g_stats.suspended = true;
    ++g_stats.suspends;
    OBL_LOG("audio output: suspended for a long frame");
#ifdef __EMSCRIPTEN__
    // clang-format off: JavaScript
    EM_ASM({
        const sdl = Module["SDL3"];
        if (sdl && sdl.audioContext && sdl.audioContext.state === "running") sdl.audioContext.suspend();
    });
    // clang-format on
#endif
}

void ResumeAudioOutput()
{
    if (!g_stats.suspended) return;
    g_stats.suspended = false;
    OBL_LOG("audio output: resumed");
#ifdef __EMSCRIPTEN__
    // clang-format off: JavaScript
    EM_ASM({
        const sdl = Module["SDL3"];
        if (sdl && sdl.audioContext && sdl.audioContext.state === "suspended") sdl.audioContext.resume();
    });
    // clang-format on
#endif
}

const AudioOutputStats& GetAudioOutputStats()
{
    return g_stats;
}

} // namespace obl::platform
