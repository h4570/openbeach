// The Windows test diagnostics' commands on hosts without them (src/host_linux/CMakeLists.txt, WINDOWS_TEST_TOOLS): they
// answer "not mine", which the interpreter reports as unknown, and the frame beat has no watchdog to feed.

#include "platform/test_tools/crash.h"
#include "platform/test_tools/cursor.h"
#include "platform/test_tools/expected_exit.h"
#include "platform/test_tools/profile.h"
#include "platform/test_tools/windowed_display.h"

namespace obl::test {

void WatchdogBeat() {}
bool ExecuteCrashCommand(const std::string&)
{
    return false;
}

bool ExecuteCursorCommand(const std::string&)
{
    return false;
}

bool ExecuteExpectedExitCommand(const std::string&, const std::vector<std::string>&)
{
    return false;
}

bool ExecuteProfileCommand(const std::string&, const std::vector<std::string>&)
{
    return false;
}

bool ExecuteDisplayCommand(const std::string&, const std::vector<std::string>&)
{
    return false;
}

} // namespace obl::test
