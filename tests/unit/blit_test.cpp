#include "game/ui/blit.h"
#include "unit_test.h"

#include <array>

using namespace obl::blit;

OBL_TEST_CASE("blit.type2_width_markers_retain_depth")
{
    const uint8_t rle[]{2, 1, 2, 1, 3, 255, 1, 4};
    std::array<uint16_t, 256> palette{};
    palette[1] = 101;
    palette[2] = 102;
    palette[3] = 103;
    palette[4] = 104;
    std::array<uint32_t, 16> dst;
    dst.fill(0x1234abcd);
    Type2(dst.data(), rle, palette.data(), {8, 1, 1, 5, 1, 0, 0, 5, 1});
    OBL_CHECK_EQ(dst[9], 0x12340005u);
    OBL_CHECK_EQ(dst[10], 0x12340066u);
    OBL_CHECK_EQ(dst[11], 0x12340067u);
    OBL_CHECK_EQ(dst[12], 0x1234abcdu);
    OBL_CHECK_EQ(dst[13], 0x12340005u);
    OBL_CHECK_EQ(dst[14], 0x1234abcdu);
}

OBL_TEST_CASE("blit.byte_rle_signed_bias_and_destination_parity")
{
    const uint8_t rle[]{4, 0, 24, 255, 100, 4, 255, 255, 255, 255};
    std::array<uint8_t, 24> dst;
    dst.fill(5);
    RleToBytes(dst.data(), rle, {8, 1, 1, 4, 2, 0, 0, 4, 2}, false);
    OBL_CHECK_EQ(dst[9], 5);
    OBL_CHECK_EQ(dst[10], 5);
    OBL_CHECK_EQ(dst[11], 231);
    OBL_CHECK_EQ(dst[12], 76);
    dst.fill(5);
    RleToBytes(dst.data(), rle, {8, 1, 1, 4, 2, 0, 0, 4, 2}, true);
    // Odd dstY skips row zero, odd dstX samples source x=1 and x=3.
    OBL_CHECK_EQ(dst[0], 231);
    OBL_CHECK_EQ(dst[1], 231);
    OBL_CHECK_EQ(dst[2], 5);
    OBL_CHECK_EQ(dst[8], 5);
}

OBL_TEST_CASE("blit.clear_runs_have_no_payload_and_max_has_source_pitch")
{
    const uint8_t runs[]{2, 254, 2, 6};
    std::array<uint8_t, 32> dst;
    dst.fill(77);
    RleClear(dst.data(), runs, {8, 1, 1, 6, 2, 1, 0, 4, 2});
    OBL_CHECK_EQ(dst[9], 0);
    OBL_CHECK_EQ(dst[10], 77);
    OBL_CHECK_EQ(dst[11], 77);
    OBL_CHECK_EQ(dst[12], 77);
    OBL_CHECK_EQ(dst[17], 0);
    OBL_CHECK_EQ(dst[19], 0);
    std::array<uint8_t, 24> src{};
    src[7] = 100;
    src[8] = 20;
    src[13] = 200;
    src[14] = 99;
    MaxBytes(dst.data(), src.data(), {8, 1, 1, 6, 999, 1, 1, 3, 3});
    OBL_CHECK_EQ(dst[9], 100);
    OBL_CHECK_EQ(dst[10], 77);
    OBL_CHECK_EQ(dst[17], 200);
    OBL_CHECK_EQ(dst[18], 99);
    OBL_CHECK_EQ(dst[19], 0);
}

