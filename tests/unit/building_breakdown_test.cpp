#include "byte_copy.h"
#include "unit_test.h"

#include "game/objects/building_breakdown.h"

#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;

OBL_TEST_CASE("building_breakdown.lightning_wears_a_working_building_step_by_step")
{
    OBL_CHECK(DecideLightning(0, 0).step == LightningStep::Wear);
    OBL_CHECK_EQ(DecideLightning(0, 0).newRepairState, 1);
    OBL_CHECK(DecideLightning(0, 1).step == LightningStep::Wear);
    OBL_CHECK_EQ(DecideLightning(0, 1).newRepairState, 2);
    OBL_CHECK(DecideLightning(0, 2).step == LightningStep::Break);
    OBL_CHECK_EQ(DecideLightning(0, 2).newRepairState, 3);
}

OBL_TEST_CASE("building_breakdown.lightning_leaves_a_broken_building_and_other_states_alone")
{
    OBL_CHECK(DecideLightning(0, 3).step == LightningStep::None);
    OBL_CHECK(DecideLightning(0, 4).step == LightningStep::None);
    for (uint32_t state : {1u, 2u, 3u, 4u, 9u}) OBL_CHECK(DecideLightning(state, 0).step == LightningStep::None);
    OBL_CHECK_EQ(DecideLightning(4, 1).newRepairState, 1);
}

OBL_TEST_CASE("building_breakdown.a_negative_wear_takes_the_breaking_branch_like_the_signed_compare")
{
    OBL_CHECK(DecideLightning(0, 0xffffffffu).step == LightningStep::Break);
    OBL_CHECK_EQ(DecideLightning(0, 0xffffffffu).newRepairState, 3);
    // above 3 as an unsigned number is not above 3 as a signed one only when the top bit is set
    OBL_CHECK(DecideLightning(0, 0x7fffffffu).step == LightningStep::None);
}

// ---- the wear step of Building::Update ----------------------------------------------------------------------------

namespace {

// A building with its definition, and an Env that serves rand() from a tape and records the calls in order, with what the building showed at
// each call.
struct Wear {
    std::unique_ptr<tables::BuildingDef> def = std::make_unique<tables::BuildingDef>();
    std::unique_ptr<object::Building> building = std::make_unique<object::Building>();
    std::vector<int> tape;
    size_t drawn = 0;
    std::vector<std::string> calls;
    std::vector<std::unique_ptr<object::Building>> seen; // the building at each call

    Wear()
    {
        def->m_AvFailTime = 4.0f;
        def->m_FailPercent = 10;
        def->m_FailAge = 5;
        building->m_definition = def.get();
        building->m_BuildingAge = 3; // chance 5 * 3 + 10 = 25
        building->m_UserRepairInterval = 8;
        building->m_LastRepairDay = 0;
        building->m_lastRepairDayTime = -1.0f;
    }
    void Note(const char* call)
    {
        calls.push_back(call);
        obl::test::ByteCopy(*seen.emplace_back(std::make_unique<object::Building>()), *building);
    }
    int Rand()
    {
        Note("rand");
        return tape.at(drawn++);
    }
    void RequestInspection() { Note("inspect"); }
    void ClearQueueForBreakdown() { Note("clear queue"); }
    void HideStaff() { Note("hide"); }
    void NotifyBroken() { Note("notify"); }
    void RequestRepair() { Note("repair"); }
    WearReport Run(int32_t day, float time, bool allowed = true) { return UpdateWear(*this, *building, day, time, allowed); }
};

} // namespace

OBL_TEST_CASE("building_breakdown.wear_clock_counts_whole_hours")
{
    OBL_CHECK_EQ(WearClock(2, 13.9f), 61);
    OBL_CHECK_EQ(WearClock(0, 0.99f), 0);
    OBL_CHECK_EQ(WearClock(0, -0.5f), 0); // truncates toward zero
    OBL_CHECK_EQ(WearClock(0x0aaaaaab, 0.0f), static_cast<int32_t>(0x0aaaaaabu * 24u));
}

OBL_TEST_CASE("building_breakdown.wear_needs_a_fail_percent_and_breakdowns_allowed")
{
    Wear w;
    w.building->m_LastFailTime = -100;
    w.building->m_RepairInterval = 1;
    WearReport off = w.Run(5, 12.0f, false);
    OBL_CHECK(!off.active);
    w.def->m_FailPercent = 0;
    WearReport none = w.Run(5, 12.0f);
    OBL_CHECK(!none.active);
    OBL_CHECK(w.calls.empty());
    OBL_CHECK_EQ(w.building->m_LastFailTime, -100);
}

