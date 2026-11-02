#include "unit_test.h"

#include "game/objects/guest_appearance.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace obl;
using namespace obl::guest_appearance;

namespace {

// Returns the scripted draws in order and counts them.
struct Script {
    std::vector<int> draws;
    int used = 0;
    int operator()() { return used < static_cast<int>(draws.size()) ? draws[used++] : (++used, 0); }
};

const float kNaN = std::numeric_limits<float>::quiet_NaN();

} // namespace

OBL_TEST_CASE("guest_appearance.food_ill_caps_without_lower_bound_or_nan_clamp")
{
    OBL_CHECK_EQ(AddFoodIll(0.25f, 0.5f), 0.75f);
    OBL_CHECK_EQ(AddFoodIll(0.75f, 0.25f), 1.0f);
    OBL_CHECK_EQ(AddFoodIll(0.75f, 0.5f), 1.0f);
    OBL_CHECK_EQ(AddFoodIll(0.25f, -0.5f), -0.25f);
    OBL_CHECK_EQ(AddFoodIll(std::nextafter(1.0f, 0.0f), -0x1p-25f), 0.99999988079071044921875f);
    OBL_CHECK_EQ(AddFoodIll(1.0f, 0x1p-25f), 1.0f);
    OBL_CHECK(std::isnan(AddFoodIll(kNaN, 0.5f)));
    OBL_CHECK(std::isnan(AddFoodIll(0.5f, kNaN)));
    const float inf = std::numeric_limits<float>::infinity();
    OBL_CHECK_EQ(AddFoodIll(0.5f, inf), 1.0f);
    OBL_CHECK_EQ(AddFoodIll(0.5f, -inf), -inf);
    OBL_CHECK(std::isnan(AddFoodIll(inf, -inf)));
}

OBL_TEST_CASE("guest_appearance.female_palette_draws_group_then_shade")
{
    // Female: draw & 3 picks the group (the first draw), the second draw % 7 + 2 the shade inside it.
    Script script{{6, 10}};
    OBL_CHECK_EQ(RandomPalette(1, false, false, 100, script), 2 * 10 + 10 % 7 + 2);
    OBL_CHECK_EQ(script.used, 2);
    // A coloured guest takes the first palette of its group with no second draw.
    Script coloured{{7}};
    OBL_CHECK_EQ(RandomPalette(1, true, false, 100, coloured), 3 * 10 + 1);
    OBL_CHECK_EQ(coloured.used, 1);
}

OBL_TEST_CASE("guest_appearance.male_palette_groups_follow_the_sprite_name")
{
    Script fat{{8, 3}};
    OBL_CHECK_EQ(RandomPalette(0, false, true, 100, fat), (8 % 3) * 10 + 3 % 7 + 2); // FAT_BLOKE: three groups
    Script other{{8, 3}};
    OBL_CHECK_EQ(RandomPalette(0, false, false, 100, other), (8 % 6) * 10 + 3 % 7 + 2); // any other male sprite: six
    Script fatColoured{{5}};
    OBL_CHECK_EQ(RandomPalette(2, true, true, 100, fatColoured), (5 % 3) * 10 + 1);
}

OBL_TEST_CASE("guest_appearance.palette_is_clamped_to_the_last_one_of_the_sprite")
{
    Script script{{3, 6}};
    OBL_CHECK_EQ(RandomPalette(1, false, false, 25, script), 24); // 3 * 10 + 6 + 2 = 38 past 25 palettes
    Script exact{{0, 0}};
    OBL_CHECK_EQ(RandomPalette(1, false, false, 2, exact), 1); // 0 * 10 + 0 + 2 = 2 is not below 2
    Script none{{5}};
    OBL_CHECK_EQ(RandomPalette(1, true, false, 0, none), -1); // a sprite without palettes: count - 1
}

OBL_TEST_CASE("guest_appearance.attached_luggage_follows_the_guest_palette")
{
    Script script;
    // The guest wears its last palette: the luggage gets its own last one and nothing is drawn.
    auto last = RandomAttachedPalette(true, 4, 5, 8, script);
    OBL_CHECK(last.has_value());
    OBL_CHECK_EQ(*last, 7);
    OBL_CHECK_EQ(script.used, 0);
    // Otherwise a draw % (count - 1): the last palette is not used.
    Script drawn{{20}};
    auto other = RandomAttachedPalette(true, 1, 5, 8, drawn);
    OBL_CHECK_EQ(*other, 20 % 7);
    // Anything else attached takes a draw % count.
    Script plain{{20}};
    auto any = RandomAttachedPalette(false, 4, 5, 8, plain);
    OBL_CHECK_EQ(*any, 20 % 8);
    OBL_CHECK_EQ(plain.used, 1);
}

