// Screen geometry of sprites in the world (src/game/ui/render_geometry.h): offsets of types 4/5 per direction,
// anchor rectangle, world-to-screen arithmetic and the 16-bit draw depth.

#include "unit_test.h"

#include "game/ui/map_view.h"
#include "game/ui/render_geometry.h"

#include <climits>
#include <limits>

using namespace obl::render_geometry;

namespace {

constexpr int kActor = 3, kShadow = 4, kBuilding = 5;

AnimMetrics Anim(int hotX, int width, char name = 'i', int hotY = 0, int height = 0)
{
    return {hotX, hotY, width, height, name};
}

int Offset(int type, int direction, const AnimMetrics& current, const AnimMetrics* first = nullptr)
{
    int offset = 12345;
    OBL_CHECK(DrawOffsetX(type, direction, current, first, &offset));
    return offset;
}

} // namespace

OBL_TEST_CASE("render_geometry.offset_plain_types_use_hotspot")
{
    const AnimMetrics anim = Anim(-7, 40);
    for (int type : {0, 1, 2, kActor, 6, 7, 0xc})
        for (int direction = 0; direction < 8; ++direction) OBL_CHECK_EQ(Offset(type, direction, anim), -7);
}

OBL_TEST_CASE("render_geometry.offset_types_4_5_mirror_odd_directions")
{
    const AnimMetrics anim = Anim(-30, 64);
    for (int type : {kShadow, kBuilding}) {
        OBL_CHECK_EQ(Offset(type, 0, anim), -30);
        OBL_CHECK_EQ(Offset(type, 2, anim), -30);
        // Mirrored frame: the hotspot moves to the other edge, offset = -(hotspot + width).
        OBL_CHECK_EQ(Offset(type, 1, anim), -34);
        OBL_CHECK_EQ(Offset(type, 3, anim), -34);
    }
}

OBL_TEST_CASE("render_geometry.offset_mirror_pivots_on_first_anim_for_s_and_l")
{
    const AnimMetrics first = Anim(-10, 50, 'i');
    for (char name : {'s', 'l'}) {
        const AnimMetrics anim = Anim(-30, 64, name);
        for (int type : {kShadow, kBuilding}) {
            OBL_CHECK(UsesFirstAnim(type, 1, name));
            OBL_CHECK(UsesFirstAnim(type, 3, name));
            OBL_CHECK(!UsesFirstAnim(type, 0, name));
            OBL_CHECK(!UsesFirstAnim(type, 2, name));
            // hotspot - 2 * first.hotspot - first.width
            OBL_CHECK_EQ(Offset(type, 1, anim, &first), -30 + 20 - 50);
            OBL_CHECK_EQ(Offset(type, 3, anim, &first), -60);
            OBL_CHECK_EQ(Offset(type, 0, anim, &first), -30);
            OBL_CHECK_EQ(Offset(type, 2, anim, &first), -30);
        }
    }
    // Only the first character decides, and it is case-sensitive.
    OBL_CHECK(!UsesFirstAnim(kShadow, 1, 'S'));
    OBL_CHECK(!UsesFirstAnim(kShadow, 1, 'L'));
    // Other types never pivot.
    OBL_CHECK(!UsesFirstAnim(kActor, 1, 's'));
}

OBL_TEST_CASE("render_geometry.offset_types_4_5_reject_directions_above_3")
{
    const AnimMetrics anim = Anim(-30, 64, 's');
    int offset = 777;
    for (int type : {kShadow, kBuilding}) {
        for (int direction : {4, 5, 7, 8, -1, INT_MIN}) {
            OBL_CHECK(!DrawOffsetX(type, direction, anim, nullptr, &offset));
            OBL_CHECK_EQ(offset, 777);
        }
    }
    // Other types ignore the direction entirely.
    OBL_CHECK(DrawOffsetX(kActor, 7, anim, nullptr, &offset));
    OBL_CHECK_EQ(offset, -30);
}

