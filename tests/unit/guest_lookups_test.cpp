#include "unit_test.h"

#include "game/objects/guest_lookups.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::guest_lookup;

namespace {

// The complaints and wharf areas, step by step for one attempt: the x offset is stored to a float, the y offset stays a double.
struct ExpectedSpot {
    float x, y;
};
ExpectedSpot ExpectedSpotOf(const HuddleArea& area, float sx, float sy, int32_t drawX, int32_t drawY)
{
    float offsetX = static_cast<float>(static_cast<double>(drawX) * static_cast<double>(script::kRandUnit) * area.span -
            area.half); // draw * unit * span - half, stored as float
    double offsetY =
            static_cast<double>(drawY) * static_cast<double>(script::kRandUnit) * area.span - area.half; // draw * unit * span - half
    const float x = static_cast<float>(static_cast<double>(sx) + static_cast<double>(offsetX)); // sx + offset, stored as float
    const float y = static_cast<float>(offsetY + static_cast<double>(sy)); // plus sy, stored as float
    return {x, y};
}

} // namespace

OBL_TEST_CASE("guest_lookups.huddle_first_attempt_matches_the_expected_spot")
{
    const float serve[][2] = {{2000.5f, 4000.25f}, {17.3f, 8000.9f}, {4000.0f, 12.0f}, {1234.567f, 7654.321f}};
    for (const HuddleArea* area : {&kComplaintsArea, &kWharfArea}) {
        for (const auto& s : serve) {
            for (int32_t a : {0, 1, 100, 16383, 32767})
                for (int32_t b : {0, 7, 20000, 32767}) {
                    const int32_t draws[] = {a, b};
                    int next = 0;
                    const ExpectedSpot expected = ExpectedSpotOf(*area, s[0], s[1], a, b);
                    const bool inside = expected.x < kMapWidth && 0.0f <= expected.x && expected.y < kMapHeight && 0.0f <= expected.y;
                    if (!inside) continue; // the same draws would repeat for ever (retries are tested below)
                    const HuddleSpot spot = FindHuddleSpot(
                            *area, s[0], s[1], [&] { return draws[next++ % 2]; },
                            [](float, float) { return 0xffffffffu & ~0x8010u & ~0x8000u; });
                    OBL_CHECK_EQ(spot.x, expected.x);
                    OBL_CHECK_EQ(spot.y, expected.y);
                    OBL_CHECK_EQ(next, 2); // two draws, nothing more
                }
        }
    }
}

OBL_TEST_CASE("guest_lookups.huddle_cell_rules_differ_by_building")
{
    // The complaints hut wants 0x10000080 and refuses 0x8010; the wharf wants 0x10000082 and refuses 0x8000.
    auto probe = [](const HuddleArea& area, uint32_t flags) {
        int draws = 0;
        FindHuddleSpot(
                area, 2000.0f, 4000.0f,
                [&] {
                    ++draws;
                    return 16000;
                },
                [&](float, float) { return draws >= 6 ? 0x10000000u : flags; });
        return draws; // 2 = taken at once, 6 = the cell was rejected twice
    };
    OBL_CHECK_EQ(probe(kComplaintsArea, 0x80u), 2);
    OBL_CHECK_EQ(probe(kComplaintsArea, 0x10000000u), 2);
    OBL_CHECK_EQ(probe(kComplaintsArea, 0x2u), 6); // 0x2 is wanted only by the wharf
    OBL_CHECK_EQ(probe(kWharfArea, 0x2u), 2);
    OBL_CHECK_EQ(probe(kComplaintsArea, 0x10000080u | 0x10u), 6);
    OBL_CHECK_EQ(probe(kWharfArea, 0x10000080u | 0x10u), 2); // 0x10 is refused only by the complaints hut
    OBL_CHECK_EQ(probe(kWharfArea, 0x10000080u | 0x8000u), 6);
    OBL_CHECK_EQ(probe(kComplaintsArea, 0x10000080u | 0x8000u), 6);
    OBL_CHECK_EQ(probe(kComplaintsArea, 0u), 6);
}

