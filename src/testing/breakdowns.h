#pragma once

#include <string>
#include <vector>

namespace obl::test {
bool ExecuteBreakdownCommand(const std::string& name, const std::vector<std::string>& args);

// Live probes over the test buildings for `await`/`expect`: fail-stamped (buildings that rolled since fail-arm, LastFailTime
// restamped), fail-inspected (CalledForInspection set). False for any other name.
bool ReadBreakdownProbe(const std::string& name, double& out);

// Before the next script of a headless script list: forgets the test buildings (they belonged to the previous world).
void ResetBreakdowns();
} // namespace obl::test
