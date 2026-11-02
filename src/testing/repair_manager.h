#pragma once

#include <string>
#include <vector>

namespace obl::test {
// repair-report <label>: counts of mechanics/cleaners/scenery and the RepairManager queues (`autotest: repair-report ...` keys for expect-last).
// repair-fixture [buildings] [scenery]: queues breakdown/inspection/urgent requests for the first objects through the game's own AddJob/AddUrgentJob
// - tests/sim_repair_*.txt, docs/specs/repair.md.
bool ExecuteRepairCommand(const std::string& name, const std::vector<std::string>& args);
} // namespace obl::test