OBL_TEST_CASE("blit.ui_rle_depth_and_clipped_bounds")
{
    // Two six-pixel rows: a transparent gap, an index zero literal and a high palette index.
    const uint8_t rle[]{2, 0, 200, 254, 2, 1, 2, 255, 5, 3, 4, 5, 6, 7};
    std::array<uint16_t, 256> palette{};
    for (size_t i = 0; i < palette.size(); ++i) palette[i] = static_cast<uint16_t>(100 + i);
    std::array<uint32_t, 32> dst;
    dst.fill(0x1234abcd);
    Ui(dst.data(), rle, palette.data(), {8, 1, 1, 6, 2, 0, 0, 6, 2});
    OBL_CHECK_EQ(dst[9], 0xffff0064u);
    OBL_CHECK_EQ(dst[10], 0xffff012cu);
    OBL_CHECK_EQ(dst[11], 0x1234abcdu);
    OBL_CHECK_EQ(dst[12], 0x1234abcdu);
    OBL_CHECK_EQ(dst[13], 0xffff0065u);
    OBL_CHECK_EQ(dst[14], 0xffff0066u);
    OBL_CHECK_EQ(dst[17], 0x1234abcdu);
    OBL_CHECK_EQ(dst[18], 0xffff0067u);
    OBL_CHECK_EQ(dst[8], 0x1234abcdu);
    OBL_CHECK_EQ(dst[15], 0x1234abcdu);
    dst.fill(0x1234abcd);
    Ui(dst.data(), rle, palette.data(), {8, 1, 1, 6, 2, 0, 1, 3, 2});
    OBL_CHECK_EQ(dst[9], 0x1234abcdu);
    OBL_CHECK_EQ(dst[10], 0xffff0067u);
    OBL_CHECK_EQ(dst[11], 0xffff0068u);
    OBL_CHECK_EQ(dst[12], 0x1234abcdu);
    OBL_CHECK_EQ(dst[18], 0x1234abcdu);
    const auto before = dst;
    Ui(dst.data(), rle, palette.data(), {8, 1, 1, 6, 2, 2, 0, 4, 2});
    OBL_CHECK(dst == before);
    std::array<uint16_t, 32> words{};
    Ui(words.data(), rle, palette.data(), {8, 1, 1, 6, 2, 0, 0, 6, 2});
    OBL_CHECK_EQ(words[9], 100);
    OBL_CHECK_EQ(words[10], 300);
    OBL_CHECK_EQ(words[11], 0);
    words.fill(0);
    Ui(words.data(), rle, palette.data(), {8, 1, 1, 6, 2, 1, 0, 2, 2});
    OBL_CHECK_EQ(words[9], 300);
    OBL_CHECK_EQ(words[10], 0);
    OBL_CHECK_EQ(words[11], 0);
    OBL_CHECK_EQ(words[12], 101);
    OBL_CHECK_EQ(words[13], 102);
}

OBL_TEST_CASE("blit.translucent_all_alpha_levels_555_565")
{
    std::array<uint16_t, 256> palette{};
    palette[1] = 0xffff;
    const uint8_t rle[]{4, 1, 65, 129, 193};
    for (const auto masks : {Masks{0x7bde, 0x739c}, Masks{0xf7de, 0xe79c}}) {
        std::array<uint32_t, 6> dst;
        dst.fill(0x12340000);
        Translucent(dst.data(), rle, palette.data(), {6, 1, 0, 4, 1, 0, 0, 4, 1}, masks);
        OBL_CHECK_EQ(dst[0], 0x12340000u);
        OBL_CHECK_EQ(dst[1], 0xffffffffu);
        OBL_CHECK_EQ(dst[2], 0xffff0000u | ((masks.half >> 1) + (masks.quarter >> 2)));
        OBL_CHECK_EQ(dst[3], 0xffff0000u | (masks.half >> 1));
        OBL_CHECK_EQ(dst[4], 0xffff0000u | (masks.quarter >> 2));
        OBL_CHECK_EQ(dst[5], 0x12340000u);
        std::array<uint16_t, 6> words{};
        Translucent(words.data(), rle, palette.data(), {6, 1, 0, 4, 1, 1, 0, 2, 1}, masks);
        OBL_CHECK_EQ(words[1], (masks.half >> 1) + (masks.quarter >> 2));
        OBL_CHECK_EQ(words[2], masks.half >> 1);
        OBL_CHECK_EQ(words[3], masks.quarter >> 2);
        OBL_CHECK_EQ(words[4], 0);
    }
}

