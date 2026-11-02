#include "unit_test.h"

#include "game/objects/building_open_state.h"

using namespace obl;

namespace {

constexpr OpeningHours kAllHours{0.0f, 0.0f};
constexpr OpeningHours kDay{8.0f, 20.0f}; // plain window
constexpr OpeningHours kNight{20.0f, 6.0f}; // wraps midnight

OpenStateInput Input(OpeningHours hours, uint32_t hour, uint32_t openStatus = 0)
{
    OpenStateInput in;
    in.hours = hours;
    in.hour = hour;
    in.openStatus = openStatus;
    return in;
}

} // namespace

OBL_TEST_CASE("building_open_state.whole_hour_is_floor_then_truncate")
{
    OBL_CHECK_EQ(WholeHour(0.0f), 0);
    OBL_CHECK_EQ(WholeHour(7.999f), 7);
    OBL_CHECK_EQ(WholeHour(8.0f), 8);
    OBL_CHECK_EQ(WholeHour(23.9999f), 23);
    OBL_CHECK_EQ(WholeHour(-0.5f), -1); // floor first: -0.5 -> -1, not truncated to 0
    OBL_CHECK_EQ(WholeHour(3.0e10f), INT32_MIN);
}

OBL_TEST_CASE("building_open_state.closed_during_hours_needs_a_forced_closure")
{
    // Open by the clock but nobody closed it: not "closed during the hours".
    OBL_CHECK(!ClosedByUserOrPowerDuringHours(false, false, 12.0f, kDay));
    OBL_CHECK(ClosedByUserOrPowerDuringHours(true, false, 12.0f, kDay));
    OBL_CHECK(ClosedByUserOrPowerDuringHours(false, true, 12.0f, kDay));
    OBL_CHECK(ClosedByUserOrPowerDuringHours(true, true, 12.0f, kDay));
}

OBL_TEST_CASE("building_open_state.closed_during_hours_window_includes_both_ends")
{
    OBL_CHECK(ClosedByUserOrPowerDuringHours(true, false, 8.0f, kDay));
    OBL_CHECK(ClosedByUserOrPowerDuringHours(true, false, 20.0f, kDay));
    OBL_CHECK(!ClosedByUserOrPowerDuringHours(true, false, 7.99f, kDay));
    OBL_CHECK(!ClosedByUserOrPowerDuringHours(true, false, 20.01f, kDay));
    OBL_CHECK(!ClosedByUserOrPowerDuringHours(true, false, 0.0f, kDay));
}

OBL_TEST_CASE("building_open_state.closed_during_hours_wrapping_and_round_the_clock")
{
    // 20..6: the hours are everything except the gap (6, 20), whose ends belong to the hours.
    OBL_CHECK(ClosedByUserOrPowerDuringHours(true, false, 23.0f, kNight));
    OBL_CHECK(ClosedByUserOrPowerDuringHours(true, false, 2.0f, kNight));
    OBL_CHECK(ClosedByUserOrPowerDuringHours(true, false, 6.0f, kNight));
    OBL_CHECK(ClosedByUserOrPowerDuringHours(true, false, 20.0f, kNight));
    OBL_CHECK(!ClosedByUserOrPowerDuringHours(true, false, 6.01f, kNight));
    OBL_CHECK(!ClosedByUserOrPowerDuringHours(true, false, 12.0f, kNight));
    OBL_CHECK(!ClosedByUserOrPowerDuringHours(true, false, 19.99f, kNight));
    // equal ends: always
    for (float t : {0.0f, 5.5f, 12.0f, 23.99f}) OBL_CHECK(ClosedByUserOrPowerDuringHours(false, true, t, kAllHours));
}

OBL_TEST_CASE("building_open_state.power_output_of_a_consumer_and_a_closed_generator")
{
    OBL_CHECK_EQ(PowerOutput(1000, 0, 0), 1000);
    OBL_CHECK_EQ(PowerOutput(0, 1, 3), 0);
    OBL_CHECK_EQ(PowerOutput(250, 1, 0), 250); // a consumer is not zeroed by being closed
    OBL_CHECK_EQ(PowerOutput(-7500, 1, 0), 0);
}

OBL_TEST_CASE("building_open_state.generator_output_falls_with_the_wear")
{
    OBL_CHECK_EQ(PowerOutput(-7500, 0, 0), 7500);
    OBL_CHECK_EQ(PowerOutput(-7500, 0, 1), 5625);
    OBL_CHECK_EQ(PowerOutput(-7500, 0, 2), 3750);
    OBL_CHECK_EQ(PowerOutput(-7500, 0, 3), 1875);
    OBL_CHECK_EQ(PowerOutput(-7500, 0, 4), 7500); // outside the table: |POWER|
    OBL_CHECK_EQ(PowerOutput(-5, 0, 1), 3); // 3.75 truncated
    OBL_CHECK_EQ(PowerOutput(-5, 0, 3), 1); // 1.25 truncated
}

