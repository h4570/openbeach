#pragma once
#include <string>
#include <vector>
namespace obl::test {
bool ExecuteSpatialSamplesCommand(const std::string& name, const std::vector<std::string>& args);
bool ReadSpatialSamplesProbe(const std::string& name, double& out);
} // namespace obl::test