OBL_TEST_CASE("blit.bitmap_pitch_origins_key_mask_and_depth")
{
    const uint8_t src[]{9, 9, 9, 9, 9, 9, 9, 0, 1, 9, 9, 9, 9, 2, 3, 9, 9, 9};
    const uint16_t palette[]{0x4567, 0x1234, 0xabcd, 0xffff, 0, 0, 0, 0, 0, 0};
    std::array<uint32_t, 25> dst;
    dst.fill(0x98766543);
    Bitmap(dst.data(), src, palette, {6, 5, 1, 1, 2, 1, 2, 2}, true, 0x0ff0);
    OBL_CHECK_EQ(dst[7], 0x98766543u);
    OBL_CHECK_EQ(dst[8], 0x98760230u);
    OBL_CHECK_EQ(dst[12], 0x98760bc0u);
    OBL_CHECK_EQ(dst[13], 0x98760ff0u);
    OBL_CHECK_EQ(dst[6], 0x98766543u);
    OBL_CHECK_EQ(dst[14], 0x98766543u);
    Bitmap(dst.data(), src, palette, {6, 5, 1, 1, 2, 1, 2, 2}, false);
    OBL_CHECK_EQ(dst[7], 0x98764567u);
    std::array<uint16_t, 25> words;
    words.fill(0x6543);
    Bitmap(words.data(), src, palette, {6, 10, 1, 1, 2, 3, 2, 2});
    OBL_CHECK_EQ(words[2], 0x6543);
    OBL_CHECK_EQ(words[3], 0x1234);
    OBL_CHECK_EQ(words[7], 0xabcd);
    OBL_CHECK_EQ(words[8], 0xffff);
    OBL_CHECK_EQ(words[17], 0x6543);
}

OBL_TEST_CASE("blit.actor_depth_slant_occlusion_alpha_and_wrap")
{
    const Masks masks{0xf7de, 0xe79c};
    const uint8_t rle[]{4, 1, 65, 129, 193, 4, 1, 65, 129, 193};
    std::array<uint16_t, 256> palette{};
    palette[1] = 0xffff;
    std::array<uint32_t, 16> dst;
    dst.fill(0x00300000);
    // slant -1: initial 40+8, first row -8 => 40; second row +16 => 56.
    Depth(dst.data(), rle, palette.data(), {8, 1, 0, 4, 2, 0, 0, 4, 2}, 40, -1, masks, DepthKind::Actor);
    for (int x = 1; x <= 4; ++x) OBL_CHECK_EQ(dst[x], 0x00300000u);
    OBL_CHECK_EQ(dst[9], 0x0038ffffu);
    OBL_CHECK_EQ(dst[10], 0x00300000u | ((masks.half >> 1) + (masks.quarter >> 2)));
    OBL_CHECK_EQ(dst[11], 0x00300000u | (masks.half >> 1));
    OBL_CHECK_EQ(dst[12], 0x00300000u | (masks.quarter >> 2));
    dst.fill(0x00280000);
    Depth(dst.data(), rle, palette.data(), {8, 1, 0, 4, 2, 0, 0, 4, 2}, 40, -1, masks, DepthKind::Actor);
    OBL_CHECK_EQ(dst[1], 0x0028ffffu); // equality is visible
    dst.fill(0);
    Depth(dst.data(), rle, palette.data(), {8, 1, 0, 4, 2, 0, 0, 4, 2}, 0xffff, 0, masks, DepthKind::Actor);
    OBL_CHECK_EQ(dst[1], 0x000fffffu); // high word wraps, rather than saturating
    OBL_CHECK_EQ(dst[0], 0u);
    OBL_CHECK_EQ(dst[5], 0u);
}

