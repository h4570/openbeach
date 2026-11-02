#include "unit_test.h"

#include "game/objects/guest_walk_targets.h"

#include <cmath>
#include <cstring>
#include <limits>

using namespace obl;
using namespace obl::guest_walk;

namespace {

// Step by step. x stays a double; y is rounded to float at each store (a raw copy, or the sum stored as float).
Goal ExpectedUnloadGoal(float x, float y, int32_t rotation)
{
    double goalX = x;
    float goalY = y;
    if (rotation == 7) {
        goalX -= 63.0;
        goalY = static_cast<float>(static_cast<double>(goalY) + 14.0);
    } else if (rotation == 5) {
        goalX += 22.0;
        goalY = static_cast<float>(static_cast<double>(goalY) + 72.0);
    }
    return {FloatToIntWrap(goalX), FloatToIntWrap(goalY)};
}

} // namespace

OBL_TEST_CASE("guest_walk_targets.truncated_goal_truncates_toward_zero")
{
    OBL_CHECK_EQ(TruncatedGoal(10.9f, -10.9f).x, 10);
    OBL_CHECK_EQ(TruncatedGoal(10.9f, -10.9f).y, -10);
    OBL_CHECK_EQ(TruncatedGoal(-0.5f, 0.99f).x, 0);
    OBL_CHECK_EQ(TruncatedGoal(-0.5f, 0.99f).y, 0);
}

OBL_TEST_CASE("guest_walk_targets.truncated_goal_wraps_beyond_int_and_zeroes_nan")
{
    // FloatToIntWrap keeps the low 32 bits of a 64-bit truncation; NaN and anything past 64 bits is the integer indefinite, whose low 32 bits are 0.
    OBL_CHECK_EQ(TruncatedGoal(4294967296.0f, 0.0f).x, 0);
    OBL_CHECK_EQ(TruncatedGoal(3e9f, 0.0f).x, static_cast<int32_t>(static_cast<uint32_t>(3000000000u)));
    OBL_CHECK_EQ(TruncatedGoal(std::numeric_limits<float>::quiet_NaN(), 5.0f).x, 0);
    OBL_CHECK_EQ(TruncatedGoal(1.0f, std::numeric_limits<float>::infinity()).y, 0);
    OBL_CHECK_EQ(TruncatedGoal(1e30f, -1e30f).x, 0);
}

OBL_TEST_CASE("guest_walk_targets.unload_goal_matches_the_expected_goal")
{
    const float xs[] = {0.0f, 62.9f, 63.0f, 64.1f, 1234.567f, 3999.99f, 16777217.0f, 3e9f, -100.5f};
    const float ys[] = {0.0f, 13.5f, 1000.25f, 7999.5f, 33554432.0f, 4e9f, -72.5f};
    for (int32_t rotation = -1; rotation <= 9; ++rotation)
        for (float x : xs)
            for (float y : ys) {
                const Goal expected = ExpectedUnloadGoal(x, y, rotation);
                const Goal actual = UnloadGoal(x, y, rotation);
                OBL_CHECK_EQ(actual.x, expected.x);
                OBL_CHECK_EQ(actual.y, expected.y);
            }
}

OBL_TEST_CASE("guest_walk_targets.unload_goal_offsets_by_direction")
{
    OBL_CHECK_EQ(UnloadGoal(1000.0f, 2000.0f, 7).x, 937);
    OBL_CHECK_EQ(UnloadGoal(1000.0f, 2000.0f, 7).y, 2014);
    OBL_CHECK_EQ(UnloadGoal(1000.0f, 2000.0f, 5).x, 1022);
    OBL_CHECK_EQ(UnloadGoal(1000.0f, 2000.0f, 5).y, 2072);
    for (int32_t rotation : {0, 1, 2, 3, 4, 6}) {
        OBL_CHECK_EQ(UnloadGoal(1000.5f, 2000.5f, rotation).x, 1000);
        OBL_CHECK_EQ(UnloadGoal(1000.5f, 2000.5f, rotation).y, 2000);
    }
}

OBL_TEST_CASE("guest_walk_targets.unload_y_is_rounded_to_float_but_x_is_not")
{
    // 2^24 + 1 is not a float: y + 14 stored as float loses the odd bit, while x - 63 stays a double.
    const float big = 16777216.0f;
    OBL_CHECK_EQ(UnloadGoal(big, big, 7).y, static_cast<int32_t>(static_cast<float>(static_cast<double>(big) + 14.0)));
    OBL_CHECK_EQ(UnloadGoal(big, big, 7).x, 16777153);
}