OBL_TEST_CASE("guest_lookups.huddle_thousandth_miss_goes_to_the_serve_position")
{
    int draws = 0;
    const HuddleSpot spot = FindHuddleSpot(
            kComplaintsArea, 1000.5f, 2000.25f,
            [&] {
                ++draws;
                return 12345;
            },
            [](float, float) { return 0u; });
    OBL_CHECK_EQ(draws, 2 * (kHuddleAttempts + 1)); // two draws for the first point and for each of the thousand retries
    OBL_CHECK_EQ(spot.x, 1000.5f);
    OBL_CHECK_EQ(spot.y, 2000.25f);
}

OBL_TEST_CASE("guest_lookups.huddle_miss_after_the_thousandth_keeps_the_candidate")
{
    // The thousandth retry is off the map (draw 0 puts x at serve - 50 < 0), so the next one is taken and is not replaced by the serve position.
    int draws = 0;
    const HuddleSpot spot = FindHuddleSpot(
            kComplaintsArea, 40.0f, 4000.0f, [&] { return draws++ == 2 * kHuddleAttempts ? 0 : 16384; }, [](float, float) { return 0u; });
    OBL_CHECK_EQ(draws, 2 * (kHuddleAttempts + 2));
    OBL_CHECK(spot.x != 40.0f && std::fabs(spot.x - 40.0f) < 0.01f);
}

