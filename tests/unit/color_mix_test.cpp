
#include "unit_test.h"

#include "game/ui/color_mix.h"

#include <cstdint>

using namespace obl::color_mix;

namespace {

uint32_t Pack565(uint32_t r, uint32_t g, uint32_t b)
{
    return (r << 11) | (g << 6) | b;
}

} // namespace

OBL_TEST_CASE("color_mix.weight_zero_returns_the_second_colour_and_weight_one_the_first")
{
    const PixelFormat f565 = PixelFormat::Rgb565(), f555 = PixelFormat::Rgb555();
    for (uint32_t a = 0; a < 0x10000; a += 37) {
        const uint32_t b = (a * 2654435761u) >> 16;
        OBL_CHECK_EQ(Mix(f565, a, b, 0.0f), b & ~0x20u);
        OBL_CHECK_EQ(Mix(f565, a, b, 1.0f), a & ~0x20u);
        OBL_CHECK_EQ(Mix(f555, a & 0x7fff, b & 0x7fff, 0.0f), b & 0x7fff);
        OBL_CHECK_EQ(Mix(f555, a & 0x7fff, b & 0x7fff, 1.0f), a & 0x7fff);
    }
}

OBL_TEST_CASE("color_mix.channels_truncate_toward_zero")
{
    const PixelFormat f = PixelFormat::Rgb565();
    // 0.5 averages each channel and drops the fraction: red (3 + 6) / 2 = 4, green (10 + 21) / 2 = 15, blue (1 + 2) / 2 = 1.
    OBL_CHECK_EQ(Mix(f, Pack565(3, 10, 1), Pack565(6, 21, 2), 0.5f), Pack565(4, 15, 1));
    // 0.25 of the first: red 8 * 0.75 + 4 * 0.25 = 7, green 31 * 0.75 + 0 = 23.25 -> 23.
    OBL_CHECK_EQ(Mix(f, Pack565(4, 0, 0), Pack565(8, 31, 0), 0.25f), Pack565(7, 23, 0));
}

OBL_TEST_CASE("color_mix.equal_colours_stay_equal_for_binary_weights")
{
    const PixelFormat f = PixelFormat::Rgb565();
    for (uint32_t c = 0; c < 0x10000; c += 101)
        for (int k = 0; k <= 16; ++k) OBL_CHECK_EQ(Mix(f, c, c, static_cast<float>(k) / 16.0f), c & ~0x20u);
}

OBL_TEST_CASE("color_mix.out_of_range_weights_clamp_and_wrap_like_the_byte_store")
{
    const PixelFormat f = PixelFormat::Rgb565();
    // 2.0: second * -1 + first * 2 = 62 in blue, past the 0x1f clamp.
    OBL_CHECK_EQ(Mix(f, 0x1f, 0, 2.0f), 0x1fu);
    // 3.5 with blue second = 31, first = 0: -77.5 -> -77 -> byte 0xb3, above the clamp as well.
    OBL_CHECK_EQ(Mix(f, 0, 0x1f, 3.5f), 0x1fu);
    // Green clamps to 0x3f in 565 although the field holds five bits (the 62 below spills into the red field) and to 0x1f in 555.
    OBL_CHECK_EQ(Mix(f, Pack565(0, 31, 0), 0, 2.0f), 62u << 6);
    const PixelFormat f555 = PixelFormat::Rgb555();
    OBL_CHECK_EQ(Mix(f555, 0x1f << 5, 0, 2.0f), 0x1fu << 5);
}

OBL_TEST_CASE("color_mix.only_the_low_byte_of_the_blue_mask_counts_and_wide_shifts_empty_a_channel")
{
    PixelFormat f = PixelFormat::Rgb565();
    f.greenMask = f.redMask = 0;
    f.blueMask = 0x01ff; // the high bit of the mask is dropped: bit 8 of a colour never reaches blue
    OBL_CHECK_EQ(Mix(f, 0x100, 0x100, 1.0f), 0u);
    OBL_CHECK_EQ(Mix(f, 0x11, 0x11, 1.0f), 0x11u);
    // A shift of 16 or more leaves nothing of a 16-bit channel, so green mixes zeros (the result's own shift is 32-bit).
    f = PixelFormat::Rgb565();
    f.greenShift = 16;
    OBL_CHECK_EQ(Mix(f, 0x7e0, 0x7e0, 0.5f), 0u);
}

OBL_TEST_CASE("color_mix.the_red_value_keeps_the_upper_half_of_its_truncated_result")
{
    const PixelFormat f = PixelFormat::Rgb565();
    // second red = 0x1f, first 0: 31 * (1 - 3.5) = -77.5 -> -77 = 0xffffffb3; the clamped byte is 0x1f, the upper half (0xffff) is shifted by 11 and kept.
    OBL_CHECK_EQ(Mix(f, 0, Pack565(31, 0, 0), 3.5f), (0xffff0000u | 0x1fu) << 11);
    // In range the upper half is zero.
    OBL_CHECK_EQ(Mix(f, Pack565(31, 0, 0), 0, 1.0f), Pack565(31, 0, 0));
}
