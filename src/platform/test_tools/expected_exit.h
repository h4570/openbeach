#pragma once

#include <string>
#include <vector>

namespace obl::test {

// Commands that end the game on purpose, for scripts with `# @expect-exit` (tools/autotest.ps1):
//   fatal-error <text...>        the game's FatalError("autotest fatal-error %s %d", text, 42): the varargs reach the formatter
//   show-error-exit <text...>    the game's ShowErrorAndExit(game window, text as UTF-16)
// Both call the function's address, so the game's function runs as it would for a game caller.
bool ExecuteExpectedExitCommand(const std::string& name, const std::vector<std::string>& args);

} // namespace obl::test
