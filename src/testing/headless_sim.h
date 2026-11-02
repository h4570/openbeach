#pragma once

// The headless mode of the standalone host (OBL_HEADLESS, launcher `--headless <replay|script>`; docs/architecture/headless/index.md): the
// game runs without window, renderer output, audio or input devices. A replay file prints the world hash at its checkpoints; a test
// script (OBL_SCRIPT as well) runs the script interpreter's simulation directives frame after frame.

#include "platform/headless_main.h"

namespace obl::game {
struct Game; // game/game/game.h
}

namespace obl::test {

using platform::kExitHeadlessFailed; // platform/headless_main.h
using platform::kExitHeadlessMismatch;

// Registers RunHeadless as the platform's headless driver. In headless mode also the determinism wrappers the run needs (the RNG
// tape that counts rand-calls, the fixed frame time). True otherwise.
bool InitHeadless();

// Replaces WinMain's message loop in headless mode. A replay: boots its start, then runs its ticks back to back; before each tick T
// it takes the world hash where the replay has a checkpoint (and at ticks 0 and `end`), submits the commands of T and runs one
// game_api::Update; game_api::Render never runs. A script: game_api::Update frames until the script quits, game_api::Render only at state switches; a
// script list (`.list`) runs its scripts one after another, back in the Intro state with a fresh clock before each.
// The Game is the game context's (game_api::Create). Returns the process exit code.
int RunHeadless();

} // namespace obl::test
