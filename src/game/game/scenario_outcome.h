#pragma once

#include "game/bot/bot.h"

#include <cstdint>

// The scenario verdict of the running world (docs/architecture/bots/contract.md, "Win and loss"): the world script's `ShowMenu` reports every dialog
// it opens; the won and lost dialogs of the campaign scripts set the outcome. Observation only, the simulation never reads it.
namespace obl::game {

void NoteScriptDialog(const char* dialog);
bot::Outcome GetScenarioOutcome();
// Back to Running for the next world (a bot run calls it once its world has settled).
void ResetScenarioOutcome();

} // namespace obl::game
