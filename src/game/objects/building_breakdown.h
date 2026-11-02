#pragma once

#include "core/float_to_int.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>

// The wear of a Building (building breakdowns, docs/architecture/objects.md): m_RepairState is 0 sound,
// 1 and 2 damaged (the building still works), 3 broken. Two methods move it without the random roll of the building's Update:
// ApplyLightningDamage and SetBroken; the roll and the preventive inspection are the first
// part of the building's Update in states 0 and 4 (UpdateWear below). No game memory here: the glue in src/game/objects/building_methods.cpp does the
// calls to the managers, this file holds the decisions (and the field writes of UpdateWear, on the building given to it).
namespace obl {

// What a lightning strike does to a building in `buildingState` (m_state) with the wear `repairState`. The wear is compared as
// a signed int: a negative value (never stored by the game) takes the last branch.
enum class LightningStep : uint8_t {
    None, // not in STATE_NULL, or already broken: nothing changes
    Wear, // 0 -> 1 or 1 -> 2
    Break, // 2 (or below 0) -> 3: the fault is stamped, the queue is cleared for the breakdown, the building closes and the notice goes out
};

struct LightningDecision {
    LightningStep step;
    int32_t newRepairState; // the wear after the step (unchanged for None)
};

inline LightningDecision DecideLightning(uint32_t buildingState, uint32_t repairState)
{
    const int32_t wear = static_cast<int32_t>(repairState);
    if (buildingState != 0 || wear >= 3) return {LightningStep::None, wear};
    if (wear == 0) return {LightningStep::Wear, 1};
    if (wear == 1) return {LightningStep::Wear, 2};
    return {LightningStep::Break, 3};
}

// ---- the wear step of the building's Update (states 0 and 4) ---------------------------------------------------------------

// The clock of the wear model: whole game hours since day 0, `int(dayTime) + day * 24` in 32-bit arithmetic.
inline int32_t WearClock(int32_t day, float dayTime)
{
    return static_cast<int32_t>(static_cast<uint32_t>(FloatToIntWrap(dayTime)) + static_cast<uint32_t>(day) * 24u);
}

// What one UpdateWear call did (for the branch counters and the unit tests; the building fields carry the result).
struct WearReport {
    bool active = false; // FAIL_PERCENT != 0 and breakdowns allowed: the clock was read
    bool inspection = false; // a preventive inspection was requested
    bool rolled = false; // AVFAILTIME hours had passed: rand() was drawn
    int32_t now = 0;
    uint32_t chance = 0; // the percentage the roll was made against (0..90)
    int32_t roll = -1; // rand() % 100, -1 without a roll
    LightningStep step = LightningStep::None; // Wear: 0 -> 1 or 1 -> 2; Break: 2 -> 3
};

// The first part of the building's Update in STATE_NULL and STATE_OCCUPIED, on the building's own fields
// (building breakdowns, docs/architecture/objects.md). The Env gives what the step asks of the game, in call order (tests pin it):
// int Rand the game's rand (import)
//   void RequestInspection()        repair::repair_facade::AddJob(repair queue, id, 0)
//   void ClearQueueForBreakdown()   ClearForBreakdown of m_queue - unlike the lightning path the queue is not tested first
//   void HideStaff()                HideStaffAndEvents
//   void NotifyBroken()             feedback::Feedback::AddText(feedback queue, FEEDBACK_BUILDING_BROKEN with BUILDING_NAME_<key>, id, 4)
//   void RequestRepair()            repair::repair_facade::AddJob(repair queue, id, 2)
// `day`/`dayTime` are the world's game day and day time and `breakdownsAllowed` is m_breakDownsAllowed; the model runs only when the definition has
// FAIL_PERCENT != 0 as well. The fields are written before every call that follows them, so a helper (or the cascade of the
// messages it sends) already sees the updated building.
template<class Env> WearReport UpdateWear(Env& env, object::Building& building, int32_t day, float dayTime, bool breakdownsAllowed)
{
    WearReport report;
    const tables::BuildingDef& def = *building.m_definition;
    if (def.m_FailPercent == 0 || !breakdownsAllowed) return report;
    report.active = true;
    const int32_t now = WearClock(day, dayTime);
    report.now = now;

    // Preventive inspection: m_RepairInterval hours after the last call for one (unsigned) and after the last repair (signed; the repair
    // stamp starts at day 0, -1.0 h), unless one is pending.
    const int32_t interval = building.m_RepairInterval;
    if (interval != 0 && !building.m_calledForInspection &&
            static_cast<uint32_t>(now - building.m_LastRepairCall) >= static_cast<uint32_t>(interval) &&
            static_cast<int32_t>(static_cast<uint32_t>(now) - static_cast<uint32_t>(building.m_LastRepairDay) * 24u -
                    static_cast<uint32_t>(FloatToIntWrap(building.m_lastRepairDayTime))) >= interval) {
        building.m_RepairInterval = building.m_UserRepairInterval;
        env.RequestInspection();
        building.m_LastRepairCall = now;
        building.m_calledForInspection = 1;
        report.inspection = true;
    }

    // The roll every AVFAILTIME hours: the elapsed hours (unsigned, exact in a double) against the float; a NaN AVFAILTIME never rolls.
    const uint32_t elapsed = static_cast<uint32_t>(now - building.m_LastFailTime);
    if (!(static_cast<double>(elapsed) >= static_cast<double>(def.m_AvFailTime))) return report;
    building.m_LastFailTime = now;
    uint32_t chance =
            static_cast<uint32_t>(def.m_FailAge) * static_cast<uint32_t>(building.m_BuildingAge) + static_cast<uint32_t>(def.m_FailPercent);
    if (chance > 90) // unsigned: a negative sum is capped too
        chance = 90;
    report.rolled = true;
    report.chance = chance;
    report.roll = env.Rand() % 100;
    if (report.roll >= static_cast<int32_t>(chance)) return report;
    switch (building.m_RepairState) {
    case 0:
        building.m_RepairState = 1;
        report.step = LightningStep::Wear;
        return report;
    case 1:
        building.m_RepairState = 2;
        report.step = LightningStep::Wear;
        return report;
    case 2: break;
    default: // broken (or a value the game never stores): nothing
        return report;
    }
    // Broken: this order differs from the lightning path (the fault is stamped after the staff are hidden here).
    report.step = LightningStep::Break;
    building.m_RepairState = 3;
    building.m_wasBroken = 1;
    building.m_OpenStatus = 1;
    env.ClearQueueForBreakdown();
    env.HideStaff();
    building.m_lastFaultDayTime = dayTime;
    building.m_LastFaultDay = day;
    ++building.m_NumFaults;
    env.NotifyBroken();
    if (!building.m_calledForRepair) {
        env.RequestRepair();
        building.m_calledForRepair = 1;
    }
    return report;
}

} // namespace obl
