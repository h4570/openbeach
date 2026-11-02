// Arithmetic of the building draw functions (src/game/ui/sprite_preview.h): the ramp depth step and the clips of the
// Alt (construction/demolition progress) variant.

#include "unit_test.h"

#include "game/ui/sprite_preview.h"

#include <cmath>
#include <limits>

using namespace obl::sprite_preview;

OBL_TEST_CASE("sprite_preview.depth_step_is_difference_over_extent")
{
    OBL_CHECK_EQ(DepthPerPixel(10, 4), 2.5f);
    OBL_CHECK_EQ(DepthPerPixel(-300, 6), -50.0f);
    OBL_CHECK_EQ(DepthPerPixel(0, 17), 0.0f);
    OBL_CHECK_EQ(DepthPerPixel(1, 3), static_cast<float>(1.0 / 3.0));
}

OBL_TEST_CASE("sprite_preview.depth_step_zero_extent_is_infinite_or_nan")
{
    OBL_CHECK(std::isinf(DepthPerPixel(5, 0)) && DepthPerPixel(5, 0) > 0);
    OBL_CHECK(std::isinf(DepthPerPixel(-5, 0)) && DepthPerPixel(-5, 0) < 0);
    OBL_CHECK(std::isnan(DepthPerPixel(0, 0)));
}

OBL_TEST_CASE("sprite_preview.depth_step_53_bit_division_matches_single_precision_division")
{
    for (int difference = -65535; difference <= 65535; difference += 7)
        for (int extent = 1; extent <= 255; ++extent) {
            const float direct = static_cast<float>(difference) / static_cast<float>(extent);
            if (DepthPerPixel(difference, static_cast<uint8_t>(extent)) != direct) {
                OBL_CHECK(false);
                return;
            }
        }
}

OBL_TEST_CASE("sprite_preview.upper_part_keeps_one_minus_progress")
{
    Window w{50, 0, 100};
    ClipToUpperPart(&w, 0.25f, 100);
    OBL_CHECK_EQ(w.bottom, 75);
    OBL_CHECK_EQ(w.top, 0);
    OBL_CHECK_EQ(w.dstY, 50);
    // A smaller bottom (already clipped by the view) stays.
    w = {50, 0, 40};
    ClipToUpperPart(&w, 0.25f, 100);
    OBL_CHECK_EQ(w.bottom, 40);
    // Progress 0 keeps everything, 1 nothing, beyond 1 a negative bottom.
    w = {0, 0, 100};
    ClipToUpperPart(&w, 0.0f, 100);
    OBL_CHECK_EQ(w.bottom, 100);
    w = {0, 0, 100};
    ClipToUpperPart(&w, 1.0f, 100);
    OBL_CHECK_EQ(w.bottom, 0);
    w = {0, 0, 100};
    ClipToUpperPart(&w, 1.5f, 100);
    OBL_CHECK_EQ(w.bottom, -50);
}

OBL_TEST_CASE("sprite_preview.upper_part_truncates_toward_zero")
{
    Window w{0, 0, 100};
    ClipToUpperPart(&w, 0.3f, 100); // 0.3f is a little above 0.3, so (1 - 0.3f) * 100 = 69.9999988: just under 70
    OBL_CHECK_EQ(w.bottom, 69);
    w = {0, 0, 100};
    ClipToUpperPart(&w, 0.999f, 10); // 0.0100000...
    OBL_CHECK_EQ(w.bottom, 0);
    w = {0, 0, 100};
    ClipToUpperPart(&w, 1.05f, 10); // -0.5 -> 0, not -1
    OBL_CHECK_EQ(w.bottom, 0);
}

OBL_TEST_CASE("sprite_preview.lower_part_moves_destination_with_top")
{
    Window w{50, 0, 100};
    ClipToLowerPart(&w, 0.25f, 100); // newTop = 100 - 25
    OBL_CHECK_EQ(w.top, 75);
    OBL_CHECK_EQ(w.dstY, 125);
    OBL_CHECK_EQ(w.bottom, 100);
    // A top that is already lower (clipped by the view) is kept and the destination stays.
    w = {50, 90, 100};
    ClipToLowerPart(&w, 0.25f, 100);
    OBL_CHECK_EQ(w.top, 90);
    OBL_CHECK_EQ(w.dstY, 50);
    // Progress 1 keeps everything (newTop 0), progress 0 leaves nothing.
    w = {50, 0, 100};
    ClipToLowerPart(&w, 1.0f, 100);
    OBL_CHECK_EQ(w.top, 0);
    OBL_CHECK_EQ(w.dstY, 50);
    w = {50, 0, 100};
    ClipToLowerPart(&w, 0.0f, 100);
    OBL_CHECK_EQ(w.top, 100);
    OBL_CHECK_EQ(w.dstY, 150);
}

OBL_TEST_CASE("sprite_preview.flat_clip_uses_progress_minus_one")
{
    Window w{0, 0, 100};
    ClipFlat(&w, 0.5f, 100); // (0.5 - 1) * 100 = -50
    OBL_CHECK_EQ(w.bottom, -50);
    w = {0, 0, 100};
    ClipFlat(&w, 1.0f, 100);
    OBL_CHECK_EQ(w.bottom, 0);
    w = {0, 0, 100};
    ClipFlat(&w, 2.0f, 100);
    OBL_CHECK_EQ(w.bottom, 100);
    w = {0, 0, 100};
    ClipFlat(&w, 3.0f, 100); // 200 is above the current bottom
    OBL_CHECK_EQ(w.bottom, 100);
}

OBL_TEST_CASE("sprite_preview.clips_with_nan_progress_use_a_zero_truncation")
{
    // the float-to-int conversion gives 0 for NaN (low word of the 64-bit indefinite value).
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Window w{0, 0, 100};
    ClipToUpperPart(&w, nan, 100);
    OBL_CHECK_EQ(w.bottom, 0);
    w = {0, 0, 100};
    ClipFlat(&w, nan, 100);
    OBL_CHECK_EQ(w.bottom, 0);
    // top = height - 0 = 100 is below the current one: the window shrinks to it and the destination moves down with it.
    w = {7, 0, 100};
    ClipToLowerPart(&w, nan, 100);
    OBL_CHECK_EQ(w.top, 100);
    OBL_CHECK_EQ(w.dstY, 107);
}
