#include "unit_test.h"

#include "game/map/grid_state.h"
#include "game/objects/guest_pool.h"

#include <bit>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::guest_pool;

namespace {

// A tiny torus of tiles for the pool walk: `flagged` tiles carry the pool bit.
struct Map {
    static constexpr uint32_t kSize = 8;
    obl::map::Grid grid{};
    std::vector<map::GridCell>& cells = grid.m_cells;
    Map()
    {
        grid.m_tileSize = 64;
        grid.m_width = grid.m_height = kSize;
        cells.resize(kSize * kSize);
    }
    void Flag(uint32_t x, uint32_t y) { grid.Cell(x, y).m_flags |= kTilePool; }
};

} // namespace

OBL_TEST_CASE("guest_pool.facing_is_the_signed_remainder_of_the_base_minus_twice_the_direction")
{
    OBL_CHECK_EQ(Facing(kPoolFacingBase, 0), 1);
    OBL_CHECK_EQ(Facing(kPoolFacingBase, 1), 7);
    OBL_CHECK_EQ(Facing(kPoolFacingBase, 2), 5);
    OBL_CHECK_EQ(Facing(kPoolFacingBase, 3), 3);
    OBL_CHECK_EQ(Facing(kPoolFacingBase, 5), -1);
    OBL_CHECK_EQ(Facing(kPoolFacingBase, 9), -1);
    OBL_CHECK_EQ(Facing(kPoolFacingBase, 6), -3);
    OBL_CHECK_EQ(Facing(kPoolFacingBase, -2), 5);
    // The slide: mode + (4 - direction) * 2.
    for (int d = 0; d < 4; ++d) {
        OBL_CHECK_EQ(SlideFacing(5, d), (5 + (4 - d) * 2) % 8);
        OBL_CHECK_EQ(SlideFacing(6, d), (6 + (4 - d) * 2) % 8);
    }
    OBL_CHECK_EQ(SlideFacing(5, 7), (5 + (4 - 7) * 2) % 8);
}

OBL_TEST_CASE("guest_pool.clamp_facing_compares_as_signed_and_uses_the_last_direction")
{
    OBL_CHECK_EQ(ClampFacing(3, 8), 3);
    OBL_CHECK_EQ(ClampFacing(7, 8), 7);
    OBL_CHECK_EQ(ClampFacing(3, 3), 2);
    OBL_CHECK_EQ(ClampFacing(4, 1), 0);
    OBL_CHECK_EQ(ClampFacing(0, 0), -1); // an animation without directions: -1
    OBL_CHECK_EQ(ClampFacing(-3, 4), -3); // a negative facing stays (signed compare)
}

OBL_TEST_CASE("guest_pool.posted_goal_truncates_x_unrounded_and_y_as_float")
{
    // Building at 100.7, 200.9, direction 0: x = 100.7 - 33 = 67.7 -> 67, y = 200.9 - 440 = -239.1 -> -239.
    guest_walk::Goal goal = PostedGoal(100.7f, 200.9f, 0, kEntryOffsets);
    OBL_CHECK_EQ(goal.x, 67);
    OBL_CHECK_EQ(goal.y, -239);
    goal = PostedGoal(100.7f, 200.9f, 3, kExitOffsets);
    OBL_CHECK_EQ(goal.x, 247);
    OBL_CHECK_EQ(goal.y, -131);
    // A direction past 3 keeps the building's own position.
    goal = PostedGoal(100.7f, 200.9f, 4, kExitOffsets);
    OBL_CHECK_EQ(goal.x, 100);
    OBL_CHECK_EQ(goal.y, 200);
    goal = PostedGoal(100.7f, 200.9f, -1, kExitOffsets);
    OBL_CHECK_EQ(goal.x, 100);
    // y goes through a float store: 16777216 + 0.5 - 1 style sums round before they are truncated, x does not.
    goal = PostedGoal(1.0f, 16777217.0f, 0, kEntryOffsets); // y: 16777216 (the float) - 440 = 16776776
    OBL_CHECK_EQ(goal.y, 16776776);
    // x keeps the double sum: 8388608.5 is exact in float; 8388608.5 - 33 = 8388575.5 -> 8388575.
    goal = PostedGoal(8388608.5f, 0.0f, 0, kEntryOffsets);
    OBL_CHECK_EQ(goal.x, 8388575);
}

OBL_TEST_CASE("guest_pool.teleport_point_stores_both_sums_as_float")
{
    const Point p = TeleportPoint(16777216.0f, 16777216.0f, {1.5f, 0.5f});
    OBL_CHECK_EQ(p.x, 16777218.0f); // 16777217.5 rounds to even in float
    OBL_CHECK_EQ(p.y, 16777216.0f);
}

