#pragma once

#include <string>
#include <vector>

namespace obl::object {
struct Building;
}

namespace obl::test {
// Scenario creation: no purchase, placement validation or construction job. Startup is posted by the game.
object::Building* SpawnBuilding(const std::string& name, int tx, int ty, int age = 0);
void ResetWorldStateCommands();
// Live probes staff-<lifeguard|mechanic|cleaner|rep|builder|securityguard>-<state>: members of the profession in that state. False for other names.
bool ReadWorldStateProbe(const std::string& name, double& out);
bool ExecuteWorldStateCommand(const std::string& name, const std::vector<std::string>& args);
} // namespace obl::test
