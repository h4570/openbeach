#pragma once

#include <string>
#include <vector>

namespace obl::test {
// building-list / building-visits / building-age / building-demolish / building-state (building_fixture.cpp).
bool ExecuteBuildingUpdateCommand(const std::string& name, const std::vector<std::string>& args);

// Live probes building-state-<0..9>: the number of buildings in that state. False for any other name.
bool ReadBuildingUpdateProbe(const std::string& name, double& out);
} // namespace obl::test