OBL_TEST_CASE("guest_pool.known_direction_is_unsigned_below_four")
{
    OBL_CHECK(KnownDirection(0));
    OBL_CHECK(KnownDirection(3));
    OBL_CHECK(!KnownDirection(4));
    OBL_CHECK(!KnownDirection(-1));
}

OBL_TEST_CASE("guest_pool.next_tile_without_pool_neighbours_draws_nothing")
{
    Map map;
    int draws = 0;
    grid_math::PathPoint goal{-1.0f, -1.0f};
    OBL_CHECK(!NextPoolTile(map.grid, 3, 3, [&] { return ++draws; }, goal));
    OBL_CHECK_EQ(draws, 0);
    OBL_CHECK_EQ(goal.x, -1.0f);
}

OBL_TEST_CASE("guest_pool.next_tile_picks_the_kth_flagged_neighbour_and_walks_on_while_pool")
{
    Map map;
    const uint32_t x = 3, y = 3;
    grid_math::TileXY neighbours[8];
    for (int d = 0; d < 8; ++d) neighbours[d] = grid_math::Neighbour(Map::kSize, Map::kSize, x, y, d);
    map.Flag(neighbours[1].x, neighbours[1].y);
    map.Flag(neighbours[4].x, neighbours[4].y);
    map.Flag(neighbours[6].x, neighbours[6].y);
    for (int draw = 0; draw < 6; ++draw) {
        int draws = 0;
        grid_math::PathPoint goal{};
        OBL_REQUIRE(NextPoolTile(
                map.grid, x, y,
                [&] {
                    ++draws;
                    return draw;
                },
                goal));
        OBL_CHECK_EQ(draws, 1);
        const int expected[3] = {1, 4, 6};
        grid_math::TileXY tile = neighbours[expected[draw % 3]];
        // The walk from there continues in the chosen direction while the next tile is a pool tile too: only the neighbour itself is flagged here, unless
        // the step lands on another flagged neighbour.
        grid_math::PathPoint last = grid_math::TileToWorldCenter(64, tile.x, tile.y);
        for (;;) {
            tile = grid_math::Neighbour(Map::kSize, Map::kSize, tile.x, tile.y, expected[draw % 3]);
            if (!(map.grid.Cell(tile.x, tile.y).m_flags & kTilePool)) break;
            last = grid_math::TileToWorldCenter(64, tile.x, tile.y);
        }
        OBL_CHECK_EQ(goal.x, last.x);
        OBL_CHECK_EQ(goal.y, last.y);
    }
}

OBL_TEST_CASE("guest_pool.next_tile_goes_to_the_last_tile_of_a_pool_row")
{
    Map map;
    // Direction 3 always steps y + 1: flag the three tiles below (3, 3).
    map.Flag(3, 4);
    map.Flag(3, 5);
    map.Flag(3, 6);
    grid_math::PathPoint goal{};
    OBL_REQUIRE(NextPoolTile(map.grid, 3, 3, [] { return 0; }, goal));
    const grid_math::PathPoint expected = grid_math::TileToWorldCenter(64, 3, 6);
    OBL_CHECK_EQ(goal.x, expected.x);
    OBL_CHECK_EQ(goal.y, expected.y);
}

OBL_TEST_CASE("guest_pool.slide_sprite_follows_the_name_without_case")
{
    OBL_CHECK_EQ(SlideSpriteIndex("Gail"), 0);
    OBL_CHECK_EQ(SlideSpriteIndex("gAIL"), 0);
    OBL_CHECK_EQ(SlideSpriteIndex("Shaz"), 1);
    OBL_CHECK_EQ(SlideSpriteIndex("FAT_BLOKE"), 2);
    OBL_CHECK_EQ(SlideSpriteIndex("Fat_Bloke2"), 3);
    OBL_CHECK_EQ(SlideSpriteIndex("Chip"), 3);
    OBL_CHECK_EQ(SlideSpriteIndex(""), 3);
    OBL_CHECK_EQ(std::string(kSlideSprites[SlideSpriteIndex("Shaz")]), "Shaz_funpool");
}

OBL_TEST_CASE("guest_pool.slide_palette_and_delay")
{
    OBL_CHECK_EQ(SlidePalette(3, 8), 3);
    OBL_CHECK_EQ(SlidePalette(8, 8), 7);
    OBL_CHECK_EQ(SlidePalette(-2, 8), -2);
    OBL_CHECK_EQ(SlidePalette(0, 0), -1);
    OBL_CHECK_EQ(SlideDelay(0), 2.0f);
    OBL_CHECK_EQ(SlideDelay(1), 2.0f);
    OBL_CHECK_EQ(SlideDelay(2), 4.0f);
    OBL_CHECK_EQ(SlideDelay(3), 4.0f);
}

