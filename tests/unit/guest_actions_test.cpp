#include "unit_test.h"
#include "game/objects/guest_actions.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace obl;
using namespace obl::guest_actions;

namespace {

// step by step: the draw as an unsigned 64-bit integer times the float k, minus r, rounded to float; the position plus that, stored as float.
float ExpectedSwimCoordinate(float position, float radius, int32_t draw)
{
    double offset = static_cast<double>(draw);
    offset *= static_cast<double>(script::kRandUnit);
    double span = -static_cast<double>(radius);
    span = static_cast<double>(radius) - span;
    offset = offset * span;
    offset -= static_cast<double>(radius);
    const float roundedOffset = static_cast<float>(offset);
    return static_cast<float>(static_cast<double>(position) + static_cast<double>(roundedOffset));
}

} // namespace

OBL_TEST_CASE("guest_actions.swim_coordinate_matches_the_expected_value")
{
    const float positions[] = {10.0f, 333.3f, 1999.7f, 3999.9f, 7123.45f, -50.0f};
    const float radii[] = {0.0f, 20.0f, 100.0f, 333.33f, 3000.0f};
    const int32_t draws[] = {0, 1, 7, 16383, 16384, 32766, 32767};
    for (float position : positions)
        for (float radius : radii)
            for (int32_t draw : draws) OBL_CHECK_EQ(SwimCoordinate(position, radius, draw), ExpectedSwimCoordinate(position, radius, draw));
    // The extremes of the draw land on the ends of the range (k is about 1 / 32767).
    OBL_CHECK_EQ(SwimCoordinate(1000.0f, 100.0f, 0), 900.0f);
    OBL_CHECK(std::fabs(SwimCoordinate(1000.0f, 100.0f, 32767) - 1100.0f) < 0.01f);
}

OBL_TEST_CASE("guest_actions.swim_offset_is_rounded_to_float_before_the_add")
{
    int found = 0;
    for (int32_t draw = 1; draw < 32767 && found < 3; draw += 97)
        for (float position : {1234.567f, 2047.9f, 3001.3f}) {
            const double r = 77.7f;
            const double unrounded = static_cast<double>(position) + (draw * static_cast<double>(script::kRandUnit) * (r + r) - r);
            if (static_cast<float>(unrounded) == ExpectedSwimCoordinate(position, 77.7f, draw)) continue;
            OBL_CHECK_EQ(SwimCoordinate(position, 77.7f, draw), ExpectedSwimCoordinate(position, 77.7f, draw));
            ++found;
        }
    OBL_CHECK(found > 0);
}

OBL_TEST_CASE("guest_actions.swim_clamp")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(ClampSwim(9.999f, kSwimMaxX), 10.0f);
    OBL_CHECK_EQ(ClampSwim(10.0f, kSwimMaxX), 10.0f);
    OBL_CHECK_EQ(ClampSwim(-1e30f, kSwimMaxY), 10.0f);
    OBL_CHECK_EQ(ClampSwim(nan, kSwimMaxX), 10.0f); // unordered is "below"
    OBL_CHECK_EQ(ClampSwim(4000.0f, kSwimMaxX), 4000.0f);
    OBL_CHECK_EQ(ClampSwim(4000.5f, kSwimMaxX), 4000.0f);
    OBL_CHECK_EQ(ClampSwim(4000.5f, kSwimMaxY), 4000.5f);
    OBL_CHECK_EQ(ClampSwim(9000.0f, kSwimMaxY), 8000.0f);
    // A NaN radius poisons both coordinates, which then clamp to the corner.
    OBL_CHECK_EQ(ClampSwim(SwimCoordinate(500.0f, nan, 1234), kSwimMaxX), 10.0f);
}

OBL_TEST_CASE("guest_actions.swim_redraws_until_free_sea")
{
    // Draws are consumed x, y per attempt; the tile test sees the clamped point.
    std::vector<int32_t> draws = {0, 0, 32767, 0, 16384, 16384};
    size_t next = 0;
    std::vector<std::pair<float, float>> asked;
    const uint32_t answers[] = {0x0, kSeaTile | kBlockedTile, kSeaTile};
    const SwimTarget target = DecideSwimTarget(
            20.0f, 7990.0f, 100.0f, [&] { return draws[next++]; },
            [&](float x, float y) {
                asked.emplace_back(x, y);
                return answers[asked.size() - 1];
            });
    OBL_CHECK_EQ(next, draws.size());
    OBL_CHECK_EQ(asked.size(), static_cast<size_t>(3));
    OBL_CHECK_EQ(asked[0].first, 10.0f); // 20 - 100, clamped
    OBL_CHECK_EQ(asked[0].second, 7890.0f);
    OBL_CHECK_EQ(asked[1].second, 7890.0f);
    OBL_CHECK(std::fabs(asked[1].first - 120.0f) < 0.01f);
    OBL_CHECK_EQ(target.x, asked[2].first);
    OBL_CHECK_EQ(target.y, asked[2].second);
    OBL_CHECK_EQ(FloatToIntWrap(target.x), 20);
}

OBL_TEST_CASE("guest_actions.nearest_lifeguard")
{
    const float guest[3] = {100.0f, 100.0f, 0.0f};
    const float a[3] = {110.0f, 100.0f, 0.0f}, b[3] = {90.0f, 100.0f, 0.0f}, c[3] = {100.0f, 103.0f, 4.0f};
    OBL_CHECK_EQ(LifeguardDistance(c, guest), 5.0);

    NearestLifeguard none;
    OBL_CHECK_EQ(none.id, NearestLifeguard::kNone);

    NearestLifeguard tie; // equal distances: the first stays
    tie.Offer(7, LifeguardDistance(a, guest));
    tie.Offer(8, LifeguardDistance(b, guest));
    OBL_CHECK_EQ(tie.id, 7);
    tie.Offer(9, LifeguardDistance(c, guest));
    OBL_CHECK_EQ(tie.id, 9);
    OBL_CHECK_EQ(tie.best, 5.0f);

    NearestLifeguard far; // beyond the 1e8 start nobody is found
    const float distant[3] = {2e8f, 0.0f, 0.0f};
    far.Offer(3, LifeguardDistance(distant, guest));
    OBL_CHECK_EQ(far.id, NearestLifeguard::kNone);

    // A NaN position wins, and with NaN as the best every later guard wins, even a farther one.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float lost[3] = {nan, 0.0f, 0.0f};
    NearestLifeguard poisoned;
    poisoned.Offer(1, LifeguardDistance(c, guest));
    poisoned.Offer(2, LifeguardDistance(lost, guest));
    OBL_CHECK_EQ(poisoned.id, 2);
    poisoned.Offer(3, LifeguardDistance(a, guest));
    OBL_CHECK_EQ(poisoned.id, 3);
}

OBL_TEST_CASE("guest_actions.death_notice_by_state")
{
    const DeathNotice eaten = DeathNoticeFor(kStateAttackByShark);
    OBL_CHECK(eaten.eaten);
    OBL_CHECK_EQ(eaten.spotKind, 8);
    OBL_CHECK(std::strcmp(eaten.key, "GUEST_EATEN") == 0);
    for (int32_t state : {0, 0x13, 0x16, 0x35, 0x37, -1}) {
        const DeathNotice drowned = DeathNoticeFor(state);
        OBL_CHECK(!drowned.eaten);
        OBL_CHECK_EQ(drowned.spotKind, 10);
        OBL_CHECK(std::strcmp(drowned.key, "GUEST_DROWNED") == 0);
    }
    OBL_CHECK_EQ(kDeathRadius, 5);
    OBL_CHECK_EQ(kDeathTickerType, 5u);
}
