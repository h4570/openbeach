// Animation clock and facing arithmetic of SpriteInstance (src/game/ui/sprite_animation.h): UpdateAnim's frame
// selection, SetAnimByName's lookup and duration, SetDirectionFromVector's quantisation and FaceTowards' normalisation.

#include "unit_test.h"

#include "core/ascii_case.h"
#include "game/ui/sprite_animation.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace obl::sprite_animation;

OBL_TEST_CASE("sprite_animation.loop_frame_is_elapsed_ticks_modulo_frames")
{
    // 18 fps, start at 10 s: at 10.5 s nine frames have elapsed.
    Step s = Advance(10.0f, 10.5f, 0, 18, 3, 12);
    OBL_CHECK_EQ(s.frame, 9);
    OBL_CHECK(!s.wrapped);
    OBL_CHECK_EQ(s.startTime, 10.0f);
    // 13 frames elapsed wrap to 1; the new frame is below the old one, so the sprite sound plays.
    s = Advance(10.0f, 10.0f + 13.0f / 18.0f + 0.001f, 0, 18, 9, 12);
    OBL_CHECK_EQ(s.frame, 1);
    OBL_CHECK(s.wrapped);
    // Staying on the same frame is not a wrap.
    s = Advance(0.0f, 0.2f, 0, 18, 3, 12);
    OBL_CHECK_EQ(s.frame, 3);
    OBL_CHECK(!s.wrapped);
}

OBL_TEST_CASE("sprite_animation.loop_modes_other_than_one_to_three_all_loop")
{
    for (int type : {0, 4, 5, -1, 99}) {
        const Step s = Advance(0.0f, 1.0f, type, 10, 0, 4);
        OBL_CHECK_EQ(s.frame, 2);
    }
}

OBL_TEST_CASE("sprite_animation.start_in_the_future_is_pulled_to_now")
{
    const Step s = Advance(50.0f, 20.0f, 0, 18, 5, 12);
    OBL_CHECK_EQ(s.startTime, 20.0f);
    OBL_CHECK_EQ(s.frame, 0);
    OBL_CHECK(s.wrapped);
}

OBL_TEST_CASE("sprite_animation.countdown_runs_from_the_last_frame_down_to_zero")
{
    OBL_CHECK_EQ(Advance(0.0f, 0.0f, kCountdown, 10, 0, 6).frame, 5);
    OBL_CHECK_EQ(Advance(0.0f, 0.25f, kCountdown, 10, 0, 6).frame, 3); // 6 - 2 - 1
    OBL_CHECK_EQ(Advance(0.0f, 0.5f, kCountdown, 10, 0, 6).frame, 0);
    OBL_CHECK_EQ(Advance(0.0f, 9.0f, kCountdown, 10, 0, 6).frame, 0);
    OBL_CHECK(!Advance(0.0f, 0.25f, kCountdown, 10, 5, 6).wrapped);
}

OBL_TEST_CASE("sprite_animation.play_once_stops_on_the_last_frame")
{
    OBL_CHECK_EQ(Advance(0.0f, 0.0f, kPlayOnce, 10, 3, 6).frame, 0);
    OBL_CHECK_EQ(Advance(0.0f, 0.35f, kPlayOnce, 10, 0, 6).frame, 3);
    OBL_CHECK_EQ(Advance(0.0f, 0.6f, kPlayOnce, 10, 0, 6).frame, 5);
    OBL_CHECK_EQ(Advance(0.0f, 100.0f, kPlayOnce, 10, 0, 6).frame, 5);
}

OBL_TEST_CASE("sprite_animation.external_frame_is_left_alone_and_flagged_when_out_of_range")
{
    Step s = Advance(0.0f, 5.0f, kExternalFrame, 10, 4, 6);
    OBL_CHECK_EQ(s.frame, 4);
    OBL_CHECK(!s.badFrame);
    s = Advance(0.0f, 5.0f, kExternalFrame, 10, 6, 6);
    OBL_CHECK_EQ(s.frame, 6);
    OBL_CHECK(s.badFrame);
    s = Advance(0.0f, 5.0f, kExternalFrame, 10, -1, 6);
    OBL_CHECK(!s.badFrame);
    // The clock still advances the start time.
    s = Advance(9.0f, 5.0f, kExternalFrame, 10, 0, 6);
    OBL_CHECK_EQ(s.startTime, 5.0f);
}

OBL_TEST_CASE("sprite_animation.no_frames_does_not_divide_by_zero")
{
    OBL_CHECK_EQ(Advance(0.0f, 1.0f, 0, 18, 0, 0).frame, 0);
}