OBL_TEST_CASE("blit.keyline_only_marks_full_run_endpoints")
{
    const uint8_t rle[]{4, 1, 2, 3, 4};
    std::array<uint16_t, 256> palette{};
    std::array<uint32_t, 8> dst;
    dst.fill(0xffffabcd);
    Depth(dst.data(), rle, palette.data(), {8, 1, 0, 4, 1, 0, 0, 4, 1}, 32, 0, {0xf7de, 0xe79c}, DepthKind::Keyline, false, 0x1234);
    OBL_CHECK_EQ(dst[1], 0x1234u);
    OBL_CHECK_EQ(dst[2], 0xffffabcdu);
    OBL_CHECK_EQ(dst[3], 0xffffabcdu);
    OBL_CHECK_EQ(dst[4], 0x1234u);
    dst.fill(0xffffabcd);
    Depth(dst.data(), rle, palette.data(), {8, 1, 0, 4, 1, 1, 0, 2, 1}, 32, 0, {0xf7de, 0xe79c}, DepthKind::Keyline, false, 0x1234);
    for (auto v : dst) OBL_CHECK_EQ(v, 0xffffabcdu);
}

OBL_TEST_CASE("blit.type12_zoom_sampling_and_depth_updates")
{
    const uint8_t rle[]{4, 1, 2, 3, 4, 4, 65, 66, 67, 68};
    std::array<uint16_t, 256> palette{};
    palette[1] = palette[2] = 0xffff;
    palette[3] = palette[4] = 0x1234;
    std::array<uint32_t, 32> dst{};
    Depth(dst.data(), rle, palette.data(), {8, 3, 3, 4, 2, 0, 0, 4, 2}, 100, 0, {0xf7de, 0xe79c}, DepthKind::Type12, true);
    OBL_CHECK_EQ(dst[9], 0x0064b5d6u); // 75% white, second row's first literal, new depth
    OBL_CHECK_EQ(dst[10], 0x0064098fu); // second sample is index 67
    OBL_CHECK_EQ(dst[8], 0u);
    OBL_CHECK_EQ(dst[11], 0u);
    dst.fill(0);
    // Clipped ZoomOut samples odd source x, unlike its full branch's first literal per pair.
    Depth(dst.data(), rle, palette.data(), {8, 2, 2, 4, 2, 0, 0, 3, 2}, 100, 0, {0xf7de, 0xe79c}, DepthKind::Type12, true);
    OBL_CHECK_EQ(dst[9], 0x0064b5d6u);
    OBL_CHECK_EQ(dst[10], 0u);
    const uint8_t singleRuns[]{1, 1, 1, 2, 1, 3, 1, 4, 1, 1, 1, 2, 1, 3, 1, 4};
    dst.fill(0);
    Depth(dst.data(), singleRuns, palette.data(), {8, 2, 2, 4, 2, 0, 0, 4, 2}, 100, 0, {0xf7de, 0xe79c}, DepthKind::Type12, true);
    // Full Type12ZoomOut samples the first literal of each run, including one-pixel runs.
    OBL_CHECK_EQ(dst[9], 0x0064ffffu);
    OBL_CHECK_EQ(dst[10], 0x0064ffffu);
    OBL_CHECK_EQ(dst[11], 0x00641234u);
    OBL_CHECK_EQ(dst[12], 0x00641234u);
    OBL_CHECK_EQ(dst[13], 0u);
}

OBL_TEST_CASE("blit.lit_preserves_occluded_light_and_uses_separate_pitch")
{
    const uint8_t rle[]{4, 1, 2, 3, 4};
    std::array<uint16_t, 256> palette{};
    palette[1] = 0x1234;
    std::array<uint32_t, 16> dst{};
    dst[10] = 0xffffabcd;
    std::array<uint8_t, 20> light;
    light.fill(99);
    Depth(dst.data(), rle, palette.data(), {8, 1, 1, 4, 1, 0, 0, 4, 1}, 32, 0, {0xf7de, 0xe79c}, DepthKind::Lit, false, 0, light.data(),
            10);
    OBL_CHECK_EQ(dst[9], 0x00301234u);
    OBL_CHECK_EQ(dst[10], 0xffffabcdu);
    OBL_CHECK_EQ(light[11], 4);
    OBL_CHECK_EQ(light[12], 99);
    OBL_CHECK_EQ(light[13], 2);
    OBL_CHECK_EQ(light[14], 4);
    OBL_CHECK_EQ(light[10], 99);
    OBL_CHECK_EQ(light[15], 99);
    // Left clipping disables the full branch's endpoint light value and ignores right.
    dst.fill(0);
    light.fill(99);
    Depth(dst.data(), rle, palette.data(), {8, 1, 1, 4, 1, 1, 0, 2, 1}, 32, 0, {0xf7de, 0xe79c}, DepthKind::Lit, false, 0, light.data(),
            10);
    OBL_CHECK_EQ(light[11], 2);
    OBL_CHECK_EQ(light[12], 2);
    OBL_CHECK_EQ(light[13], 2);
    OBL_CHECK_EQ(light[14], 99);
}

