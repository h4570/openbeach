#pragma once

#include <string>
#include <vector>

namespace obl::test {
bool RegisterGuestDesireTrace();
bool ExecuteGuestDesireCommand(const std::string& name, const std::vector<std::string>& args);
} // namespace obl::test
