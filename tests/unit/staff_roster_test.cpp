#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/world/world_state.h"
#include "unit_test.h"

#include "game/objects/staff_wages.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

using namespace obl;
using namespace obl::world_data;
using namespace obl::tables;

namespace {

struct Fixture {
    std::unique_ptr<object::Staff> staff = std::make_unique<object::Staff>();
    std::unique_ptr<StaffDef> def = std::make_unique<StaffDef>();
    std::unique_ptr<World> world = std::make_unique<World>();
    std::unique_ptr<Player> player = std::make_unique<Player>();

    Fixture()
    {
        obl::unit::FillObject<object::Staff>(*staff);
        staff->m_staffDefinition = def.get();
        staff->m_Wage = 20;
        staff->m_SalaryExpectation = 40.0f;
        world->m_player.reset(player.get());
    }
};

float Nan()
{
    return std::numeric_limits<float>::quiet_NaN();
}

} // namespace

OBL_TEST_CASE("staff_roster.target_is_three_quarters_of_wage_over_expectation")
{
    Fixture f;
    staff::RecalculateHappinessTarget(*f.staff); // 20 / 40 * 0.75
    OBL_CHECK_EQ(f.staff->m_targetHappiness, 0.375f);
    OBL_CHECK_EQ(f.staff->m_happinessChanging, 1);
    OBL_CHECK_EQ(f.staff->m_happinessRate, (0.375f - 0.0f) * 0.5f);
    f.staff->m_happiness = 0.875f;
    staff::RecalculateHappinessTarget(*f.staff);
    OBL_CHECK_EQ(f.staff->m_happinessRate, (0.375f - 0.875f) * 0.5f);
}

OBL_TEST_CASE("staff_roster.target_is_clamped_to_unit_range")
{
    Fixture f;
    f.staff->m_Wage = 100; // 100 / 40 * 0.75 > 1
    staff::RecalculateHappinessTarget(*f.staff);
    OBL_CHECK_EQ(f.staff->m_targetHappiness, 1.0f);
    f.staff->m_SalaryExpectation = -10.0f; // negative ratio
    staff::RecalculateHappinessTarget(*f.staff);
    OBL_CHECK_EQ(f.staff->m_targetHappiness, 0.0f);
}

OBL_TEST_CASE("staff_roster.zero_over_zero_is_nan_and_clamps_to_zero_but_wage_over_zero_to_one")
{
    Fixture f;
    f.staff->m_Wage = 0;
    f.staff->m_SalaryExpectation = 0.0f; // 0 / 0 = NaN: not greater than 1, but below 0 for the lower clamp
    staff::RecalculateHappinessTarget(*f.staff);
    OBL_CHECK_EQ(f.staff->m_targetHappiness, 0.0f);
    f.staff->m_Wage = 5;
    staff::RecalculateHappinessTarget(*f.staff); // 5 / 0 = +inf
    OBL_CHECK_EQ(f.staff->m_targetHappiness, 1.0f);
    f.staff->m_SalaryExpectation = Nan(); // NaN expectation: NaN ratio again
    staff::RecalculateHappinessTarget(*f.staff);
    OBL_CHECK_EQ(f.staff->m_targetHappiness, 0.0f);
}

OBL_TEST_CASE("staff_roster.wage_is_unsigned")
{
    Fixture f;
    f.staff->m_Wage = -1; // 0xffffffff, a huge wage, not a negative one
    f.staff->m_SalaryExpectation = 40.0f;
    staff::RecalculateHappinessTarget(*f.staff);
    OBL_CHECK_EQ(f.staff->m_targetHappiness, 1.0f);
}