OBL_TEST_CASE("guest_lookups.huddle_off_map_points_are_retried_even_when_the_cell_fits")
{
    // Serve position at the left edge: draw 0 gives x - 50 < 0 (retried), the next draw pair is inside.
    int draws = 0;
    const int32_t sequence[] = {0, 16384, 32767, 16384};
    const HuddleSpot spot =
            FindHuddleSpot(kComplaintsArea, 10.0f, 4000.0f, [&] { return sequence[draws++]; }, [](float, float) { return 0x80u; });
    OBL_CHECK_EQ(draws, 4);
    OBL_CHECK(spot.x >= 0.0f && spot.x < 70.0f);
    OBL_CHECK(std::fabs(spot.y - 4000.0f) < 1.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(!OnMap(nan, 100.0f));
    OBL_CHECK(!OnMap(100.0f, nan));
    OBL_CHECK(OnMap(0.0f, 0.0f) && OnMap(4095.99f, 8191.99f));
    OBL_CHECK(!OnMap(4096.0f, 0.0f) && !OnMap(0.0f, 8192.0f) && !OnMap(-0.01f, 5.0f) && !OnMap(5.0f, -0.01f));
}

OBL_TEST_CASE("guest_lookups.bench_distance_wraps_in_32_bits")
{
    OBL_CHECK_EQ(BenchDistance(0, 0, 3, 4), 5.0);
    OBL_CHECK_EQ(BenchDistance(10, 10, 10, 10), 0.0);
    OBL_CHECK_EQ(BenchDistance(-3, 4, 0, 0), 5.0);
    // 46341^2 overflows a signed 32-bit sum: the integer is read as the wrapped (negative) integer and its square root is NaN.
    OBL_CHECK(std::isnan(BenchDistance(46341, 0, 0, 0)));
}

OBL_TEST_CASE("guest_lookups.nearest_bench_ties_go_to_the_later_one_within_five")
{
    NearestBench nearest(4);
    nearest.Offer(0, 5.0); // equal to the start: taken
    OBL_CHECK_EQ(nearest.index, 0u);
    nearest.Offer(1, 5.0); // equal to the best (5.0): the later wins
    OBL_CHECK_EQ(nearest.index, 1u);
    nearest.Offer(2, 5.5); // beyond: ignored
    OBL_CHECK_EQ(nearest.index, 1u);
    nearest.Offer(3, 2.0);
    OBL_CHECK_EQ(nearest.index, 3u);
    OBL_CHECK_EQ(nearest.best, 2.0f);

    NearestBench none(3);
    none.Offer(0, 5.000001); // the double beyond the float limit
    OBL_CHECK_EQ(none.index, 3u); // nothing chosen: the index stays at the count
    none.Offer(1, std::numeric_limits<double>::quiet_NaN()); // unordered sets the flags of "below or equal": it wins ...
    OBL_CHECK_EQ(none.index, 1u);
    none.Offer(2, 100.0); // ... and, as the best is NaN now, so does every later bench
    OBL_CHECK_EQ(none.index, 2u);
}

OBL_TEST_CASE("guest_lookups.nearest_bench_stores_the_best_as_float")
{
    NearestBench nearest(2);
    nearest.Offer(0, 4.123456789012345);
    OBL_CHECK_EQ(nearest.best, static_cast<float>(4.123456789012345));
    nearest.Offer(1, 4.12345680); // above the double, but below the rounded float limit? 4.1234567 float is 4.1234569549560547
    OBL_CHECK_EQ(nearest.index, 1u);
}

OBL_TEST_CASE("guest_lookups.bench_room_is_unsigned")
{
    OBL_CHECK(BenchHasRoom(0, 2));
    OBL_CHECK(!BenchHasRoom(2, 2));
    OBL_CHECK(!BenchHasRoom(3, 2));
    OBL_CHECK(BenchHasRoom(-1, 2) == false); // -1 is 4294967295 unsigned
    OBL_CHECK(BenchHasRoom(1, -1)); // capacity -1 is the largest
}

OBL_TEST_CASE("guest_lookups.litter_type_follows_the_animation")
{
    int draws = 0;
    int next = 0;
    auto draw = [&] {
        ++draws;
        return next;
    };
    next = 1;
    OBL_CHECK_EQ(std::string(LitterType("D_STAND_DROPDRINK", draw)), "Littercancrush");
    next = 2;
    OBL_CHECK_EQ(std::string(LitterType("D_WALK_DRINK", draw)), "Littercanstand");
    OBL_CHECK_EQ(draws, 2);
    OBL_CHECK_EQ(std::string(LitterType("D_STAND_DROPICE", draw)), "Littericesplat");
    OBL_CHECK_EQ(std::string(LitterType("d_stand_dropice", draw)), "Littericesplat");
    OBL_CHECK_EQ(std::string(LitterType("D_STAND_DROPBURGER", draw)), "Litterburger");
    OBL_CHECK_EQ(std::string(LitterType("D_WALK_BURGER", draw)), "Litterburger");
    OBL_CHECK_EQ(std::string(LitterType("D_STAND_VOMIT", draw)), "LitterPuke");
    OBL_CHECK_EQ(draws, 2); // only the drinks draw
    for (const char* other : {"", "D_STAND_DROPDRINK2", "D_STAND_DROPDRIN", "D_WALK_BURGE", "D_STAND_VOMITS", "D_WALK"})
        OBL_CHECK(LitterType(other, draw) == nullptr);
    OBL_CHECK_EQ(draws, 2);
}

OBL_TEST_CASE("guest_lookups.litter_draw_parity_is_signed")
{
    // rand & 0x80000001, then the sign fix-up; zero picks the standing can. Negative draws cannot come from rand but the arithmetic is the same.
    for (int32_t draw : {0, 1, 2, 32767, 32766, -1, -2}) {
        uint32_t value = static_cast<uint32_t>(draw) & 0x80000001u;
        if (static_cast<int32_t>(value) < 0) value = ((value - 1) | 0xfffffffeu) + 1;
        const char* expected = value == 0 ? "Littercanstand" : "Littercancrush";
        OBL_CHECK_EQ(std::string(LitterType("D_WALK_DRINK", [&] { return draw; })), expected);
    }
}

// ---- GetConsumePoint ----

OBL_TEST_CASE("guest_lookups.consume_cell_rules")
{
    // bit tests on 0x10000080, 0x8000, bit 1 and 0x8010.
    struct Row {
        uint32_t flags;
        bool fits;
    };
    const Row rows[] = {
            {0x00000000, false},
            {0x00000080, true},
            {0x10000000, true},
            {0x00008080, false},
            {0x10008000, false},
            {0x00000002, true},
            {0x00000012, false},
            {0x00008002, false},
            {0x00008082, false},
            {0x00000082, true},
            {0x00000010, false},
            {0x00000080 | 0x10, true},
            {0x00008002 | 0x80, false},
            {0x00000102, true},
            {0x00010002, true},
            {0x00000008, false},
    };
    for (const Row& row : rows) OBL_CHECK_EQ(ConsumeCellFits(row.flags), row.fits);
    // The two clauses are independent: 0x8000 spoils only the first one, 0x10 only the second.
    OBL_CHECK(ConsumeCellFits(0x80u | 0x2u | 0x10u));
    OBL_CHECK(ConsumeCellFits(0x80u | 0x2u | 0x8000u) == false);
    for (uint32_t flags = 0; flags < 0x20000u; flags += 0x1u) {
        const bool first = (flags & 0x10000080u) != 0 && (flags & 0x8000u) == 0;
        const bool second = (flags & 2u) != 0 && (flags & 0x8010u) == 0;
        OBL_CHECK_EQ(ConsumeCellFits(flags), first || second);
    }
}

OBL_TEST_CASE("guest_lookups.consume_partner_point_is_one_rounding_of_x_minus_twenty")
{
    const float world[3] = {1234.567f, 2222.25f, 7.5f};
    const Point3 point = PartnerPoint(world);
    OBL_CHECK_EQ(point.x, static_cast<float>(static_cast<double>(1234.567f) - 20.0));
    OBL_CHECK_EQ(point.y, 2222.25f);
    OBL_CHECK_EQ(point.z, 7.5f);
    // A value too large for the float's last digits keeps the double subtraction (not a float subtraction rounded twice).
    const float big[3] = {33554436.0f, 0.0f, 0.0f};
    OBL_CHECK_EQ(PartnerPoint(big).x, static_cast<float>(33554436.0 - 20.0));
}

OBL_TEST_CASE("guest_lookups.consume_map_bounds")
{
    // y < 8192, x >= 0, y >= 0 and (x <= 4096) are tested; a NaN fails the x >= 0 or the y >= 0 compare.
    OBL_CHECK(ConsumeOnMap(0.0f, 0.0f));
    OBL_CHECK(ConsumeOnMap(4096.0f, 8191.99f)); // the x bound is inclusive
    OBL_CHECK(!ConsumeOnMap(4096.01f, 100.0f));
    OBL_CHECK(!ConsumeOnMap(100.0f, 8192.0f));
    OBL_CHECK(!ConsumeOnMap(-0.01f, 100.0f));
    OBL_CHECK(!ConsumeOnMap(100.0f, -0.01f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(!ConsumeOnMap(nan, 100.0f));
    OBL_CHECK(!ConsumeOnMap(100.0f, nan));
    OBL_CHECK(ConsumeOnMap(-0.0f, 0.0f));
}

OBL_TEST_CASE("guest_lookups.consume_jitter_is_draw_mod_500_minus_250")
{
    OBL_CHECK_EQ(ConsumeJitter(0, 100.0f), -150.0f);
    OBL_CHECK_EQ(ConsumeJitter(499, 100.0f), 349.0f);
    OBL_CHECK_EQ(ConsumeJitter(500, 100.0f), -150.0f);
    OBL_CHECK_EQ(ConsumeJitter(32767, 1000.0f), 1000.0f + static_cast<float>(32767 % 500 - 250));
    OBL_CHECK_EQ(ConsumeJitter(250, 12.5f), 12.5f);
    // The signed remainder: a negative draw gives a negative remainder (rand() cannot produce one, the arithmetic is the same).
    OBL_CHECK_EQ(ConsumeJitter(-1, 0.0f), -251.0f);
}

OBL_TEST_CASE("guest_lookups.consume_spot_draws_x_then_y_and_retries_from_home")
{
    std::vector<int32_t> draws = {250, 300, 100, 100, 260, 255}; // offsets 0 / 50, -150 / -150, 10 / 5
    size_t next = 0;
    std::vector<Point3> asked;
    const Point3 first{1000.0f, 2000.0f, 3.0f}, home{500.0f, 700.0f, 9.0f};
    const Point3 spot = FindConsumeSpot(
            first, home, [&] { return draws[next++]; },
            [&](float x, float y, float z) {
                asked.push_back({x, y, z});
                return asked.size() < 3 ? 0u : 0x80u; // the third try fits
            });
    OBL_CHECK_EQ(next, 6u);
    OBL_REQUIRE(asked.size() == 3u);
    OBL_CHECK_EQ(asked[0].x, 1000.0f);
    OBL_CHECK_EQ(asked[0].y, 2050.0f);
    OBL_CHECK_EQ(asked[0].z, 3.0f); // the first try starts from the first point, z included
    OBL_CHECK_EQ(asked[1].x, 350.0f);
    OBL_CHECK_EQ(asked[1].y, 550.0f);
    OBL_CHECK_EQ(asked[1].z, 9.0f); // every retry from home
    OBL_CHECK_EQ(spot.x, 510.0f);
    OBL_CHECK_EQ(spot.y, 705.0f);
    OBL_CHECK_EQ(spot.z, 9.0f);
}

OBL_TEST_CASE("guest_lookups.consume_spot_off_the_map_is_retried_even_when_the_cell_fits")
{
    // a fitting cell outside 0..4096 x 0..8192 draws again.
    std::vector<int32_t> draws = {0, 0, 250, 250}; // the first point is 250 left of x = 100, the second is on the spot
    size_t next = 0;
    const Point3 spot = FindConsumeSpot(
            Point3{100.0f, 100.0f, 0.0f}, Point3{100.0f, 100.0f, 0.0f}, [&] { return draws[next++]; },
            [](float, float, float) { return 0x80u; });
    OBL_CHECK_EQ(next, 4u);
    OBL_CHECK_EQ(spot.x, 100.0f);
    OBL_CHECK_EQ(spot.y, 100.0f);
}

OBL_TEST_CASE("guest_lookups.totty_taste_matches_equal_and_unordered_values")
{
    // the partner's attractiveness equals my preference (unordered counts as equal too).
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(TottyTasteMatches(0.5f, 0.5f));
    OBL_CHECK(TottyTasteMatches(0.0f, -0.0f));
    OBL_CHECK(!TottyTasteMatches(0.5f, 0.25f));
    OBL_CHECK(!TottyTasteMatches(0.25f, 0.5f));
    OBL_CHECK(TottyTasteMatches(nan, 0.5f));
    OBL_CHECK(TottyTasteMatches(0.5f, nan));
    OBL_CHECK(TottyTasteMatches(nan, nan));
}

OBL_TEST_CASE("guest_lookups.totty_partner_needs_every_condition")
{
    const TottySeeker me{10, kGenderMale, 0.5f};
    const TottyPartner good{11, 3, 0xd, kGenderFemale, 0.5f};
    OBL_CHECK(IsTottyPartner(me, good));
    TottyPartner other = good;
    other.state = kStateTottyAlt;
    OBL_CHECK(IsTottyPartner(me, other));
    other = good;
    other.id = 10;
    OBL_CHECK(!IsTottyPartner(me, other));
    other = good;
    other.type = 2;
    OBL_CHECK(!IsTottyPartner(me, other));
    other = good;
    other.gender = kGenderMale;
    OBL_CHECK(!IsTottyPartner(me, other));
    other = good;
    other.state = 0xc;
    OBL_CHECK(!IsTottyPartner(me, other));
    other = good;
    other.attractive = 0.75f;
    OBL_CHECK(!IsTottyPartner(me, other));
    OBL_CHECK(!IsTottyPartner(TottySeeker{10, kGenderFemale, 0.5f}, good)); // only a male guest looks
}

OBL_TEST_CASE("guest_lookups.hold_hands_direction_clamps_to_the_partner_animation")
{
    // A direction below the count (signed) stays, otherwise count - 1 (-1 for an animation with no directions).
    OBL_CHECK_EQ(PartnerDirection(3, 8), 3);
    OBL_CHECK_EQ(PartnerDirection(7, 8), 7);
    OBL_CHECK_EQ(PartnerDirection(7, 1), 0);
    OBL_CHECK_EQ(PartnerDirection(0, 1), 0);
    OBL_CHECK_EQ(PartnerDirection(5, 5), 4);
    OBL_CHECK_EQ(PartnerDirection(0, 0), -1);
    OBL_CHECK_EQ(PartnerDirection(-1, 8), -1);
}

OBL_TEST_CASE("guest_lookups.hold_hands_offsets_follow_the_jump_table")
{
    // The lookup table selects one of eight branches.
    struct Row {
        int32_t dx, dy;
        bool leftTest;
    };
    const Row rows[] = {{18, 0, true}, {12, 10, true}, {0, 18, false}, {12, -12, false}, {18, 0, false}, {12, 10, false}, {0, 18, true},
            {12, -12, true}};
    for (uint32_t direction = 0; direction < 8; ++direction) {
        const HoldHandsRule rule = HoldHandsFor(direction);
        OBL_CHECK_EQ(rule.dx, rows[direction].dx);
        OBL_CHECK_EQ(rule.dy, rows[direction].dy);
        OBL_CHECK_EQ(rule.leftTest, rows[direction].leftTest);
    }
    const HoldHandsRule beyond = HoldHandsFor(8);
    OBL_CHECK_EQ(beyond.dx, 0);
    OBL_CHECK_EQ(beyond.dy, 0);
    OBL_CHECK(!beyond.leftTest);
}

OBL_TEST_CASE("guest_lookups.hold_hands_swap_exchanges_the_two_animations")
{
    HandSwap swap{};
    const HoldHandsRule leftRule = HoldHandsFor(0), rightRule = HoldHandsFor(2);
    OBL_CHECK(SwapHands(leftRule, "D_WALK_HOLDHAND_LEFT", swap));
    OBL_CHECK_EQ(std::string(swap.guestPlays), "D_WALK_HOLDHAND_RIGHT");
    OBL_CHECK_EQ(std::string(swap.partnerPlays), "D_WALK_HOLDHAND_LEFT");
    OBL_CHECK(SwapHands(rightRule, "D_WALK_HOLDHAND_RIGHT", swap));
    OBL_CHECK_EQ(std::string(swap.guestPlays), "D_WALK_HOLDHAND_LEFT");
    OBL_CHECK_EQ(std::string(swap.partnerPlays), "D_WALK_HOLDHAND_RIGHT");
    OBL_CHECK(!SwapHands(leftRule, "D_WALK_HOLDHAND_RIGHT", swap)); // each direction tests for one name only
    OBL_CHECK(!SwapHands(rightRule, "D_WALK_HOLDHAND_LEFT", swap));
    OBL_CHECK(!SwapHands(leftRule, "D_WALK_HOLDHAND_LEFT2", swap));
    OBL_CHECK(!SwapHands(leftRule, "d_walk_holdhand_left", swap));
    OBL_CHECK(!SwapHands(leftRule, "", swap));
}

OBL_TEST_CASE("guest_lookups.hold_hands_position_is_one_float_rounding")
{
    OBL_CHECK_EQ(PartnerCoordinate(18, 100.5f), 118.5f);
    OBL_CHECK_EQ(PartnerCoordinate(-12, 5.0f), -7.0f);
    const float big = 16777216.0f; // 2^24: adding 1 rounds back to even
    OBL_CHECK_EQ(PartnerCoordinate(1, big), big);
    OBL_CHECK_EQ(PartnerCoordinate(0, 3.25f), 3.25f);
}