OBL_TEST_CASE("building_breakdown.wear_rolls_every_avfailtime_hours")
{
    Wear w;
    w.tape = {99, 99, 99};
    w.building->m_LastFailTime = 100 - 3;
    OBL_CHECK(!w.Run(4, 4.5f).rolled); // now = 100, 3 hours
    OBL_CHECK_EQ(w.drawn, size_t(0));
    w.building->m_LastFailTime = 100 - 4;
    WearReport report = w.Run(4, 4.5f);
    OBL_CHECK(report.rolled);
    OBL_CHECK_EQ(report.now, 100);
    OBL_CHECK_EQ(w.building->m_LastFailTime, 100);
    OBL_CHECK_EQ(report.roll, 99);
    // A stamp in the future is a huge unsigned distance: it rolls.
    w.building->m_LastFailTime = 200;
    OBL_CHECK(w.Run(4, 4.5f).rolled);
    // A fractional AVFAILTIME needs the next whole hour; NaN never rolls.
    w.def->m_AvFailTime = 2.5f;
    w.building->m_LastFailTime = 98;
    OBL_CHECK(!w.Run(4, 4.5f).rolled);
    w.building->m_LastFailTime = 97;
    OBL_CHECK(w.Run(4, 4.5f).rolled);
    w.def->m_AvFailTime = std::numeric_limits<float>::quiet_NaN();
    w.building->m_LastFailTime = 0;
    OBL_CHECK(!w.Run(4, 4.5f).rolled);
    OBL_CHECK_EQ(w.drawn, size_t(3));
}

OBL_TEST_CASE("building_breakdown.wear_chance_is_capped_at_90_unsigned")
{
    Wear w;
    w.tape = {124, 125, 1089, 1090, 77};
    auto roll = [&] {
        w.building->m_LastFailTime = -1000;
        w.building->m_RepairState = 0;
        return w.Run(0, 0.0f);
    };
    WearReport a = roll();
    OBL_CHECK_EQ(a.chance, 25u);
    OBL_CHECK(a.step == LightningStep::Wear); // 24 < 25
    OBL_CHECK(roll().step == LightningStep::None); // 25
    w.building->m_BuildingAge = 1000;
    WearReport c = roll();
    OBL_CHECK_EQ(c.chance, 90u);
    OBL_CHECK(c.step == LightningStep::Wear); // 89 < 90
    OBL_CHECK(roll().step == LightningStep::None); // 90
    w.building->m_BuildingAge = 0;
    w.def->m_FailPercent = -5; // a negative sum is a large unsigned one
    OBL_CHECK_EQ(roll().chance, 90u);
}

OBL_TEST_CASE("building_breakdown.wear_steps_and_the_break_sequence")
{
    Wear w;
    w.tape = {0, 0, 0, 0};
    w.building->m_NumFaults = 2;
    for (uint32_t expected : {1u, 2u}) {
        w.building->m_LastFailTime = 0;
        OBL_CHECK(w.Run(1, 7.25f).step == LightningStep::Wear);
        OBL_CHECK_EQ(w.building->m_RepairState, expected);
    }
    w.calls.clear();
    w.seen.clear();
    w.building->m_LastFailTime = 0;
    WearReport report = w.Run(1, 7.25f);
    OBL_CHECK(report.step == LightningStep::Break);
    OBL_CHECK(w.calls == std::vector<std::string>({"rand", "clear queue", "hide", "notify", "repair"}));
    // The queue and the staff see the broken, closed building, the fault not stamped yet; the notice sees the stamp.
    OBL_CHECK_EQ(w.seen[1]->m_RepairState, 3u);
    OBL_CHECK_EQ(w.seen[1]->m_wasBroken, 1);
    OBL_CHECK_EQ(w.seen[1]->m_OpenStatus, 1u);
    OBL_CHECK_EQ(w.seen[2]->m_NumFaults, 2);
    OBL_CHECK_EQ(w.seen[3]->m_NumFaults, 3);
    OBL_CHECK_EQ(w.seen[3]->m_LastFaultDay, 1);
    OBL_CHECK_EQ(w.seen[3]->m_lastFaultDayTime, 7.25f);
    OBL_CHECK_EQ(w.seen[4]->m_calledForRepair, 0);
    OBL_CHECK_EQ(w.building->m_calledForRepair, 1);
    // Broken: a successful roll changes nothing but the roll stamp.
    w.calls.clear();
    w.building->m_LastFailTime = 0;
    OBL_CHECK(w.Run(2, 1.0f).step == LightningStep::None);
    OBL_CHECK(w.calls == std::vector<std::string>({"rand"}));
    OBL_CHECK_EQ(w.building->m_LastFailTime, 49);
}

