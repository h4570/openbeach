#include "unit_test.h"
#include "game/objects/guest_conflict.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace obl;
using namespace obl::guest_conflict;

OBL_TEST_CASE("guest_conflict.walkable_tile_follows_the_two_flag_tests")
{
    OBL_CHECK(WalkableTile(0x80));
    OBL_CHECK(WalkableTile(0x10000000));
    OBL_CHECK(!WalkableTile(0x8080)); // blocked bit, and bit 2 absent
    OBL_CHECK(WalkableTile(0x8082 & ~0x8010)); // bit 2 without the blocked bits
    OBL_CHECK(WalkableTile(0x2));
    OBL_CHECK(!WalkableTile(0x10));
    OBL_CHECK(!WalkableTile(0x0));
    OBL_CHECK(WalkableTile(0x8000 | 0x2) == false);
    OBL_CHECK(WalkableTile(0x80 | 0x10)); // the first test does not look at bit 0x10
    OBL_CHECK(!WalkableTile(0x80 | 0x8000)); // ... but the blocked bit 0x8000 sends it to the second one, which needs bit 2
    OBL_CHECK(!WalkableTile(0x1 | 0x4));
}

OBL_TEST_CASE("guest_conflict.bar_distance_wraps_like_the_32_bit_sum")
{
    OBL_CHECK_EQ(BarDistance(3, 4), 5.0);
    OBL_CHECK_EQ(BarDistance(-3, -4), 5.0);
    OBL_CHECK_EQ(BarDistance(0, 0), 0.0);
    // 65536^2 wraps to 0 in the 32-bit multiply; the sum is read back as an unsigned 64-bit integer, never negative.
    OBL_CHECK_EQ(BarDistance(65536, 0), 0.0);
    OBL_CHECK_EQ(BarDistance(46341, 0), std::sqrt(static_cast<double>(46341u * 46341u)));
    OBL_CHECK(BarDistance(0x7fffffff, 0x7fffffff) >= 0.0);
}

OBL_TEST_CASE("guest_conflict.nearest_bar_keeps_the_first_of_equals")
{
    NearestBar nearest;
    OBL_CHECK_EQ(nearest.index, 0u);
    OBL_CHECK_EQ(nearest.best, kNoBarDistance);
    nearest.Offer(3, 12.0);
    OBL_CHECK_EQ(nearest.index, 3u);
    nearest.Offer(5, 12.0); // a tie keeps the first
    OBL_CHECK_EQ(nearest.index, 3u);
    nearest.Offer(6, 11.99);
    OBL_CHECK_EQ(nearest.index, 6u);
    nearest.Offer(7, 50000.0); // nothing is farther than the start value
    OBL_CHECK_EQ(nearest.index, 6u);
    // The best is stored as a float: a distance that rounds up to the stored value is not below it any more.
    NearestBar rounded;
    rounded.Offer(1, 1.0000000001);
    OBL_CHECK_EQ(rounded.index, 1u);
    OBL_CHECK_EQ(rounded.best, 1.0f);
    rounded.Offer(2, 1.00000000005);
    OBL_CHECK_EQ(rounded.index, 1u);
}

OBL_TEST_CASE("guest_conflict.nothing_is_nearer_than_the_start_value")
{
    NearestBar nearest;
    nearest.Offer(4, 50000.0);
    OBL_CHECK_EQ(nearest.index, 0u);
    nearest.Offer(4, 49999.5);
    OBL_CHECK_EQ(nearest.index, 4u);
}

OBL_TEST_CASE("guest_conflict.fight_jitter_spans_the_tiles_around_the_bar")
{
    for (int32_t draw = 0; draw < 15; ++draw) OBL_CHECK_EQ(FightJitter(40, draw), 40 - 7 + draw);
    OBL_CHECK_EQ(FightJitter(40, 15), 33);
    OBL_CHECK_EQ(FightJitter(40, 32767), 32767 % 15 + 33);
    // The signed remainder truncates towards zero, so a negative draw still lands within -14..0 of the base.
    OBL_CHECK_EQ(FightJitter(40, -1), 32);
}

