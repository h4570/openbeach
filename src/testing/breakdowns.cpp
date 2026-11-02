// Checks of the building breakdown model in Building::Update (game/objects/building_breakdown.h,
// docs/specs/repair.md):
//   fail-allowed <0|1>                        m_breakDownsAllowed
//   fail-create <NAME> <tx> <ty> <count> <age> test buildings (world::CreateBuilding, state 0, BuildingAge = age)
//   fail-arm [interval]                       make every test building due for a roll (LastFailTime = -100 h);
//                                             with an interval also set the repair interval and due
//                                             the inspection (LastRepairCall = -100 h)
//   fail-arm-all [interval]                   the same for every building of the world (scenario buildings, not only the test ones)
//   fail-report <label>                       histogram of RepairState, invariants of the broken state, and
//                                             the number of advances against the modelled chance
//   await/expect probes fail-stamped / fail-inspected: test buildings that have rolled / been called for inspection
//   fail-hold                                 pin RepairState/flags back to 0 so a round can be repeated
//   fail-lightning [n]                        BuildingApplyLightningDamage on every test building, n times (default 1): the wear
//                                             moves one step per strike without the random roll, the third strike breaks the building
//   fail-lightning-all [n]                   the same strike on every building of the world, test buildings or not (queues, guests, power)
//   fail-setbroken [all]                      BuildingSetBroken on every test building (a scripted `Broken` building), or with `all` on
//                                             every building of the world
//   fail-state <label>                        counts of RepairState 0..3, of broken buildings that are closed and WasBroken (`closed`) and of
//                                             broken ones that are not called for repair or have no fault (`bad`), as `key=number` for expect-last
// Each roll advances RepairState (0..2) by one step, broken buildings (3) stay, with chance min(90, FAIL_AGE * age + FAIL_PERCENT) percent.

#include "breakdowns.h"
#include "world_state.h"

#include "game/objects/object_building.h"
#include "core/log.h"
#include "test_util.h"
#include "game/objects/building_methods.h"
#include "game/world/world_control.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