OBL_TEST_CASE("building_breakdown.wear_break_with_a_repair_already_called")
{
    Wear w;
    w.tape = {0};
    w.building->m_RepairState = 2;
    w.building->m_calledForRepair = 1;
    w.building->m_LastFailTime = 0;
    w.Run(1, 0.0f);
    OBL_CHECK(w.calls == std::vector<std::string>({"rand", "clear queue", "hide", "notify"}));
}

OBL_TEST_CASE("building_breakdown.wear_other_values_never_step")
{
    Wear w;
    w.tape = {0, 0};
    for (uint32_t state : {4u, 0xffffffffu}) {
        w.building->m_RepairState = state;
        w.building->m_LastFailTime = 0;
        OBL_CHECK(w.Run(1, 0.0f).step == LightningStep::None);
        OBL_CHECK_EQ(w.building->m_RepairState, state);
    }
}

OBL_TEST_CASE("building_breakdown.inspection_is_due_after_the_interval_since_the_call_and_the_repair")
{
    Wear w;
    w.tape = {99};
    w.building->m_LastFailTime = 99; // 1 hour: no roll
    w.building->m_RepairInterval = 6;
    w.building->m_LastRepairCall = 100 - 6;
    WearReport report = w.Run(4, 4.0f); // now = 100; 101 hours since the repair stamp (day 0, -1 h)
    OBL_CHECK(report.inspection);
    OBL_CHECK(!report.rolled);
    OBL_CHECK(w.calls == std::vector<std::string>({"inspect"}));
    OBL_CHECK_EQ(w.seen[0]->m_RepairInterval, 8); // the user's interval is set before the job is asked for ...
    OBL_CHECK_EQ(w.seen[0]->m_LastRepairCall, 94); // ... the call is stamped after
    OBL_CHECK_EQ(w.building->m_LastRepairCall, 100);
    OBL_CHECK_EQ(w.building->m_calledForInspection, 1);
    OBL_CHECK_EQ(w.building->m_RepairInterval, 8);
    // The inspection comes before the roll in the same call.
    Wear both;
    both.tape = {99};
    both.building->m_LastFailTime = 0;
    both.building->m_RepairInterval = 6;
    both.building->m_LastRepairCall = 0;
    both.Run(4, 4.0f);
    OBL_CHECK(both.calls == std::vector<std::string>({"inspect", "rand"}));
}

OBL_TEST_CASE("building_breakdown.inspection_is_not_due")
{
    auto due = [](auto setup) {
        Wear w;
        w.building->m_LastFailTime = 100; // no roll
        w.building->m_RepairInterval = 6;
        w.building->m_LastRepairCall = 94;
        setup(w);
        return w.Run(4, 4.0f).inspection;
    };
    OBL_CHECK(due([](Wear&) {}));
    OBL_CHECK(!due([](Wear& w) { w.building->m_RepairInterval = 0; }));
    OBL_CHECK(!due([](Wear& w) { w.building->m_calledForInspection = 1; }));
    OBL_CHECK(!due([](Wear& w) { w.building->m_LastRepairCall = 95; }));
    OBL_CHECK(due([](Wear& w) { w.building->m_LastRepairCall = 101; })); // unsigned: a call stamped in the future counts as long ago
    // The repair at day 4, 0.5 h -> 4 hours ago (the time is truncated): not yet; at day 3, 22.9 h -> 6 hours ago: due.
    OBL_CHECK(!due([](Wear& w) {
        w.building->m_LastRepairDay = 4;
        w.building->m_lastRepairDayTime = 0.5f;
    }));
    OBL_CHECK(due([](Wear& w) {
        w.building->m_LastRepairDay = 3;
        w.building->m_lastRepairDayTime = 22.9f;
    }));
    // Signed: a repair stamped in the future is never long enough ago.
    OBL_CHECK(!due([](Wear& w) { w.building->m_LastRepairDay = 10; }));
}