OBL_TEST_CASE("sprite_animation.stamp_only_for_positive_time")
{
    OBL_CHECK(StampsAnimation(0.5f));
    OBL_CHECK(!StampsAnimation(0.0f));
    OBL_CHECK(!StampsAnimation(-1.0f));
    OBL_CHECK(!StampsAnimation(std::numeric_limits<float>::quiet_NaN()));
}

OBL_TEST_CASE("sprite_animation.duration_is_frames_over_fps")
{
    OBL_CHECK_EQ(Duration(18, 18), 1.0);
    OBL_CHECK_EQ(Duration(9, 18), 0.5);
    OBL_CHECK_EQ(Duration(10, 3), 10.0 / 3.0);
    OBL_CHECK(std::isinf(Duration(5, 0)));
    OBL_CHECK(std::isnan(Duration(0, 0)));
}

OBL_TEST_CASE("sprite_animation.names_compare_without_case")
{
    using obl::ascii::EqualsNoCase;
    OBL_CHECK(EqualsNoCase("walk_normal", "WALK_Normal"));
    OBL_CHECK(!EqualsNoCase("walk", "walk_normal"));
    OBL_CHECK(!EqualsNoCase("walk_normal", "walk"));
    OBL_CHECK(EqualsNoCase("", ""));
    // Only ASCII letters fold; bytes above 0x7f stay distinct.
    OBL_CHECK(!EqualsNoCase("\xc4", "\xe4"));
}

OBL_TEST_CASE("sprite_animation.anim_lookup_prefers_the_name_then_walk_normal_then_zero")
{
    const std::vector<const char*> names = {"idle", "Run", "walk_normal", "run"};
    auto nameAt = [&](int i) { return names[i]; };
    OBL_CHECK_EQ(FindAnim(4, nameAt, "run"), 1); // first match, any case
    OBL_CHECK_EQ(FindAnim(4, nameAt, "WALK_NORMAL"), 2);
    OBL_CHECK_EQ(FindAnim(4, nameAt, "missing"), 2); // falls back to walk_normal
    const std::vector<const char*> bare = {"idle", "stand"};
    auto bareAt = [&](int i) { return bare[i]; };
    OBL_CHECK_EQ(FindAnim(2, bareAt, "missing"), 0); // and finally to the first animation
    OBL_CHECK_EQ(FindAnim(0, bareAt, "missing"), 0);
}

OBL_TEST_CASE("sprite_animation.eight_directions_follow_the_unit_circle")
{
    // y grows downwards on screen; (0, -1) is direction 0 and the sectors go clockwise through (1, 0) = 2.
    const float r = 0.70710678f;
    const struct {
        float x, y;
        int dir;
    } cases[] = {{0, -1, 0}, {r, -r, 1}, {1, 0, 2}, {r, r, 3}, {0, 1, 4}, {-r, r, 5}, {-1, 0, 6}, {-r, -r, 7}};
    for (const auto& c : cases) OBL_CHECK_EQ(DirectionFromVector(c.x, c.y, 8), c.dir);
}

OBL_TEST_CASE("sprite_animation.sector_borders_round_to_the_nearest_direction")
{
    // 22.5 degrees is the border between directions 0 and 1; slightly inside each side picks that side.
    const double pi = 3.14159265358979323846;
    for (int dir = 0; dir < 8; ++dir) {
        const double base = dir * pi / 4.0;
        for (double offset : {-0.3, 0.0, 0.3}) {
            const double a = base + offset;
            const float x = static_cast<float>(std::sin(a)), y = static_cast<float>(-std::cos(a));
            OBL_CHECK_EQ(DirectionFromVector(x, y, 8), dir);
        }
    }
}

OBL_TEST_CASE("sprite_animation.four_direction_sprites_use_quarter_turns")
{
    OBL_CHECK_EQ(DirectionFromVector(0.0f, -1.0f, 4), 0);
    OBL_CHECK_EQ(DirectionFromVector(1.0f, 0.0f, 4), 1);
    OBL_CHECK_EQ(DirectionFromVector(0.0f, 1.0f, 4), 3); // the pole resolves to 4, past a four-direction sprite: clamps
    OBL_CHECK_EQ(DirectionFromVector(-1.0f, 0.0f, 4), 3);
    // A single-direction sprite always faces 0, a missing count gives 0.
    OBL_CHECK_EQ(DirectionFromVector(1.0f, 0.5f, 1), 0);
    OBL_CHECK_EQ(DirectionFromVector(1.0f, 0.5f, 0), 0);
}