OBL_TEST_CASE("staff_roster.overtime_over_ten_hours_lowers_the_target")
{
    Fixture f;
    f.staff->m_Wage = 40; // ratio 0.75
    for (uint32_t hours : {0u, 10u}) {
        f.staff->m_hoursWorkedInRow = hours;
        staff::RecalculateHappinessTarget(*f.staff);
        OBL_CHECK_EQ(f.staff->m_targetHappiness, 0.75f);
    }
    f.staff->m_hoursWorkedInRow = 12; // 2 h over: minus 2 * 0.1f
    staff::RecalculateHappinessTarget(*f.staff);
    OBL_CHECK_EQ(f.staff->m_targetHappiness, static_cast<float>(0.75 - 2.0 * static_cast<double>(0.1f)));
    f.staff->m_hoursWorkedInRow = 40; // far below zero: clamped again
    staff::RecalculateHappinessTarget(*f.staff);
    OBL_CHECK_EQ(f.staff->m_targetHappiness, 0.0f);
    f.staff->m_hoursWorkedInRow = 0xfffffff0u; // read signed: negative overtime is no overtime
    staff::RecalculateHappinessTarget(*f.staff);
    OBL_CHECK_EQ(f.staff->m_targetHappiness, 0.75f);
}

OBL_TEST_CASE("staff_roster.increment_wraps_24_to_zero_and_only_24")
{
    Fixture f;
    f.staff->m_StartTime = 8;
    f.staff->m_EndTime = 23;
    staff::IncrementShiftHours(*f.staff);
    OBL_CHECK_EQ(f.staff->m_StartTime, 9);
    OBL_CHECK_EQ(f.staff->m_EndTime, 0); // 24
    f.staff->m_StartTime = 24; // out of the GUI range: 25 stays
    f.staff->m_EndTime = -1; // 0xffffffff + 1 wraps to 0 by itself
    staff::IncrementShiftHours(*f.staff);
    OBL_CHECK_EQ(f.staff->m_StartTime, 25);
    OBL_CHECK_EQ(f.staff->m_EndTime, 0);
}

OBL_TEST_CASE("staff_roster.decrement_wraps_below_zero_to_23_and_garbage_above_30_too")
{
    Fixture f;
    f.staff->m_StartTime = 8;
    f.staff->m_EndTime = 0;
    staff::DecrementShiftHours(*f.staff);
    OBL_CHECK_EQ(f.staff->m_StartTime, 7);
    OBL_CHECK_EQ(f.staff->m_EndTime, 23); // 0 - 1 = 0xffffffff > 0x1e
    f.staff->m_StartTime = 31; // 30: not above 0x1e, stays
    f.staff->m_EndTime = 32; // 31 > 0x1e
    staff::DecrementShiftHours(*f.staff);
    OBL_CHECK_EQ(f.staff->m_StartTime, 30);
    OBL_CHECK_EQ(f.staff->m_EndTime, 23);
}

OBL_TEST_CASE("staff_roster.sack_takes_ten_times_the_recommended_wage_from_cash_and_adds_it_to_the_costs")
{
    Fixture f;
    f.def->m_RecommendedWage = 25;
    f.player->m_cash = 1000.0f;
    f.world->m_costsToday = 7.0f;
    f.world->m_wagesCostToday = 3.0f;
    staff::Sack(*f.staff, *f.world);
    OBL_CHECK_EQ(f.staff->m_gotTheSack, 1);
    OBL_CHECK_EQ(f.player->m_cash, 750.0f);
    OBL_CHECK_EQ(f.world->m_costsToday, 257.0f);
    OBL_CHECK_EQ(f.world->m_wagesCostToday, 253.0f);
}

OBL_TEST_CASE("staff_roster.sack_severance_wraps_at_32_bits_and_cash_reads_it_signed")
{
    Fixture f;
    f.def->m_RecommendedWage = 0x1999999a; // x 10 = 0x100000004: wraps to 4
    f.player->m_cash = 10.0f;
    staff::Sack(*f.staff, *f.world);
    OBL_CHECK_EQ(f.player->m_cash, 6.0f);
    OBL_CHECK_EQ(f.world->m_costsToday, 4.0f);

    Fixture g;
    g.def->m_RecommendedWage = 429496729; // x 10 = 4294967290 = -6 as a signed int: the cash grows, the costs take the unsigned value
    g.player->m_cash = 100.0f;
    staff::Sack(*g.staff, *g.world);
    OBL_CHECK_EQ(g.player->m_cash, 106.0f);
    OBL_CHECK_EQ(g.world->m_costsToday, static_cast<float>(4294967290.0)); // float spacing at 2^32 is 512
}