namespace obl::test {
namespace {
std::vector<object::Building*> g_buildings;
std::vector<int> g_previous; // RepairState at the previous report/arm, to count the steps of one round

// Whole game hours since the start of the game, as Building::Update computes them.
int HoursNow()
{
    return static_cast<int>(World()->m_gameDayTime) + World()->m_gameDay * 24;
}

double Chance(const object::Building* building)
{
    const tables::BuildingDef* def = building->m_definition;
    int percent = def->m_FailAge * building->m_BuildingAge + def->m_FailPercent;
    return std::min(percent, 0x5a) / 100.0;
}
} // namespace

void ResetBreakdowns()
{
    g_buildings.clear();
    g_previous.clear();
}

bool ReadBreakdownProbe(const std::string& name, double& out)
{
    if ((name != "fail-stamped" && name != "fail-inspected") || !World()) return false;
    int count = 0;
    for (const auto* b : g_buildings) {
        if (name == "fail-stamped" ? b->m_LastFailTime >= HoursNow() - 1 : b->m_calledForInspection != 0) ++count;
    }
    out = count;
    return true;
}

bool ExecuteBreakdownCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name.rfind("fail-", 0) != 0) return false;
    if (!World()) {
        OBL_LOG("autotest: FAILED %s requires a world", name.c_str());
        return true;
    }
    if (name == "fail-allowed" && args.size() == 1) {
        world::SetBreakdownsAllowed(*World(), std::stoi(args[0]) != 0);
        OBL_LOG("autotest: fail-allowed %s", args[0].c_str());
    } else if (name == "fail-create" && args.size() == 5) {
        int count = std::stoi(args[3]);
        for (int i = 0; i < count; ++i) {
            auto building = SpawnBuilding(args[0], std::stoi(args[1]) + i % 10, std::stoi(args[2]) + i / 10, std::stoi(args[4]));
            if (!building) return true;
            object::CompleteBuildingStartup(*building);
            g_buildings.push_back(building);
        }
        g_previous.assign(g_buildings.size(), 0);
        OBL_LOG("autotest: fail-create %s x%d age=%s chance=%.2f allowed=%d hours=%d", args[0].c_str(), count, args[4].c_str(),
                g_buildings.empty() ? 0.0 : Chance(g_buildings.back()), int(World()->m_breakDownsAllowed), HoursNow());
    } else if (name == "fail-arm") {
        int interval = args.empty() ? 0 : std::stoi(args[0]);
        for (size_t i = 0; i < g_buildings.size(); ++i) {
            auto* b = g_buildings[i];
            g_previous[i] = static_cast<int>(b->m_RepairState);
            object::ArmBuildingFailure(*b, HoursNow(), interval);
        }
        OBL_LOG("autotest: fail-arm hours=%d interval=%d", HoursNow(), interval);
    } else if (name == "fail-arm-all") {
        int interval = args.empty() ? 0 : std::stoi(args[0]);
        object::Manager* manager = game::GetObjectManager();
        int armed = 0;
        for (uint32_t i = 0; manager && i < manager->m_buildings.size(); ++i) {
            auto* b = static_cast<object::Building*>(manager->m_buildings[i]);
            if (!b) continue;
            object::ArmBuildingFailure(*b, HoursNow(), interval);
            ++armed;
        }
        OBL_LOG("autotest: fail-arm-all hours=%d interval=%d buildings=%d", HoursNow(), interval, armed);
    } else if (name == "fail-hold") {
        for (size_t i = 0; i < g_buildings.size(); ++i) {
            auto* b = g_buildings[i];
            object::ResetBuildingWear(*b);
            g_previous[i] = 0;
        }
    } else if (name == "fail-lightning") {
        int times = args.empty() ? 1 : std::stoi(args[0]);
        for (int i = 0; i < times; ++i)
            for (auto* b : g_buildings) object::BuildingApplyLightningDamage(b);
        OBL_LOG("autotest: fail-lightning x%d on %zu buildings", times, g_buildings.size());
    } else if (name == "fail-lightning-all") {
        int times = args.empty() ? 1 : std::stoi(args[0]);
        object::Manager* manager = game::GetObjectManager();
        int struck = 0;
        for (int i = 0; i < times; ++i)
            for (uint32_t b = 0; manager && b < manager->m_buildings.size(); ++b)
                if (manager->m_buildings[b]) {
                    object::BuildingApplyLightningDamage(static_cast<object::Building*>(manager->m_buildings[b]));
                    ++struck;
                }
        OBL_LOG("autotest: fail-lightning-all x%d: %d strikes", times, struck);
    } else if (name == "fail-setbroken") {
        size_t count = 0;
        if (!args.empty() && args[0] == "all") {
            object::Manager* manager = game::GetObjectManager();
            for (uint32_t b = 0; manager && b < manager->m_buildings.size(); ++b)
                if (manager->m_buildings[b]) {
                    object::BuildingSetBroken(static_cast<object::Building*>(manager->m_buildings[b]));
                    ++count;
                }
        } else {
            for (auto* b : g_buildings) {
                object::BuildingSetBroken(b);
                ++count;
            }
        }
        OBL_LOG("autotest: fail-setbroken on %zu buildings", count);
    } else if (name == "fail-state") {
        std::array<int, 4> histogram{};
        int bad = 0, closed = 0;
        for (const auto* b : g_buildings) {
            int state = static_cast<int>(b->m_RepairState);
            if (state >= 0 && state < 4) ++histogram[state];
            // A broken building is called for repair and has a fault stamped; a strike also closes it and marks it WasBroken, SetBroken does not.
            if (state == 3 && !(b->m_calledForRepair && b->m_NumFaults >= 1)) ++bad;
            if (state == 3 && b->m_wasBroken && b->m_OpenStatus == 1) ++closed;
        }
        OBL_LOG("autotest: fail-state %s n=%zu s0=%d s1=%d s2=%d s3=%d closed=%d bad=%d", args.empty() ? "" : args[0].c_str(),
                g_buildings.size(), histogram[0], histogram[1], histogram[2], histogram[3], closed, bad);
    } else if (name == "fail-report") {
        std::array<int, 4> histogram{};
        int advanced = 0, badInvariant = 0, stamped = 0, inspection = 0, eligible = 0;
        for (size_t i = 0; i < g_buildings.size(); ++i) {
            const auto* b = g_buildings[i];
            int state = static_cast<int>(b->m_RepairState);
            if (state >= 0 && state < 4) ++histogram[state];
            if (g_previous[i] < 3) ++eligible; // a broken building (3) has no further step
            if (state == g_previous[i] + 1) ++advanced;
            if (b->m_LastFailTime >= HoursNow() - 1) ++stamped;
            if (b->m_calledForInspection) ++inspection;
            if (state == 3 && !(b->m_wasBroken && b->m_OpenStatus == 1 && b->m_calledForRepair && b->m_NumFaults >= 1)) ++badInvariant;
            if (state != 3 && state != g_previous[i] && state != g_previous[i] + 1) ++badInvariant;
        }
        double p = g_buildings.empty() ? 0 : Chance(g_buildings.back());
        double n = double(eligible);
        double sigma = std::sqrt(n * p * (1 - p));
        bool allowed = World()->m_breakDownsAllowed != 0;
        // A disabled model must not move anything; an enabled one advances about n*p buildings by one step.
        bool inspectionRound = !args.empty() && args[0] == "inspection"; // fail-arm <interval> ran before
        bool ok = !badInvariant && (!inspectionRound || inspection == int(g_buildings.size())) &&
                (allowed ? std::fabs(advanced - n * p) <= 4 * sigma + 1 : advanced == 0);
        OBL_LOG("autotest: %s fail-report %s n=%zu eligible=%d allowed=%d states=[%d %d %d %d] advanced=%d expected=%.1f+-%.1f "
                "stamped=%d inspection=%d badInvariant=%d",
                ok ? "PASS" : "FAILED", args.empty() ? "" : args[0].c_str(), g_buildings.size(), eligible, int(allowed), histogram[0],
                histogram[1], histogram[2], histogram[3], advanced, n * p, sigma, stamped, inspection, badInvariant);
    } else {
        return false;
    }
    return true;
}
} // namespace obl::test