OBL_TEST_CASE("render_geometry.ftol_truncates_like_the_exe")
{
    OBL_CHECK_EQ(FloatToIntWrap(0.99), 0);
    OBL_CHECK_EQ(FloatToIntWrap(-0.99), 0);
    OBL_CHECK_EQ(FloatToIntWrap(-1.5), -1);
    OBL_CHECK_EQ(FloatToIntWrap(2.5), 2);
    OBL_CHECK_EQ(FloatToIntWrap(2147483647.9), INT_MAX);
    OBL_CHECK_EQ(FloatToIntWrap(-2147483648.9), INT_MIN);
    // float to int returns the low word of a 64-bit truncation: values beyond int wrap.
    OBL_CHECK_EQ(FloatToIntWrap(2147483648.0), INT_MIN);
    OBL_CHECK_EQ(FloatToIntWrap(-2147483650.0), 2147483646);
    OBL_CHECK_EQ(FloatToIntWrap(3e9), -1294967296);
    // NaN and anything outside int64 give the 64-bit integer indefinite value, low word 0.
    OBL_CHECK_EQ(FloatToIntWrap(std::numeric_limits<double>::quiet_NaN()), 0);
    OBL_CHECK_EQ(FloatToIntWrap(std::numeric_limits<double>::infinity()), 0);
    OBL_CHECK_EQ(FloatToIntWrap(-std::numeric_limits<double>::infinity()), 0);
    OBL_CHECK_EQ(FloatToIntWrap(1e19), 0);
}

OBL_TEST_CASE("render_geometry.map_point_halves_world_y")
{
    OBL_CHECK_EQ(WorldToMapX(100.75f), 100);
    OBL_CHECK_EQ(WorldToMapX(-3.5f), -3);
    OBL_CHECK_EQ(WorldToMapY(100.75f), 50);
    OBL_CHECK_EQ(WorldToMapY(1.0f), 0);
    OBL_CHECK_EQ(WorldToMapY(-3.0f), -1);
    OBL_CHECK_EQ(WorldToMapY(-1.0f), 0);
}

OBL_TEST_CASE("render_geometry.map_to_screen_subtracts_height_and_camera")
{
    const ScreenPoint p = MapToScreen(1000, 600, 25, 200, 100);
    OBL_CHECK_EQ(p.x, 800);
    OBL_CHECK_EQ(p.y, 475);
    const ScreenPoint negative = MapToScreen(10, 10, 0, 200, 100);
    OBL_CHECK_EQ(negative.x, -190);
    OBL_CHECK_EQ(negative.y, -90);
}

OBL_TEST_CASE("render_geometry.anchor_rect_adds_camera_back")
{
    const AnimMetrics anim = Anim(-8, 40, 'i', -20, 30);
    const Rect r = AnchorRect({800, 475}, 200, 100, -8, anim);
    OBL_CHECK_EQ(r.left, 800 + 200 - 8);
    OBL_CHECK_EQ(r.top, 475 + 100 - 20);
    OBL_CHECK_EQ(r.right, r.left + 40);
    OBL_CHECK_EQ(r.bottom, r.top + 30);
}

OBL_TEST_CASE("render_geometry.draw_depth_wraps_at_16_bits")
{
    OBL_CHECK_EQ(DrawDepth(100, 0), 108);
    OBL_CHECK_EQ(DrawDepth(100, 0x32), 0x32 + 108);
    OBL_CHECK_EQ(DrawDepth(0xfffa, 0), 2); // 16-bit wrap of the +8 bias
    OBL_CHECK_EQ(DrawDepth(0x12345678, 0), static_cast<uint16_t>(0x5678 + 8));
    OBL_CHECK_EQ(DrawDepth(0xfff0, 0x20), static_cast<uint16_t>(0xfff0 + 8 + 0x20));
}

OBL_TEST_CASE("render_geometry.in_world_depth_keeps_32_bits")
{
    OBL_CHECK_EQ(InWorldDepth(100, 0), 108u);
    OBL_CHECK_EQ(InWorldDepth(0x1ffff, 0), 0xffffu + 8u); // only the low 16 bits of the map depth count, no wrap after the bias
    OBL_CHECK_EQ(InWorldDepth(0xfffa, 0x20), 0xfffau + 8u + 0x20u);
    OBL_CHECK_EQ(InWorldDepth(0x12340005, 0x10), 5u + 8u + 0x10u);
}

OBL_TEST_CASE("render_geometry.toned_palette_needed_unless_neutral")
{
    OBL_CHECK(!NeedsTonedPalette(1.0f, 0.0f));
    OBL_CHECK(!NeedsTonedPalette(1.0f, -0.5f));
    OBL_CHECK(NeedsTonedPalette(1.0f, 0.01f));
    OBL_CHECK(NeedsTonedPalette(0.99f, 0.0f));
    OBL_CHECK(NeedsTonedPalette(1.5f, 0.0f));
    // flag tests: an unordered compare reads as "equal" for the scale and "not above" for the blend.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(!NeedsTonedPalette(nan, 0.0f));
    OBL_CHECK(!NeedsTonedPalette(nan, nan));
    OBL_CHECK(NeedsTonedPalette(nan, 0.5f));
    OBL_CHECK(!NeedsTonedPalette(1.0f, nan));
}
