#pragma once

// The headless host of the chaos bot (docs/testing/chaos-bot.md): `--headless <file>.chaos` boots the run's start, plays the bot's
// random commands (or a fixed command list) and checks the hard invariants: the run reaches max-ticks, rejected commands leave the
// world hash unchanged, and a save and reload at `save-at` continues to the same hashes. It writes the run as a replay, which the runner
// plays in a fresh process for the determinism check (tools/chaos_runs.py).

#include "core/char16.h"
#include <string_view>

namespace obl::game {
struct Game; // game/game/game.h
}

namespace obl::test {

// True for a path the headless host runs as a chaos run file (the `.chaos` extension, any case).
bool IsChaosRunPath(Text16View path);

// Runs the chaos run file of OBL_HEADLESS. Exit code 0 when every hard invariant held, kExitHeadlessMismatch when one failed,
// kExitHeadlessFailed when the run could not start.
// The Game is the game context's.
int RunChaos();

} // namespace obl::test