OBL_TEST_CASE("sprite_animation.direction_clamps_to_the_last_direction_of_a_small_sprite")
{
    // dir 4 resolved from sector 0 by y > 0 does not exist in a two-direction sprite.
    OBL_CHECK_EQ(DirectionFromVector(0.0f, 1.0f, 2), 1);
    OBL_CHECK_EQ(DirectionFromVector(0.0f, 1.0f, 3), 2);
}

OBL_TEST_CASE("sprite_animation.direction_special_values")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(DirectionFromVector(nan, -1.0f, 8), 0);
    OBL_CHECK_EQ(DirectionFromVector(0.0f, nan, 8), 0);
    // Zero vector: acos(-0) = pi / 2 -> direction 2.
    OBL_CHECK_EQ(DirectionFromVector(0.0f, 0.0f, 8), 2);
    // Vectors longer than one clamp to the pole.
    OBL_CHECK_EQ(DirectionFromVector(0.0f, -5.0f, 8), 0);
    OBL_CHECK_EQ(DirectionFromVector(0.0f, 5.0f, 8), 4);
}

OBL_TEST_CASE("sprite_animation.sqrt_table_matches_the_exes_generator")
{
    const uint32_t* table = SqrtMantissaTable();
    OBL_CHECK_EQ(table[0x8000], 0u); // sqrt(1.0) = 1.0
    OBL_CHECK_EQ(table[0], 0x3504f3u); // sqrt(2.0) = 1.41421354 (float 0x3fb504f3)
    // sqrt(4 - epsilon) ends just below 2.0, sqrt(2 - epsilon) just below 1.41421.
    OBL_CHECK(table[0x7fff] > 0x7fff00u);
    for (int i = 0; i < kSqrtMantissaTableSize; ++i)
        if (table[i] > 0x7fffffu) {
            OBL_CHECK(false);
            break;
        }
}

// The reciprocal comes from an integer-subtraction guess (up to 12.5 % off) and one Newton step, so the length is
// within 1/64 below one, the direction exact.
OBL_TEST_CASE("sprite_animation.face_vector_is_a_nearly_unit_vector_toward_the_target")
{
    for (float angle = 0.1f; angle < 6.28f; angle += 0.37f) {
        const float tx = 100.0f * std::cos(angle), ty = 100.0f * std::sin(angle);
        const Vector2 v = FaceVector(tx, ty, 0.0f, 0.0f);
        const double length = std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y);
        OBL_CHECK(length <= 1.0 + 1e-5 && length > 1.0 - 1.0 / 64.0 - 1e-5);
        OBL_CHECK(std::fabs(v.x / length - std::cos(angle)) < 1e-5);
        OBL_CHECK(std::fabs(v.y / length - std::sin(angle)) < 1e-5);
    }
}

OBL_TEST_CASE("sprite_animation.face_vector_points_from_the_second_point_to_the_first")
{
    const Vector2 v = FaceVector(10.0f, 5.0f, 4.0f, 5.0f);
    OBL_CHECK(v.x > 0.98f && v.x <= 1.0001f);
    OBL_CHECK_EQ(v.y, 0.0f);
    const Vector2 w = FaceVector(3.0f, 3.0f, 3.0f, 7.0f);
    OBL_CHECK_EQ(w.x, 0.0f);
    OBL_CHECK(w.y < -0.98f && w.y >= -1.0001f);
}

OBL_TEST_CASE("sprite_animation.face_vector_of_coincident_points_is_nan")
{
    const Vector2 v = FaceVector(3.0f, 4.0f, 3.0f, 4.0f);
    OBL_CHECK(std::isnan(v.x));
    OBL_CHECK(std::isnan(v.y));
    OBL_CHECK_EQ(DirectionFromVector(v.x, v.y, 8), 0);
}

OBL_TEST_CASE("sprite_animation.face_vector_works_over_many_magnitudes")
{
    for (float scale : {1e-3f, 1.0f, 40.0f, 1e4f, 1e8f}) {
        const Vector2 v = FaceVector(3.0f * scale, -4.0f * scale, 0.0f, 0.0f);
        OBL_CHECK(v.x > 0.6f * 0.984f - 1e-4f && v.x <= 0.6f + 1e-4f);
        OBL_CHECK(v.y < -0.8f * 0.984f + 1e-4f && v.y >= -0.8f - 1e-4f);
    }
}