OBL_TEST_CASE("guest_conflict.fight_tile_redraws_until_walkable_and_inside")
{
    std::vector<int32_t> draws = {0, 0, // (-7, -7) around (0, 0): outside the grid
            7, 7, // (0, 0) but not walkable
            14, 7}; // (7, 0) walkable
    size_t next = 0;
    int flagCalls = 0;
    const FightTile tile = PickFightTile(
            0, 0, [&] { return draws[next++]; },
            [&](int32_t x, int32_t y) -> uint32_t {
                ++flagCalls;
                return (x == 7 && y == 0)  ? 0x80u
                        : (x < 0 || y < 0) ? 0x80u
                                           : 0x0u; // negative tiles look walkable through the wrapped cell
            });
    OBL_CHECK_EQ(tile.x, 7);
    OBL_CHECK_EQ(tile.y, 0);
    OBL_CHECK_EQ(next, draws.size());
    OBL_CHECK_EQ(flagCalls, 3);
    // Draws come in the order x, y.
    const FightTile asymmetric =
            PickFightTile(20, 50, [n = 0]() mutable { return n++ == 0 ? 3 : 9; }, [](int32_t, int32_t) { return 0x80u; });
    OBL_CHECK_EQ(asymmetric.x, 20 - 7 + 3);
    OBL_CHECK_EQ(asymmetric.y, 50 - 7 + 9);
    // The edges of the grid: 127 is inside, 128 is not.
    const FightTile edge = PickFightTile(127, 127, [n = 0]() mutable { return n++ < 2 ? 14 : 7; }, [](int32_t, int32_t) { return 0x80u; });
    OBL_CHECK_EQ(edge.x, 127);
    OBL_CHECK_EQ(edge.y, 127);
}

OBL_TEST_CASE("guest_conflict.opponent_approach_is_a_float_subtraction")
{
    OBL_CHECK_EQ(OpponentApproachX(100.0f), 80.0f);
    OBL_CHECK_EQ(OpponentApproachX(0.0f), -20.0f);
    OBL_CHECK_EQ(OpponentApproachX(20.0f), 0.0f);
    OBL_CHECK_EQ(OpponentApproachX(3999.9f), 3999.9f - 20.0f);
    OBL_CHECK(std::isnan(OpponentApproachX(std::numeric_limits<float>::quiet_NaN())));
}

OBL_TEST_CASE("guest_conflict.only_guests_on_their_way_to_a_desire_can_be_fought")
{
    OBL_CHECK(CanBeFought(0xd));
    OBL_CHECK(CanBeFought(0x1f));
    OBL_CHECK(!CanBeFought(0x20));
    OBL_CHECK(!CanBeFought(0xc));
    OBL_CHECK(!CanBeFought(0));
}

OBL_TEST_CASE("guest_conflict.vandal_metric_is_abs_dx_plus_dy_squared")
{
    OBL_CHECK_EQ(VandalMetric(0, 0), 0);
    OBL_CHECK_EQ(VandalMetric(5, 0), 5);
    OBL_CHECK_EQ(VandalMetric(-5, 0), 5);
    OBL_CHECK_EQ(VandalMetric(0, 3), 9);
    OBL_CHECK_EQ(VandalMetric(-2, -3), 11);
    OBL_CHECK_EQ(VandalMetric(7, 1), 8);
    // dx * dx wraps in 32 bits: 65536^2 is 0, and a wrapped negative square makes a square root give NaN, which the float-to-int conversion turns into 0.
    OBL_CHECK_EQ(VandalMetric(65536, 0), 0);
    OBL_CHECK_EQ(VandalMetric(50000, 0), 0); // 50000^2 = 2.5e9 wraps negative
    OBL_CHECK_EQ(VandalMetric(46340, 0), 46340);
    OBL_CHECK_EQ(Abs32(-5), 5);
    OBL_CHECK_EQ(Abs32(INT32_MIN), INT32_MIN);
}