OBL_TEST_CASE("guest_pool.swim_sprite_follows_the_funpool_name_without_case")
{
    OBL_CHECK_EQ(SwimSpriteIndex("Gail_funpool"), 0);
    OBL_CHECK_EQ(SwimSpriteIndex("GAIL_FUNPOOL"), 0);
    OBL_CHECK_EQ(SwimSpriteIndex("Shaz_funpool"), 1);
    OBL_CHECK_EQ(SwimSpriteIndex("bob_FunPool"), 2);
    OBL_CHECK_EQ(SwimSpriteIndex("Chip_funpool"), 3);
    OBL_CHECK_EQ(SwimSpriteIndex("Gail"), 3);
    OBL_CHECK_EQ(SwimSpriteIndex(""), 3);
    OBL_CHECK_EQ(std::string(kSwimSprites[SwimSpriteIndex("Bob_funpool")]), "Fat_bloke");
    OBL_CHECK_EQ(std::string(kSwimSprites[SwimSpriteIndex("Other")]), "Fit_Bloke");
}

OBL_TEST_CASE("guest_pool.swim_swaps_the_sprite_in_the_states_one_to_four_only")
{
    OBL_CHECK(!SwimSwapsSprite(0));
    for (int32_t state = 1; state <= 4; ++state) OBL_CHECK(SwimSwapsSprite(state));
    OBL_CHECK(!SwimSwapsSprite(5));
    OBL_CHECK(!SwimSwapsSprite(-1));
    // The turn of the landing state and the slide's facing rule: base - 2 * direction, remainder 8.
    OBL_CHECK_EQ(Facing(kSwimFacingBase, 0), 7);
    OBL_CHECK_EQ(Facing(kSwimFacingBase, 1), 5);
    OBL_CHECK_EQ(Facing(kSwimFacingBase, 2), 3);
    OBL_CHECK_EQ(Facing(kSwimFacingBase, 3), 1);
    OBL_CHECK_EQ(SlideFacing(kSwimStartModes[0], 1), (5 + 3 * 2) % 8);
    OBL_CHECK_EQ(SlideFacing(kSwimStartModes[3], 0), (6 + 4 * 2) % 8);
}

// The offsets are constants written as bit patterns, so the test holds the same bit patterns.
OBL_TEST_CASE("guest_pool.swim_offsets_match_the_expected_constants")
{
    const uint32_t start[kSwimLanes][kBuildingDirections][2] = {
            {{0xc27c0000, 0xc4050000}, {0xc2d40000, 0xc3b40000}, {0x433e0000, 0xc3de0000}, {0xc1a00000, 0xc41c8000}},
            {{0xc25c0000, 0xc4010000}, {0xc2b40000, 0xc3bf0000}, {0x432e0000, 0xc3e80000}, {0xc2180000, 0xc4148000}},
            {{0x41000000, 0xc3df0000}, {0xc1a80000, 0xc3d60000}, {0x42920000, 0xc3fc0000}, {0xc2c80000, 0xc40c0000}},
            {{0x42500000, 0xc3ce0000}, {0x41880000, 0xc3ed0000}, {0x427c0000, 0xc40e0000}, {0xc30d0000, 0xc3fb0000}},
    };
    const uint32_t landing[kBuildingDirections][2] = {
            {0xc3040000, 0xc3ee0000}, {0xc2920000, 0xc38b0000}, {0x437b0000, 0xc3e50000}, {0xc2500000, 0xc4238000}};
    const uint32_t footprint[kBuildingDirections][2] = {
            {0xc32b0000, 0xc4050000}, {0xc2ee0000, 0xc3810000}, {0x43960000, 0xc3d40000}, {0xc0000000, 0xc4360000}};
    for (int lane = 0; lane < kSwimLanes; ++lane)
        for (int d = 0; d < 4; ++d) {
            OBL_CHECK_EQ(kSwimStartOffsets[lane][d].x, std::bit_cast<float>(start[lane][d][0]));
            OBL_CHECK_EQ(kSwimStartOffsets[lane][d].y, std::bit_cast<float>(start[lane][d][1]));
        }
    for (int d = 0; d < 4; ++d) {
        OBL_CHECK_EQ(kSwimLandingOffsets[d].x, std::bit_cast<float>(landing[d][0]));
        OBL_CHECK_EQ(kSwimLandingOffsets[d].y, std::bit_cast<float>(landing[d][1]));
        OBL_CHECK_EQ(kSwimFootprintStart[d].x, std::bit_cast<float>(footprint[d][0]));
        OBL_CHECK_EQ(kSwimFootprintStart[d].y, std::bit_cast<float>(footprint[d][1]));
    }
    OBL_CHECK_EQ(kSwimFootprintOffsetX, std::bit_cast<float>(0xc35e0000u));
    OBL_CHECK_EQ(kSwimFootprintOffsetY, std::bit_cast<float>(0xc4100000u));
}