OBL_TEST_CASE("blit.actor_zoom_origin_parity_and_opaque_background_depth")
{
    const uint8_t rle[]{2, 1, 2, 2, 3, 4};
    std::array<uint16_t, 256> palette{};
    palette[2] = 0x1234;
    palette[4] = 0xabcd;
    std::array<uint32_t, 16> dst;
    dst.fill(0x00200000);
    Depth(dst.data(), rle, palette.data(), {8, 1, 1, 2, 2, 0, 0, 2, 2}, 40, 0, {0xf7de, 0xe79c}, DepthKind::Actor, true);
    OBL_CHECK_EQ(dst[0], 0x00201234u); // full branch uses even local row and odd x; retains depth
    OBL_CHECK_EQ(dst[1], 0x00200000u);
    dst.fill(0x00200000);
    Depth(dst.data(), rle, palette.data(), {8, 1, 1, 2, 2, 0, 0, 2, 2}, 40, -1, {0xf7de, 0xe79c}, DepthKind::Actor, true);
    OBL_CHECK_EQ(dst[0], 0x00301234u); // negative slant updates opaque depth, including odd-origin +8
}

OBL_TEST_CASE("blit.ramp_encoded_depth_offsets_clipped_bias_and_ghost")
{
    const uint8_t rle[]{0, 255, 3, 200, 201, 202};
    const uint8_t frame[]{99, 99, 99, 3, 4, 5};
    std::array<uint16_t, 256> palette{};
    palette[200] = 0xffff;
    palette[201] = 0x1234;
    palette[202] = 0xabcd;
    std::array<uint32_t, 8> dst;
    dst.fill(0x00020000);
    Ramp(dst.data(), rle, frame, palette.data(), {8, 0, 0, 4, 1, 0, 0, 4, 1}, 3, -0.5f, {0xf7de, 0xe79c}, false, false);
    OBL_CHECK_EQ(dst[0], 0x00020000u);
    OBL_CHECK_EQ(dst[1], 0x0002ffffu);
    OBL_CHECK_EQ(dst[2], 0x00020000u);
    dst.fill(0);
    Ramp(dst.data(), rle, frame, palette.data(), {8, 0, 0, 4, 1, 1, 0, 3, 1}, 10, 0.5f, {0xf7de, 0xe79c}, true, false);
    OBL_CHECK_EQ(dst[0], 0x000c7befu); // clipped branch adds one to the depth sample
    OBL_CHECK_EQ(dst[1], 0x000c090au);
    OBL_CHECK_EQ(dst[2], 0u);
}

OBL_TEST_CASE("blit.shadow_payload_free_runs_depth_and_zoom_odd_control")
{
    const uint8_t runs[]{255, 3, 255, 3};
    std::array<uint32_t, 16> dst;
    dst.fill(0x0010ffff);
    Shadow(dst.data(), runs, {8, 0, 0, 4, 2, 0, 0, 4, 2}, 32, {0xf7de, 0xe79c}, false);
    OBL_CHECK_EQ(dst[0], 0x0010ffffu);
    OBL_CHECK_EQ(dst[1], 0x00107befu);
    OBL_CHECK_EQ(dst[9], 0x00207befu);
    dst.fill(0);
    Shadow(dst.data(), runs, {8, 0, 0, 4, 2, 0, 0, 4, 2}, 32, {0xf7de, 0xe79c}, true);
    OBL_CHECK_EQ(dst[0], 0x00200000u);
    OBL_CHECK_EQ(dst[1], 0x00200000u);
    OBL_CHECK_EQ(dst[2], 0u);
}

