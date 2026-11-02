#include "unit_test.h"
#include "game/objects/guest_lifecycle.h"

#include <cmath>
#include <limits>

using namespace obl;
using namespace obl::guest_lifecycle;

namespace {

// a < b; a NaN on either side counts as true.
bool BelowOrNan(float a, float b)
{
    return std::isnan(a) || std::isnan(b) || a < b;
}

// a <= b; a NaN on either side counts as true.
bool AtMostOrNan(float a, float b)
{
    return std::isnan(a) || std::isnan(b) || a <= b;
}

// Step by step, one branch per condition.
bool ExpectedBedTime(float wake, float bed, float hour, int32_t day)
{
    if (BelowOrNan(bed, wake)) {
        if (!AtMostOrNan(hour, bed)) {
            if (BelowOrNan(hour, wake)) {
                if (day != 0) return true;
            }
        }
    }
    if (AtMostOrNan(bed, wake)) return false;
    if (!AtMostOrNan(hour, bed)) return true;
    if (!BelowOrNan(hour, wake)) return false;
    return day != 0;
}

} // namespace

OBL_TEST_CASE("guest_lifecycle.bed_time_matches_the_expected_answer_incl_nan")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float values[] = {0.0f, 0.5f, 3.0f, 7.0f, 7.5f, 8.0f, 12.0f, 22.0f, 23.0f, 23.5f, 24.0f, nan};
    int proceeds = 0;
    for (float wake : values)
        for (float bed : values)
            for (float hour : values)
                for (int32_t day : {0, 1, 5}) {
                    const bool want = ExpectedBedTime(wake, bed, hour, day);
                    OBL_CHECK_EQ(BedTimeProceeds(wake, bed, hour, day), want);
                    proceeds += want;
                }
    OBL_CHECK(proceeds > 100);
}

OBL_TEST_CASE("guest_lifecycle.bed_time_windows")
{
    // The 7-23 window: asleep before 7 and after 23 (on day 0 only the evening counts).
    OBL_CHECK(BedTimeProceeds(7, 23, 23.5f, 0));
    OBL_CHECK(BedTimeProceeds(7, 23, 3.0f, 1));
    OBL_CHECK(!BedTimeProceeds(7, 23, 3.0f, 0));
    OBL_CHECK(!BedTimeProceeds(7, 23, 12.0f, 1));
    // A window that wraps (9..1): the hour is between bed and wake.
    OBL_CHECK(!BedTimeProceeds(9, 1, 0.5f, 1));
    OBL_CHECK(BedTimeProceeds(9, 1, 3.0f, 1));
    OBL_CHECK(!BedTimeProceeds(8, 8, 12.0f, 1));
}

OBL_TEST_CASE("guest_lifecycle.time_to_leave")
{
    OBL_CHECK_EQ(TimeToLeave(0, 7.0f), 8.0f);
    OBL_CHECK_EQ(TimeToLeave(32767, 7.0f), static_cast<float>(((2.0 * 32767 * static_cast<double>(3.0518509e-5f)) + 7.0) + 1.0));
    const float mid = TimeToLeave(16384, 9.0f);
    OBL_CHECK(mid > 10.9f && mid < 11.1f);
}

OBL_TEST_CASE("guest_lifecycle.leave_decision")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // Unhappy wins over everything, 0.25 itself included; NaN is "not greater", so it counts as unhappy too.
    OBL_CHECK(LeaveDecision(0.25f, 5, 0, 0, true) == LeaveAction::Unhappy);
    OBL_CHECK(LeaveDecision(nan, 5, 0, 0, false) == LeaveAction::Unhappy);
    OBL_CHECK(LeaveDecision(0.5f, 5, 0, 0, true) == LeaveAction::GoHome);
    OBL_CHECK(LeaveDecision(0.5f, 0, 5.0f, 12.0f, false) == LeaveAction::Souvenirs); // nights used up, time to leave passed
    OBL_CHECK(LeaveDecision(0.5f, 0, 20.0f, 12.0f, false) == LeaveAction::Stay);
    OBL_CHECK(LeaveDecision(0.5f, 2, 5.0f, 12.0f, false) == LeaveAction::Stay); // nights remain
    OBL_CHECK(LeaveDecision(0.5f, 0, nan, 12.0f, false) == LeaveAction::Souvenirs); // "below" an unordered pair compares as below
    OBL_CHECK(LeaveDecision(0.5f, 0, 5.0f, nan, false) == LeaveAction::Souvenirs);
}

OBL_TEST_CASE("guest_lifecycle.attraction_slot_bands_and_gender")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(AttractionSlot(0.0f, 0) == 0);
    OBL_CHECK(AttractionSlot(0.19f, 0) == 0);
    OBL_CHECK(AttractionSlot(nan, 0) == 0); //  an unordered pair compares as below
    OBL_CHECK(AttractionSlot(0.2f, 0) == 1); // the float 0.2 is above the double 0.2
    OBL_CHECK(AttractionSlot(0.5f, 0) == 1);
    OBL_CHECK(AttractionSlot(0.7f, 0) == 1); // the float 0.7 is 0.69999999, below the double limit
    OBL_CHECK(AttractionSlot(0.71f, 0) == 2);
    OBL_CHECK(AttractionSlot(1.0f, 0) == 2);
    OBL_CHECK(AttractionSlot(0.5f, 1) == 4);
    OBL_CHECK(AttractionSlot(0.0f, 1) == 3);
    OBL_CHECK(AttractionSlot(0.9f, 1) == 5);
}
