#pragma once

#include <cstdint>

namespace obl::ui {
struct Renderer; // game/ui/renderer_types.h
}
namespace obl::audio {
struct Sound; // game/ui/sound_types.h
}

// The platform services Game construction calls (game.cpp), implemented in src/platform/ and bound at link time like
// diagnostics.h. Window handles are opaque.
namespace obl::ui {
// The renderer 16 constructor and Destruct on the windowed path (renderer_lifecycle.cpp); also forces the windowed branch.
ui::Renderer* ConstructRenderer(ui::Renderer* renderer, void* hwnd, int width, int height, int windowed);
void DestructRenderer(ui::Renderer* renderer);
} // namespace obl::ui
namespace obl::platform {
// The intro movie as a state of the host's main loop: Start opens it, each Frame call shows one pass of it and returns
// false once it ended, was skipped or never started, Finish closes it (also one that is still running).
void IntroMovieStart(void* window, const char* path);
bool IntroMovieFrame();
void IntroMovieFinish();
// Around a frame that holds the main loop for long (a world built, loaded or torn down): the browser suspends its audio
// output, because Web Audio repeats the last buffer the blocked main thread fed it; native hosts only count the calls.
void SuspendAudioOutput();
void ResumeAudioOutput();
} // namespace obl::platform
