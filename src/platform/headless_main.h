#pragma once

namespace obl::game {
struct GameParameters;
}

namespace obl::platform {

// Exit codes of a headless run besides 0 (every checkpoint matched or had no recorded hash).
// unreadable replay, unsupported start, the world did not start or left gameplay; a script that did not reach `quit`
constexpr int kExitHeadlessFailed = 5;
constexpr int kExitHeadlessMismatch = 6; // a recorded checkpoint hash differs; a script reached `quit` with FAILED lines

// Replaces WinMain's message loop in headless mode and returns the process exit code. The test interpreter registers it
// (test::InitHeadless) because the drivers (replay, script, bot, chaos) live there; without one the run fails.
using HeadlessDriver = int (*)();
void SetHeadlessDriver(HeadlessDriver driver);

// WinMain's headless branch (docs/architecture/headless/index.md): the game parameters, the Game without a window (null window handle),
// the headless driver instead of the message loop, then the teardown. Returns the run's exit code. `instance` is the platform module
// handle (null on Linux). Shared by the Windows C++ WinMain and the Linux obl-sim.
int RunHeadlessGame(void* instance);

// The context's Game (game_api::Destroy) and its GameParameters, destroyed with the holder's delete; either may be absent.
void DestroyGame(game::GameParameters* parameters);

} // namespace obl::platform
