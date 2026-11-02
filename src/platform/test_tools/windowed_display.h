#pragma once

#include <string>
#include <vector>

namespace obl::test {

bool RegisterDisplayTrace();
bool ExecuteDisplayCommand(const std::string& name, const std::vector<std::string>& args);

} // namespace obl::test
