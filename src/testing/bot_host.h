#pragma once

// The headless host of the scenario bots (docs/architecture/bots/index.md): `--headless <file>.bot` boots the run's start, then lets the bot
// think every few ticks on a read-only view of the live world, submits its commands and stops at the scenario's verdict.

#include "core/char16.h"
#include "game/bot/bot.h"

#include <cstdint>
#include <string_view>

namespace obl::game {
struct Game; // game/game/game.h
}

namespace obl::test {

// True for a path the headless host runs as a bot run file (the `.bot` extension, any case).
bool IsBotRunPath(Text16View path);

// The world as a bot sees it at `tick` (a copy; nothing of it points into the game). Needs a world.
bot::WorldView CaptureWorldView(uint32_t tick);
// The Queries a bot gets: the place mode's checks on the live world.
const bot::Queries& LiveQueries();

// Runs the bot run file of OBL_HEADLESS. Exit code 0 when the run reached its expected outcome, kExitHeadlessMismatch for the other
// outcome or none within max-ticks, kExitHeadlessFailed when it could not run.
int RunBot(game::Game& game);

} // namespace obl::test