OBL_TEST_CASE("guest_conflict.vandal_search_prefers_later_candidates_on_ties")
{
    VandalSearch search;
    OBL_CHECK(search.Reaches(10));
    OBL_CHECK(!search.Reaches(11));
    search.Take(6, Bin);
    OBL_CHECK(search.Reaches(6)); // a later kind wins a tie
    OBL_CHECK(!search.Reaches(7));
    search.Take(6, Bench);
    OBL_CHECK_EQ(search.kind, static_cast<int32_t>(Bench));
    OBL_CHECK_EQ(search.best, 6.0f);
    OBL_CHECK_EQ(VandalSearch{}.kind, kKindNone);
    OBL_CHECK(search.Reaches(-3));
}

OBL_TEST_CASE("guest_conflict.vandal_target_is_free_unless_claimed_in_any_way")
{
    OBL_CHECK(VandalTargetFree(0, 0, 0, 0));
    OBL_CHECK(VandalTargetFree(2, 0, 0, 0));
    OBL_CHECK(!VandalTargetFree(4, 0, 0, 0));
    OBL_CHECK(!VandalTargetFree(0, 1, 0, 0));
    OBL_CHECK(!VandalTargetFree(0, 0, 1, 0));
    OBL_CHECK(!VandalTargetFree(0, 0, 0, 1));
}

OBL_TEST_CASE("guest_conflict.vandal_standing_offsets_follow_the_tables")
{
    const float binA[8] = {0, -15, -30, -15, 0, 15, 30, 15};
    const float otherA[8] = {0, -15, -20, -15, 0, 15, 20, 15};
    const float b[8] = {20, 15, 0, -15, -20, -15, 0, 15};
    for (uint32_t d = 0; d < 8; ++d) {
        OBL_CHECK_EQ(VandalStandingOffset(Bin, d).a, binA[d]);
        OBL_CHECK_EQ(VandalStandingOffset(Bench, d).a, otherA[d]);
        OBL_CHECK_EQ(VandalStandingOffset(StreetLamp, d).a, otherA[d]);
        OBL_CHECK_EQ(VandalStandingOffset(Bin, d).b, b[d]);
    }
}

OBL_TEST_CASE("guest_conflict.vandal_action_state_and_fruitless_search")
{
    OBL_CHECK_EQ(VandalActionState(Bin, true), 2);
    OBL_CHECK_EQ(VandalActionState(Bin, false), 3);
    OBL_CHECK_EQ(VandalActionState(Bench, true), 4);
    OBL_CHECK_EQ(VandalActionState(StreetLamp, false), 5);
    OBL_CHECK_EQ(VandalisedToFight(0.25f, 0.5f), 0.75f);
    OBL_CHECK_EQ(VandalisedToFight(0.7f, 0.6f), 1.0f);
    OBL_CHECK_EQ(VandalisedToFight(1.0f, 0.0f), 1.0f);
    OBL_CHECK_EQ(VandalisedToFight(-0.5f, 0.2f), static_cast<float>(-0.5 + static_cast<double>(0.2f)));
    OBL_CHECK(std::isnan(VandalisedToFight(std::numeric_limits<float>::quiet_NaN(), 0.5f)));
}

OBL_TEST_CASE("guest_conflict.topless_witnesses_are_the_men_of_the_neighbourhood")
{
    OBL_CHECK(IsToplessWitness(0)); // GuestDef::m_Gender 0 = male
    OBL_CHECK(!IsToplessWitness(1));
    OBL_CHECK(!IsToplessWitness(2));
    OBL_CHECK(!IsToplessWitness(-1));
    OBL_CHECK_EQ(kToplessRadius, 1);
    OBL_CHECK_EQ(kSpotTopless, 4);
}
