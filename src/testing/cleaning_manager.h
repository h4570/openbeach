#pragma once

#include <string>
#include <vector>

namespace obl::test {
// cleaning-report <label>: cleaners, objects named like a cleaning job (Litter, Bin, LOO) and the CleaningManager queues
// (`autotest: cleaning-report ...` keys for expect-last).
// cleaning-fixture [n]: requests for the first n objects of each cleaning kind through the game's own AddJob/AddUrgentJob, repeated and promoted
// ones included (tests/sim_cleaning_*.txt, docs/specs/cleaning.md).
bool ExecuteCleaningCommand(const std::string& name, const std::vector<std::string>& args);
} // namespace obl::test
