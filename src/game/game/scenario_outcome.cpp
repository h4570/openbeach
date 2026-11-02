#include "game/game/scenario_outcome.h"

#include "core/log.h"

#include <string>

namespace obl::game {
namespace {
bot::Outcome g_outcome = bot::Outcome::Running;
} // namespace

void NoteScriptDialog(const char* dialog)
{
    if (!dialog) return;
    const bot::Outcome outcome = bot::OutcomeOfDialog(dialog);
    // The first verdict stands: the scripts change to their after-game state at once, a later dialog does not overturn it.
    if (outcome == bot::Outcome::Running || g_outcome != bot::Outcome::Running) return;
    g_outcome = outcome;
    OBL_LOG("scenario: %s (ShowMenu %s)", std::string(bot::OutcomeName(outcome)).c_str(), dialog);
}

bot::Outcome GetScenarioOutcome()
{
    return g_outcome;
}

void ResetScenarioOutcome()
{
    g_outcome = bot::Outcome::Running;
}

} // namespace obl::game
