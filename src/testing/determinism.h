#pragma once

#include "core/rand_tape.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace obl::test {

// Wraps the game clock update so that `fixed-dt` / `step` can replace the real frame time (script mode only).
bool InitDeterminism();

// seed <n> | fixed-dt <seconds>|off | fp-report (game-thread FPU/MXCSR controls)
bool ExecuteDeterminismCommand(const std::string& name, const std::vector<std::string>& args);

// `seed <n>`: seeds the game's rand() (msvcrt, game thread) and restarts the rand-calls count. False while a tape is active.
bool SeedGameRand(unsigned seed);

// Fixed frame time in seconds; 0 = real time. `step N dt` sets it for the length of the step, `fixed-dt` until changed.
void SetFixedDt(double seconds);
double FixedDt();
// The value of the last `seed` directive, if one ran.
std::optional<uint32_t> LastSeed();
// Thread-local; callers must complete or cancel a tape before returning to game_api::Update.
RandTape& GameRandTape();
// Before the next script of a headless script list: no fixed frame time or seed (the state of a fresh process).
void ResetDeterminism();

} // namespace obl::test
