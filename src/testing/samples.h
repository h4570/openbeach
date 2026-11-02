#pragma once
#include <string>
#include <vector>
namespace obl::test {
bool ExecuteSamplesCommand(const std::string& name, const std::vector<std::string>& args);
bool ReadSamplesProbe(const std::string& name, double& out);
} // namespace obl::test
