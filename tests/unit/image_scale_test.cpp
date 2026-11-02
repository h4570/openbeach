
#include "unit_test.h"

#include "core/image_scale.h"

#include <vector>

using namespace obl::image;

namespace {

uint16_t Rgb555(int r, int g, int b)
{
    return static_cast<uint16_t>((r << 10) | (g << 5) | b);
}
uint16_t Rgb565(int r, int g5, int b)
{
    return static_cast<uint16_t>((r << 11) | (g5 << 6) | b);
} // upper 5 bits of green

} // namespace

OBL_TEST_CASE("image_scale.palette16_formats")
{
    uint8_t palette[768] = {};
    palette[0] = 0xff;
    palette[1] = 0xff;
    palette[2] = 0xff; // white
    palette[3] = 0xf8;
    palette[4] = 0x00;
    palette[5] = 0x00; // pure red
    palette[6] = 0x07;
    palette[7] = 0xfc;
    palette[8] = 0x07; // low bits are dropped
    palette[9] = 0x00;
    palette[10] = 0x04;
    palette[11] = 0x08; // 565 keeps 6 green bits, 555 only 5
    uint16_t out555[256], out565[256];
    BuildPalette16(palette, true, out555);
    BuildPalette16(palette, false, out565);
    OBL_CHECK_EQ(out555[0], 0x7fff);
    OBL_CHECK_EQ(out565[0], 0xffff);
    OBL_CHECK_EQ(out555[1], 0x7c00);
    OBL_CHECK_EQ(out565[1], 0xf800);
    OBL_CHECK_EQ(out555[2], Rgb555(0, 0x1f, 0));
    OBL_CHECK_EQ(out565[2], 0x07e0);
    OBL_CHECK_EQ(out555[3], Rgb555(0, 0, 1));
    OBL_CHECK_EQ(out565[3], 0x0021); // green bit 2 -> 565 bit 5, blue 1
    OBL_CHECK_EQ(out555[255], 0);
}

OBL_TEST_CASE("image_scale.uniform_images_stay_uniform")
{
    for (bool is555 : {false, true}) {
        uint16_t value = is555 ? Rgb555(13, 21, 7) : Rgb565(13, 21, 7);
        for (int sw = 2; sw <= 40; sw += 7) {
            for (int dw = 1; dw <= 50; dw += 9) {
                std::vector<uint16_t> src(static_cast<size_t>(sw) * 33, value), dst(static_cast<size_t>(dw) * 11, 0);
                Resample16(src.data(), sw, 33, dst.data(), dw, 11, is555);
                for (uint16_t p : dst) OBL_CHECK_EQ(p, value);
            }
        }
    }
}

OBL_TEST_CASE("image_scale.sample_positions_and_weights")
{
    // 4x4 -> 2x2: scale 1.5, so destination x = 1 samples between source columns 1 and 2 with weight 0.5.
    std::vector<uint16_t> src(16);
    for (int i = 0; i < 16; ++i) src[i] = Rgb555(10 + (i % 4), 0, 0); // red = 10, 11, 12, 13 along each row
    uint16_t dst[4];
    Resample16(src.data(), 4, 4, dst, 2, 2, true);
    OBL_CHECK_EQ(dst[0], Rgb555(10, 0, 0)); // exact sample
    OBL_CHECK_EQ(dst[1], Rgb555(12, 0, 0)); // (11 + 12) / 2 = 11.5 -> rounds to even 12
    for (int i = 0; i < 16; ++i) src[i] = Rgb555(9 + (i % 4), 0, 0); // 9, 10, 11, 12
    Resample16(src.data(), 4, 4, dst, 2, 2, true);
    OBL_CHECK_EQ(dst[1], Rgb555(10, 0, 0)); // (10 + 11) / 2 = 10.5 -> rounds to even 10
}

OBL_TEST_CASE("image_scale.565_drops_the_lowest_green_bit")
{
    uint16_t src[4] = {
            static_cast<uint16_t>(0x07e0), static_cast<uint16_t>(0x07e0), static_cast<uint16_t>(0x07e0), static_cast<uint16_t>(0x07e0)};
    uint16_t dst = 0;
    Resample16(src, 2, 2, &dst, 1, 1, false);
    OBL_CHECK_EQ(dst, 0x07c0); // green 111111 -> 11111 stored in bits 6..10
}

OBL_TEST_CASE("image_scale.chain_halves_until_within_two_to_one")
{
    // 16x16 gradient to 3x3: halving 16 -> 8 -> 4 (4/2 = 2 < 3 stops), then a 4 -> 3 resample. Must equal doing it by hand.
    std::vector<uint16_t> src(16 * 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) src[y * 16 + x] = Rgb565(x * 2, (x + y) & 31, y * 2);
    std::vector<uint16_t> half(8 * 8), quarter(4 * 4), expected(9), actual(9);
    Resample16(src.data(), 16, 16, half.data(), 8, 8, false);
    Resample16(half.data(), 8, 8, quarter.data(), 4, 4, false);
    Resample16(quarter.data(), 4, 4, expected.data(), 3, 3, false);
    BuildHalfSizeChain(src.data(), 16, 16, actual.data(), 3, 3, false);
    OBL_CHECK((expected == actual));
}

OBL_TEST_CASE("image_scale.chain_without_halving_is_a_single_pass")
{
    std::vector<uint16_t> src(10 * 10);
    for (int i = 0; i < 100; ++i) src[i] = static_cast<uint16_t>(i * 331);
    std::vector<uint16_t> expected(6 * 6), actual(6 * 6);
    Resample16(src.data(), 10, 10, expected.data(), 6, 6, true); // 6 > 10 / 2
    BuildHalfSizeChain(src.data(), 10, 10, actual.data(), 6, 6, true);
    OBL_CHECK((expected == actual));
}

OBL_TEST_CASE("image_scale.empty_destination_writes_nothing")
{
    std::vector<uint16_t> src(16, 0x1234);
    uint16_t dst[4] = {1, 2, 3, 4};
    Resample16(src.data(), 4, 4, dst, 0, 2, true);
    Resample16(src.data(), 4, 4, dst, 2, 0, true);
    OBL_CHECK_EQ(dst[0], 1);
    OBL_CHECK_EQ(dst[3], 4);
}
