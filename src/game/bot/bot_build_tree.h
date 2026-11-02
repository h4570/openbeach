#pragma once

#include "bot.h"

#include <memory>

namespace obl::bot {

// Scenario 1 ("build-tree-s1", docs/architecture/bots/index.md): the goal is one finished building of every type the scenario offers
// (BuildTreeExist) before 17:00 on day 2. The bot places each type as soon as its predecessor is finished and the cash covers it, at the
// free spot nearest to the buildings already standing.
std::unique_ptr<Bot> MakeBuildTreeBot(const BotPlan&);

} // namespace obl::bot
