#pragma once

#include "core/char16.h"
#include <string>
#include <vector>

namespace obl::test {
void InitLayoutWorldDump(const Text16& gameDirectory);
// scripts-dump: the scenario script and every object script read through the native script records into <game>/scripts-dump.json for
// tools/check_script_dump.py (oracle: the decoded .ble sources of the same scripts).
bool ExecuteLayoutWorldCommand(const std::string& name, const std::vector<std::string>& args);
} // namespace obl::test