OBL_TEST_CASE("guest_appearance.attached_palette_with_a_zero_modulus_gives_no_palette")
{
    Script script;
    OBL_CHECK(!RandomAttachedPalette(true, 0, 5, 1, script).has_value()); // count - 1 = 0
    OBL_CHECK(!RandomAttachedPalette(false, 0, 5, 0, script).has_value()); // count = 0
    OBL_CHECK_EQ(script.used, 0);
    // A single palette is fine for anything but luggage; luggage wearing its guest's last one never reaches the modulus.
    // The optionals are named: CHECK_EQ binds `*temporary` by reference, which would dangle (ASan: stack-use-after-scope).
    Script one{{9}};
    const auto single = RandomAttachedPalette(false, 0, 5, 1, one);
    OBL_REQUIRE(single.has_value());
    OBL_CHECK_EQ(*single, 0);
    const auto luggage = RandomAttachedPalette(true, 4, 5, 1, script);
    OBL_REQUIRE(luggage.has_value());
    OBL_CHECK_EQ(*luggage, 0);
    // A negative count gives a negative modulus: the remainder is 0, which is not below the count, so the clamp gives count - 1.
    Script negative{{20}};
    const auto clamped = RandomAttachedPalette(false, 0, 5, -1, negative);
    OBL_REQUIRE(clamped.has_value());
    OBL_CHECK_EQ(*clamped, -2);
}

OBL_TEST_CASE("guest_appearance.palette_tone_of_a_sunburnt_guest")
{
    OBL_CHECK_EQ(PaletteTone(-0.25f, 1, 9.0f, 0, 3.0f), 1.25);
    OBL_CHECK_EQ(PaletteTone(-5.0f, 1, 9.0f, 0, 3.0f), 2.0); // capped at 2
    OBL_CHECK_EQ(PaletteTone(-1.0f, 1, 9.0f, 0, 3.0f), 2.0); // exactly 2
    OBL_CHECK(std::isnan(PaletteTone(kNaN, 1, 9.0f, 0, 3.0f))); // a NaN level takes the sunburnt branch and stays a NaN
}

OBL_TEST_CASE("guest_appearance.palette_tone_fades_in_over_three_days")
{
    // (today - arrived) days of 24 hours plus the clock difference, over 72, at most 1.
    OBL_CHECK_EQ(PaletteTone(0.0f, 0, 12.0f, 0, 0.0f), static_cast<double>(12.0f) * static_cast<double>(0.013888889f));
    OBL_CHECK_EQ(PaletteTone(0.5f, 1, 6.0f, 0, 18.0f), (static_cast<double>(6.0f - 18.0f) + 24.0) * static_cast<double>(0.013888889f));
    OBL_CHECK_EQ(PaletteTone(0.0f, 5, 0.0f, 0, 0.0f), 1.0); // past 72 hours
    // Arriving after the current clock (a loaded game's old day counter) gives a negative tone: not clamped below.
    OBL_CHECK(PaletteTone(0.0f, 0, 1.0f, 0, 10.0f) < 0.0);
}

OBL_TEST_CASE("guest_appearance.sunburn_severity_is_the_negated_level_up_to_one")
{
    OBL_CHECK_EQ(SunburnSeverity(0.0f), 0.0f);
    OBL_CHECK_EQ(SunburnSeverity(0.7f), 0.0f); // the level is positive while the guest is not burnt
    OBL_CHECK_EQ(SunburnSeverity(-0.4f), 0.4f);
    OBL_CHECK_EQ(SunburnSeverity(-3.0f), 1.0f);
    OBL_CHECK(std::isnan(SunburnSeverity(kNaN))); // NaN takes the burnt branch (unordered counts as below)
}

OBL_TEST_CASE("guest_appearance.approachable_states")
{
    for (uint32_t state = 0; state < 0x100; ++state) {
        const bool expected = state == 0xc || state == 0xd || state == 0x3a;
        OBL_CHECK_EQ(IsApproachable(state), expected);
    }
    OBL_CHECK(!IsApproachable(0xffffffffu)); // an unsigned compare: -1 is far above 0xb, and not 0x3a
}

OBL_TEST_CASE("guest_appearance.states_with_a_handler")
{
    int count = 0;
    for (uint32_t state = 0; state < 0x100; ++state) count += HasState(state);
    OBL_CHECK_EQ(count, 2 + 1 + 8 + 44); // 0, 1, 5, 0xc..0x13, 0x15..0x40
    OBL_CHECK(HasState(0) && HasState(1) && HasState(5) && HasState(0xc) && HasState(0x13) && HasState(0x15) && HasState(0x40));
    OBL_CHECK(
            !HasState(2) && !HasState(4) && !HasState(6) && !HasState(0xb) && !HasState(0x14) && !HasState(0x41) && !HasState(0xffffffffu));
}
