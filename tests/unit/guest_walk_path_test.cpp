#include "unit_test.h"

#include "game/map/grid_state.h"
#include "game/objects/guest_walk_path.h"

#include <cstring>
#include <vector>

using namespace obl;
using namespace obl::guest_path;

namespace {

// A tiny torus of tiles; `Mark` gives a tile the flags of the test.
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
    void Mark(uint32_t x, uint32_t y, uint32_t flags = kLandBit) { grid.Cell(x, y).m_flags = flags; }
};

} // namespace

OBL_TEST_CASE("guest_path.walkable_tile_is_marked_without_pavement_or_land_without_pavement_and_building")
{
    OBL_CHECK(!WalkableTile(0));
    OBL_CHECK(WalkableTile(kLandBit));
    OBL_CHECK(!WalkableTile(kLandBit | 0x10)); // a building on it
    OBL_CHECK(!WalkableTile(kLandBit | 0x8000)); // pavement
    OBL_CHECK(!WalkableTile(kLandBit | 0x8000 | 0x10));
    OBL_CHECK(WalkableTile(0x80)); // marked, no pavement
    OBL_CHECK(WalkableTile(0x10000000));
    OBL_CHECK(!WalkableTile(0x80 | 0x8000));
    // Marked wins over the building bit that stops a land tile: 0x80 | 0x10 is marked and has no pavement.
    OBL_CHECK(WalkableTile(0x80 | 0x10));
    OBL_CHECK(!WalkableTile(kLandBit | 0x80 | 0x8000)); // the pavement bit stops both rules
}

OBL_TEST_CASE("guest_path.action_state_is_one_only_for_the_desperate_walk_of_the_desperate_state")
{
    char field[40] = {};
    std::strcpy(field, kDesperateWalk);
    OBL_CHECK_EQ(ActionStateOf(kStateDesperate, field, sizeof(field)), 1);
    OBL_CHECK_EQ(ActionStateOf(0x10, field, sizeof(field)), 0);
    std::strcpy(field, "D_WALK_DESPERATE2");
    OBL_CHECK_EQ(ActionStateOf(kStateDesperate, field, sizeof(field)), 0);
    std::strcpy(field, "D_WALK_DESPERAT");
    OBL_CHECK_EQ(ActionStateOf(kStateDesperate, field, sizeof(field)), 0);
    std::strcpy(field, "walk");
    OBL_CHECK_EQ(ActionStateOf(kStateDesperate, field, sizeof(field)), 0);
}

OBL_TEST_CASE("guest_path.no_walkable_neighbour_draws_nothing")
{
    Map map;
    int draws = 0;
    int visits = 0;
    OBL_CHECK_EQ(WalkableNeighbours(map.grid, 3, 3), 0u);
    OBL_CHECK(!WalkTiles(map.grid, 3, 3, [&] { return ++draws; }, [&](grid_math::TileXY) { ++visits; }));
    OBL_CHECK_EQ(draws, 0);
    OBL_CHECK_EQ(visits, 0);
}

OBL_TEST_CASE("guest_path.the_draw_picks_the_kth_walkable_neighbour")
{
    Map map;
    const uint32_t x = 3, y = 3;
    grid_math::TileXY neighbours[8];
    for (int d = 0; d < 8; ++d) neighbours[d] = grid_math::Neighbour(Map::kSize, Map::kSize, x, y, d);
    map.Mark(neighbours[2].x, neighbours[2].y);
    map.Mark(neighbours[5].x, neighbours[5].y, 0x80);
    map.Mark(neighbours[7].x, neighbours[7].y);
    OBL_CHECK_EQ(WalkableNeighbours(map.grid, x, y), 3u);
    const int expected[3] = {2, 5, 7};
    for (int draw = 0; draw < 7; ++draw) {
        int draws = 0;
        std::vector<grid_math::TileXY> visited;
        OBL_REQUIRE(WalkTiles(
                map.grid, x, y,
                [&] {
                    ++draws;
                    return draw;
                },
                [&](grid_math::TileXY tile) { visited.push_back(tile); }));
        OBL_CHECK_EQ(draws, 1);
        const int direction = expected[draw % 3];
        // Only the neighbour itself is walkable (the tile behind it in the same direction is not), so it is visited once.
        OBL_REQUIRE(visited.size() == 1u);
        OBL_CHECK_EQ(visited.front().x, neighbours[direction].x);
        OBL_CHECK_EQ(visited.front().y, neighbours[direction].y);
    }
}

OBL_TEST_CASE("guest_path.the_middle_of_the_second_to_last_tile_of_a_row_is_the_goal")
{
    Map map;
    for (uint32_t length = 1; length <= 4; ++length) {
        for (uint32_t row = 4; row < 4 + length; ++row) map.Mark(3, row);
        std::vector<grid_math::TileXY> visited;
        OBL_REQUIRE(WalkTiles(map.grid, 3, 3, [] { return 0; }, [&](grid_math::TileXY tile) { visited.push_back(tile); }));
        OBL_REQUIRE(visited.size() == length);
        OBL_CHECK_EQ(visited.back().x, 3u);
        OBL_CHECK_EQ(visited.back().y, length == 1 ? 4u : 4u + length - 2);
        OBL_CHECK_EQ(visited.front().y, 4u);
        for (uint32_t row = 4; row < 4 + length; ++row) map.Mark(3, row, 0);
    }
}

OBL_TEST_CASE("guest_path.the_draw_is_taken_as_unsigned")
{
    Map map;
    const uint32_t x = 3, y = 3;
    grid_math::TileXY neighbours[8];
    for (int d = 0; d < 8; ++d) neighbours[d] = grid_math::Neighbour(Map::kSize, Map::kSize, x, y, d);
    map.Mark(neighbours[1].x, neighbours[1].y);
    map.Mark(neighbours[6].x, neighbours[6].y);
    // 0x80000001 as unsigned is 2147483649, odd: 2147483649 % 2 + 1 = 2, the second walkable tile; a signed remainder would be -1 (+ 1 = 0 draws a walk of none).
    std::vector<grid_math::TileXY> visited;
    OBL_REQUIRE(WalkTiles(
            map.grid, x, y, [] { return static_cast<int>(0x80000001u); }, [&](grid_math::TileXY tile) { visited.push_back(tile); }));
    OBL_REQUIRE(!visited.empty());
    OBL_CHECK_EQ(visited.front().x, neighbours[6].x);
    OBL_CHECK_EQ(visited.front().y, neighbours[6].y);
}
