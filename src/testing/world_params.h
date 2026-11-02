#pragma once

#include "core/char16.h"
#include <string>
#include <vector>

namespace obl::test {
void InitWorldParamsDump(const Text16& gameDirectory);
// worldparams-dump: the loaded WorldParams read through world_params_tables.h, written to
// <game>/worldparams-dump.json for tools/check_worldparams_dump.py (oracle: the .ble files).
bool ExecuteWorldParamsCommand(const std::string& name, const std::vector<std::string>& args);
} // namespace obl::test