OBL_TEST_CASE("blit.water_mask_levels_depth_flag_odd_extents_and_scalar_truncation")
{
    const Masks masks{0xf7de, 0xe79c};
    const WaterColors colors{0x39e7, 0x7bef, 0xb5d6};
    const uint32_t src[]{0x12320000, 0x12320000, 0x12320000, 0x12320000, 0x12320000, 0x1233abcd, 1, 2, 3, 4, 5, 6};
    const uint8_t mask[]{0, 1, 2, 3, 4, 4, 0, 0, 0, 0, 0, 0};
    std::array<uint32_t, 24> dst;
    dst.fill(0xdeadbeef);
    Water(dst.data(), src, mask, {0x10006, 0x10000, 0x10000, 6, 2, 8, 1, 1}, masks, colors, false);
    OBL_CHECK_EQ(dst[9], 0x12320000u);
    OBL_CHECK_EQ(dst[10], 0x123239e7u);
    OBL_CHECK_EQ(dst[11], 0x12327befu);
    OBL_CHECK_EQ(dst[12], 0x1232b5d6u);
    OBL_CHECK_EQ(dst[13], 0x1232ffffu);
    OBL_CHECK_EQ(dst[14], 0x1233abcdu);
    dst.fill(0xdeadbeef);
    Water(dst.data(), src, mask, {6, 0, 0, 5, 2, 8, 3, 3}, masks, colors, true);
    OBL_CHECK_EQ(dst[9], 0u);
    OBL_CHECK_EQ(dst[10], 0x7befu);
    OBL_CHECK_EQ(dst[11], 0xffffu);
    OBL_CHECK_EQ(dst[12], 0xdeadbeefu);
    Water(dst.data(), nullptr, nullptr, {6, 0, 0, 5, 2, 8, 3, 3}, masks, colors, true);
    OBL_CHECK_EQ(dst[12], 0xdeadbeefu);
}

OBL_TEST_CASE("blit.water_overlapping_full_copy_preserves_pair_loads_and_bottom_up_rows")
{
    const Masks masks{0xf7de, 0xe79c};
    const WaterColors colors{};
    std::array<uint32_t, 8> pixels{1, 2, 3, 4, 5, 6, 7, 8};
    Water(pixels.data(), pixels.data(), nullptr, {8, 0, 0, 5, 1, 8, 1, 0}, masks, colors, false);
    const std::array<uint32_t, 8> paired{1, 1, 2, 2, 4, 4, 7, 8};
    OBL_CHECK(pixels == paired);
    pixels = {1, 2, 3, 4, 5, 6, 7, 8};
    Water(pixels.data(), pixels.data(), nullptr, {2, 0, 0, 2, 3, 2, 0, 1}, masks, colors, false);
    const std::array<uint32_t, 8> bottomUp{1, 2, 1, 2, 3, 4, 5, 6};
    OBL_CHECK(pixels == bottomUp);
}

OBL_TEST_CASE("blit.water_masked_alias_reads_prior_writes_and_half_alias_samples_forward")
{
    const Masks masks{0xf7de, 0xe79c};
    const WaterColors colors{};
    const uint8_t mask[]{0, 0, 0, 0, 0, 0, 0, 0};
    std::array<uint32_t, 8> pixels{1, 2, 3, 4, 5, 6, 7, 8};
    Water(pixels.data(), pixels.data(), mask, {8, 0, 0, 5, 1, 8, 1, 0}, masks, colors, false);
    const std::array<uint32_t, 8> forward{1, 1, 1, 1, 1, 1, 7, 8};
    OBL_CHECK(pixels == forward);
    pixels = {1, 2, 3, 4, 5, 6, 7, 8};
    Water(pixels.data(), pixels.data(), nullptr, {4, 0, 0, 4, 2, 4, 2, 0}, masks, colors, true);
    const std::array<uint32_t, 8> half{1, 1, 3, 4, 5, 6, 7, 8};
    OBL_CHECK(pixels == half);
}
