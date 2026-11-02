#pragma once
#include <string>
#include <vector>
namespace obl::test {
bool ExecuteBinarySerialCommand(const std::string& name, const std::vector<std::string>& args);
// Before the next script of a headless script list: deletes the scratch save slot a script left behind (no `serial-cleanup`).
void ResetBinarySerial();
} // namespace obl::test
