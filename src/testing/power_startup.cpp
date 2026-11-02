// startup-power <NAME> <tileX> <tileY> [demandAll installed output]: creates a building the way the scenario loader
// does (world::CreateBuilding) and runs its STATE_STARTUP handler Building::OnExitState, then checks whether the
// building came up shut down for lack of power (m_closedForPower) against the documented rule
// (docs/specs/index.md). The optional numbers temporarily replace World m_powerUse/m_maxPower/m_currentPower so every branch of the
// rule can be reached without building a real power grid; the building totals are recomputed on the next frame.

#include "power_startup.h"
#include "world_state.h"
#include "game/world/world_control.h"

#include "game/objects/building_methods.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "test_util.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace obl::test {
bool ExecutePowerStartupCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "startup-power" || args.size() < 3) return false;
    layout::World* world = World();
    if (!world) {
        OBL_LOG("autotest: FAILED startup-power requires a world");
        return true;
    }
    if (args.size() >= 6) {
        const float demand = std::stof(args[3]);
        const float installed = std::stof(args[4]);
        const float output = std::stof(args[5]);
        world::SetPowerTotals(*world, demand, installed, output);
    }
    float demandAll = world->m_powerUse, installed = world->m_maxPower, output = world->m_currentPower;

    auto building = SpawnBuilding(args[0], std::stoi(args[1]), std::stoi(args[2]));
    if (!building) return true;
    int power = building->m_definition->m_Power;

    object::CompleteBuildingStartup(*building);

    // The rule: only a world whose output is below the demand of all consumers sheds the newcomer, and then not a
    // zero-power building and not the HANDYMAN while some generator is shut down (output below installed).
    bool handyman = args[0] == "HANDYMAN";
    bool expected = output < demandAll && !((handyman && output < installed) || power < 1);
    bool shed = building->m_closedForPower != 0;
    bool closed = building->m_OpenStatus != 0;
    bool ok = shed == expected && (!shed || closed);
    OBL_LOG("autotest: %s startup-power %s id=%d power=%d demandAll=%g installed=%g output=%g -> shed=%d closed=%d "
            "expected-shed=%d",
            ok ? "PASS" : "FAILED", args[0].c_str(), building->m_id, power, demandAll, installed, output, shed, closed, expected);
    return true;
}
} // namespace obl::test