OBL_TEST_CASE("building_open_state.power_closed_building_returns_when_output_covers_it")
{
    OpenStateInput in = Input(kDay, 3);
    in.closedForPower = true;
    in.power = 800;
    in.powerDemand = 2000.0f;
    in.currentPower = 2801.0f;
    OBL_CHECK(DecideOpenState(in) == OpenStateStep::ClearPowerClosed);
    in.currentPower = 2800.0f; // equal is not enough
    OBL_CHECK(DecideOpenState(in) == OpenStateStep::None);
    in.currentPower = 100.0f;
    OBL_CHECK(DecideOpenState(in) == OpenStateStep::None);
    // The hours are not looked at while the building is shed (even a closed-by-clock building comes back first).
    in.hours = kDay;
    in.hour = 3;
    in.currentPower = 5000.0f;
    OBL_CHECK(DecideOpenState(in) == OpenStateStep::ClearPowerClosed);
}

OBL_TEST_CASE("building_open_state.player_closure_and_a_broken_building_are_left_alone")
{
    OpenStateInput in = Input(kDay, 12);
    in.userClosed = true;
    OBL_CHECK(DecideOpenState(in) == OpenStateStep::None);
    in = Input(kDay, 12, 1);
    in.repairState = 3;
    OBL_CHECK(DecideOpenState(in) == OpenStateStep::None);
    in.repairState = 2; // damaged still follows the clock
    OBL_CHECK(DecideOpenState(in) == OpenStateStep::Open);
}

OBL_TEST_CASE("building_open_state.plain_hours_open_every_frame_and_close_once")
{
    // 8..20: inside (8 <= h < 20) always asks to open, even when already open.
    for (uint32_t hour : {8u, 12u, 19u}) OBL_CHECK(DecideOpenState(Input(kDay, hour, 0)) == OpenStateStep::Open);
    for (uint32_t hour : {8u, 12u, 19u}) OBL_CHECK(DecideOpenState(Input(kDay, hour, 1)) == OpenStateStep::Open);
    // outside: closes only an open building
    for (uint32_t hour : {0u, 7u, 20u, 23u}) {
        OBL_CHECK(DecideOpenState(Input(kDay, hour, 0)) == OpenStateStep::Close);
        OBL_CHECK(DecideOpenState(Input(kDay, hour, 1)) == OpenStateStep::None);
    }
}

OBL_TEST_CASE("building_open_state.wrapping_hours_open_in_the_evening_and_the_morning")
{
    // 20..6: open for h >= 20 and h < 6; closed for 6 <= h < 20.
    for (uint32_t hour : {20u, 23u, 0u, 5u}) OBL_CHECK(DecideOpenState(Input(kNight, hour, 0)) == OpenStateStep::Open);
    for (uint32_t hour : {6u, 12u, 19u}) {
        OBL_CHECK(DecideOpenState(Input(kNight, hour, 0)) == OpenStateStep::Close);
        OBL_CHECK(DecideOpenState(Input(kNight, hour, 1)) == OpenStateStep::None);
    }
}

OBL_TEST_CASE("building_open_state.round_the_clock_always_opens")
{
    for (uint32_t hour = 0; hour < 24; ++hour) {
        OBL_CHECK(DecideOpenState(Input(kAllHours, hour, 0)) == OpenStateStep::Open);
        OBL_CHECK(DecideOpenState(Input(kAllHours, hour, 1)) == OpenStateStep::Open);
    }
    constexpr OpeningHours kSame{9.0f, 9.0f}; // equal ends that are not 0
    OBL_CHECK(DecideOpenState(Input(kSame, 3, 1)) == OpenStateStep::Open);
}

OBL_TEST_CASE("building_open_state.fractional_ends_compare_against_the_whole_hour")
{
    constexpr OpeningHours kHalf{9.5f, 17.5f};
    // The hour is the whole hour: 9 < 9.5 is outside, 17 < 17.5 is inside.
    OBL_CHECK(DecideOpenState(Input(kHalf, 9, 0)) == OpenStateStep::Close);
    OBL_CHECK(DecideOpenState(Input(kHalf, 10, 0)) == OpenStateStep::Open);
    OBL_CHECK(DecideOpenState(Input(kHalf, 17, 0)) == OpenStateStep::Open);
    OBL_CHECK(DecideOpenState(Input(kHalf, 18, 0)) == OpenStateStep::Close);
}
