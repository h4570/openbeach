#include "unit_test.h"

#include "game/objects/building_queries.h"

#include <limits>

using namespace obl;

OBL_TEST_CASE("building_queries.has_state_is_unsigned_up_to_four")
{
    for (uint32_t state = 0; state <= 4; ++state) OBL_CHECK(BuildingStateIsKnown(state));
    OBL_CHECK(!BuildingStateIsKnown(5));
    OBL_CHECK(!BuildingStateIsKnown(0xffffffffu)); // -1 is above 4 when compared unsigned
}

OBL_TEST_CASE("building_queries.queue_direction_per_rotation")
{
    QueueDirection d{1.0f, 2.0f};
    OBL_CHECK(QueueDirectionFromRotation(0, d));
    OBL_CHECK_EQ(d.x, 16.0f);
    OBL_CHECK_EQ(d.y, 16.0f);
    OBL_CHECK(QueueDirectionFromRotation(1, d));
    OBL_CHECK_EQ(d.x, 16.0f);
    OBL_CHECK_EQ(d.y, -16.0f);
    OBL_CHECK(QueueDirectionFromRotation(2, d));
    OBL_CHECK_EQ(d.x, -16.0f);
    OBL_CHECK_EQ(d.y, -16.0f);
    OBL_CHECK(QueueDirectionFromRotation(3, d));
    OBL_CHECK_EQ(d.x, -16.0f);
    OBL_CHECK_EQ(d.y, 16.0f);
}

OBL_TEST_CASE("building_queries.queue_direction_ignores_other_rotations")
{
    QueueDirection d{1.0f, 2.0f};
    OBL_CHECK(!QueueDirectionFromRotation(4, d));
    OBL_CHECK(!QueueDirectionFromRotation(0xffffffffu, d)); // negative rotation is above 3 unsigned
    OBL_CHECK_EQ(d.x, 1.0f);
    OBL_CHECK_EQ(d.y, 2.0f);
}

OBL_TEST_CASE("building_queries.average_intensity_reads_one_column")
{
    const float table[5][2] = {{1.0f, 10.0f}, {2.0f, 20.0f}, {6.0f, 30.0f}, {100.0f, 100.0f}, {100.0f, 100.0f}};
    OBL_CHECK_EQ(AverageIntensity(&table[0][0], 3), 3.0); // (1 + 2 + 6) / 3
    OBL_CHECK_EQ(AverageIntensity(&table[0][1], 3), 20.0); // the other column
    OBL_CHECK_EQ(AverageIntensity(&table[0][0], 1), 1.0);
    OBL_CHECK_EQ(AverageIntensity(&table[0][0], 0), 0.0); // no commodities: 0.0, not a division by zero
    OBL_CHECK_EQ(AverageIntensity(&table[0][0], -2), 0.0); // the loop is skipped
}

OBL_TEST_CASE("building_queries.overlay_offset_halves_towards_zero")
{
    OBL_CHECK_EQ(OverlayYOffset(10), 5);
    OBL_CHECK_EQ(OverlayYOffset(11), 5);
    OBL_CHECK_EQ(OverlayYOffset(-10), -5);
    OBL_CHECK_EQ(OverlayYOffset(-11), -5);
    OBL_CHECK_EQ(OverlayYOffset(0), 0);
    OBL_CHECK_EQ(OverlayYOffset(-32768), -16384);
}

OBL_TEST_CASE("building_queries.progress_non_zero_is_ordered")
{
    OBL_CHECK(!ProgressIsNonZero(0.0f));
    OBL_CHECK(!ProgressIsNonZero(-0.0f));
    OBL_CHECK(ProgressIsNonZero(0.25f));
    OBL_CHECK(ProgressIsNonZero(-1.0f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(!ProgressIsNonZero(nan)); // a NaN counts as equal
}

OBL_TEST_CASE("building_queries.beer_slot_is_the_first_commodity_named_beer")
{
    const char* names[] = {"Ice cream", "BEER", "beer"};
    OBL_CHECK_EQ(FindBeerSlot(3, [&](uint32_t i) { return names[i]; }), 1); // _stricmp: case ignored, the first match wins
    OBL_CHECK_EQ(FindBeerSlot(1, [&](uint32_t i) { return names[i]; }), kNoBeerSlot);
    OBL_CHECK_EQ(FindBeerSlot(0, [&](uint32_t i) { return names[i]; }), kNoBeerSlot);
    const char* near[] = {"Beers", "Bee"};
    OBL_CHECK_EQ(FindBeerSlot(2, [&](uint32_t i) { return near[i]; }), kNoBeerSlot);
}

OBL_TEST_CASE("building_queries.beer_slot_cost_is_base_plus_strength_times_cost")
{
    OBL_CHECK_EQ(BeerSlotCost(1.5f, 2.0f, 0.25f), 2.0f);
    OBL_CHECK_EQ(BeerSlotCost(1.0f, 0.5f, 0.0f), 1.0f);
    OBL_CHECK_EQ(BeerSlotCost(0.0f, 3.0f, 1.0f), 3.0f);
}
