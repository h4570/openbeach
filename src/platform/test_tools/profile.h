#pragma once

#include <string>
#include <vector>

namespace obl::test {

// `profile on` starts a sampling profiler of the calling (game) thread; `profile off [n]` logs the top n (default 60) addresses by
// self and by inclusive (return addresses found on the stack) samples. Each entry includes its module offset and an available PDB name.
bool ExecuteProfileCommand(const std::string& name, const std::vector<std::string>& args);

} // namespace obl::test
