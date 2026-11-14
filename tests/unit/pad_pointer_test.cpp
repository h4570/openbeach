// The thumb stick as a mouse (platform/pad_pointer.h).

#include "unit_test.h"

#include "platform/pad_pointer.h"

#include <cmath>

using namespace obl::platform;

namespace {

bool Near(double a, double b)
{
    return std::fabs(a - b) < 0.001;
}

} // namespace

OBL_TEST_CASE("pad_pointer.dead_zone_stays_still")
{
    PadPointer pad;
    const auto rest = pad.Step(3000, -3000, 1.0); // about 13 % of the range: inside the dead zone
    OBL_CHECK(rest.x == 0.0 && rest.y == 0.0);
}

OBL_TEST_CASE("pad_pointer.full_deflection_is_full_speed")
{
    PadPointer pad;
    const auto right = pad.Step(32767, 0, 1.0);
    OBL_CHECK(Near(right.x, kPadMaxSpeed) && right.y == 0.0);
    const auto up = pad.Step(0, -32767, 0.5);
    OBL_CHECK(Near(up.y, -kPadMaxSpeed / 2));
}

OBL_TEST_CASE("pad_pointer.small_push_is_slower_than_linear")
{
    PadPointer pad;
    const auto half = pad.Step(16384, 0, 1.0); // half way: the curve gives less than half the speed
    OBL_CHECK(half.x > 0 && half.x < kPadMaxSpeed / 2);
}

OBL_TEST_CASE("pad_pointer.diagonal_is_not_faster_than_straight")
{
    PadPointer pad;
    const auto diagonal = pad.Step(32767, 32767, 1.0); // the corner of the square range clamps to the rim
    OBL_CHECK(Near(std::hypot(diagonal.x, diagonal.y), kPadMaxSpeed));
}

OBL_TEST_CASE("pad_pointer.speed_setting_scales_the_motion")
{
    PadPointer pad;
    OBL_CHECK(Near(pad.Step(32767, 0, 1.0, 50.0).x, kPadMaxSpeed / 2));
    OBL_CHECK(Near(pad.Step(32767, 0, 1.0, 250.0).x, kPadMaxSpeed)); // above 100 % is 100 %
    OBL_CHECK(Near(pad.Step(32767, 0, 1.0, 0.0).x, kPadMaxSpeed * kPadMinPercent / 100.0)); // 0 % still moves
}

OBL_TEST_CASE("pad_pointer.slow_motion_adds_up")
{
    PadAccumulator accumulator;
    int total = 0;
    for (int i = 0; i < 12; ++i) total += accumulator.Add(0.25); // exact in binary
    OBL_CHECK_EQ(total, 3);
    OBL_CHECK_EQ(accumulator.Add(-1.5), -1); // truncates toward zero; the half pixel waits
    OBL_CHECK_EQ(accumulator.Add(-0.5), -1);
}