OBL_TEST_CASE("guest_walk_targets.wind_surf_offset_constants_match_the_expected_bits")
{
    uint32_t x = 0, y = 0;
    std::memcpy(&x, &kWindSurfOffsetX, 4);
    std::memcpy(&y, &kWindSurfOffsetY, 4);
    OBL_CHECK_EQ(x, 0xc2be0000u);
    OBL_CHECK_EQ(y, 0xc27c0000u);
}

// ---- HeadForBin -----------------------------------------------------------------------------------------------------------------

namespace {

// step by step: gx - ox stays a double, gy - oy is rounded to float, then dy^2 + dx^2 and square root.
double ExpectedBinDistance(float gx, float gy, float ox, float oy)
{
    const double dx = static_cast<double>(gx) - static_cast<double>(ox);
    const float dy = static_cast<float>(static_cast<double>(gy) - oy);
    const double dx2 = dx * dx;
    const double dy2 = static_cast<double>(dy) * dy;
    return std::sqrt(dy2 + dx2);
}

} // namespace

OBL_TEST_CASE("guest_walk_targets.bin_distance_matches_the_expected_value")
{
    const float values[] = {0.0f, 0.5f, 63.9f, 160.0f, 1234.567f, 4095.99f, 16777217.0f, -20.25f, 1e9f};
    for (float gx : values)
        for (float gy : values)
            for (float ox : {0.0f, 61.3f, 1234.0f, 3e7f})
                for (float oy : {0.0f, 7.77f, 2000.5f, -3e7f}) {
                    const double expected = ExpectedBinDistance(gx, gy, ox, oy);
                    const double actual = BinDistance(gx, gy, ox, oy);
                    OBL_CHECK(std::memcmp(&expected, &actual, sizeof(double)) == 0);
                }
}

OBL_TEST_CASE("guest_walk_targets.nearest_bin_takes_the_first_of_a_tie_and_nothing_beyond_the_range")
{
    NearestBin tie(3);
    OBL_CHECK_EQ(tie.index, 3u); // none yet
    tie.Offer(0, 100.0);
    tie.Offer(1, 100.0); // the same distance does not win
    tie.Offer(2, 100.5);
    OBL_CHECK_EQ(tie.index, 0u);
    OBL_CHECK_EQ(tie.best, 100.0f);

    NearestBin far(2);
    far.Offer(0, 160.0); // exactly at the range is not below it
    far.Offer(1, 200.0);
    OBL_CHECK_EQ(far.index, 2u);
    far.Offer(1, 159.99);
    OBL_CHECK_EQ(far.index, 1u);

    // The best is kept as float: 99.9999999 rounds up to 100.0f, so a bin at 100.0 is not below it.
    NearestBin rounded(2);
    rounded.Offer(0, 99.99999999);
    OBL_CHECK_EQ(rounded.best, 100.0f);
    rounded.Offer(1, 100.0);
    OBL_CHECK_EQ(rounded.index, 0u);
}

OBL_TEST_CASE("guest_walk_targets.nearest_bin_nan_wins_and_keeps_winning")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    NearestBin poisoned(3);
    poisoned.Offer(0, 50.0);
    poisoned.Offer(1, nan);
    OBL_CHECK_EQ(poisoned.index, 1u);
    poisoned.Offer(2, 150.0); // nothing compares as not-below NaN
    OBL_CHECK_EQ(poisoned.index, 2u);
}

OBL_TEST_CASE("guest_walk_targets.bin_room_is_an_unsigned_compare")
{
    OBL_CHECK(BinHasRoom(0, 5));
    OBL_CHECK(BinHasRoom(4, 5));
    OBL_CHECK(!BinHasRoom(5, 5));
    OBL_CHECK(!BinHasRoom(6, 5));
    OBL_CHECK(!BinHasRoom(-1, 5)); // -1 is 0xffffffff
    OBL_CHECK(BinHasRoom(5, -1)); // and a negative capacity is huge
}

OBL_TEST_CASE("guest_walk_targets.uses_bin_compares_the_scaled_draw_with_the_chance")
{
    OBL_CHECK(UsesBin(0, 0.25f));
    OBL_CHECK(!UsesBin(0, 0.0f)); // 0 is not below 0
    OBL_CHECK(UsesBin(32767, 1.0f)); // the constant is a hair under 1/32767: even the largest draw scales to 0.99999999906..., below 1
    OBL_CHECK(UsesBin(32767, 1.5f));
    OBL_CHECK(UsesBin(16384, 0.75f));
    OBL_CHECK(!UsesBin(30000, 0.75f));
    OBL_CHECK(UsesBin(5, std::numeric_limits<float>::quiet_NaN())); // unordered counts as below
    OBL_CHECK(!UsesBin(5, -1.0f));
}
