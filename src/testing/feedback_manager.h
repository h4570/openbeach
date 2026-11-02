#pragma once

#include <string>
#include <vector>

namespace obl::test {
// feedback-report <label>: waiting notices, history entries, silenced types and the review clock of the FeedbackManager
// (`autotest: feedback-report ...` keys for expect-last).
// feedback-fixture [n]: n notices (default 20) through the game's own Add/AddWide, alternating key and wide text, the first with the balloon
// busy so they queue, and the history cursor walked over the entries (tests/sim_feedback_*.txt).
bool ExecuteFeedbackCommand(const std::string& name, const std::vector<std::string>& args);
} // namespace obl::test
