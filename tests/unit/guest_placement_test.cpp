#include "unit_test.h"

#include "game/map/grid_state.h"
#include "game/objects/guest_placement.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace obl;
using namespace obl::guest_placement;

namespace {

struct Script {
    std::vector<int> draws;
    int used = 0;
    int operator()() { return used < static_cast<int>(draws.size()) ? draws[used++] : (++used, 0); }
};

struct TestGrid {
    obl::map::Grid grid{};
    std::vector<map::GridCell>& cells = grid.m_cells;
    explicit TestGrid(uint32_t size)
    {
        grid.m_tileSize = 64;
        grid.m_width = grid.m_height = size;
        cells.resize(size * size);
    }
};

} // namespace

OBL_TEST_CASE("guest_placement.snog_steps_by_direction")
{
    const int32_t expected[8][2] = {{0, -14}, {11, -12}, {15, 0}, {11, 10}, {0, 16}, {-10, 10}, {-15, 0}, {-10, -10}};
    for (uint32_t direction = 0; direction < 8; ++direction) {
        SnogStep step{99, 99};
        OBL_CHECK(SnogStepFor(direction, step));
        OBL_CHECK_EQ(step.x, expected[direction][0]);
        OBL_CHECK_EQ(step.y, expected[direction][1]);
    }
    SnogStep untouched{99, 99};
    OBL_CHECK(!SnogStepFor(8, untouched));
    OBL_CHECK(!SnogStepFor(0xffffffffu, untouched));
    OBL_CHECK_EQ(untouched.x, 99);
    OBL_CHECK_EQ(SnogCoordinate(-14, 100.5f), 86.5f);
    OBL_CHECK_EQ(SnogCoordinate(11, 0.1f), static_cast<float>(11.0 + static_cast<double>(0.1f)));
}

OBL_TEST_CASE("guest_placement.surface_candidate_draws_x_then_y_and_keeps_the_y_offset_unrounded")
{
    // rand 0 gives -radius on both axes, rand 32767 almost +radius.
    Script script{{0, 32767}};
    const SurfaceCandidate low = SurfaceCandidateAround(1000.0f, 2000.0f, 50.0f, script);
    OBL_CHECK_EQ(script.used, 2);
    OBL_CHECK_EQ(low.x, 950.0f);
    const double unit = static_cast<double>(script::kRandUnit);
    const double offsetY = (32767.0 * unit) * 100.0 - 50.0;
    OBL_CHECK_EQ(low.y, static_cast<float>(2000.0 + offsetY));
    // The x offset is rounded to a float before the position is added, the y one is not: a position where the two differ.
    Script mid{{12345, 12345}};
    const SurfaceCandidate point = SurfaceCandidateAround(1000.0f, 1000.0f, 33.3f, mid);
    const double r = static_cast<double>(33.3f);
    const double off = (12345.0 * unit) * (r - -r) - r;
    OBL_CHECK_EQ(point.x, static_cast<float>(1000.0 + static_cast<double>(static_cast<float>(off))));
    OBL_CHECK_EQ(point.y, static_cast<float>(1000.0 + off));
}

OBL_TEST_CASE("guest_placement.surface_candidate_is_clamped_to_the_map")
{
    Script script{{0, 0}};
    SurfaceCandidate low = SurfaceCandidateAround(20.0f, 30.0f, 500.0f, script);
    OBL_CHECK_EQ(low.x, 10.0f);
    OBL_CHECK_EQ(low.y, 10.0f);
    Script high{{32767, 32767}};
    SurfaceCandidate top = SurfaceCandidateAround(3990.0f, 7990.0f, 500.0f, high);
    OBL_CHECK_EQ(top.x, 4000.0f);
    OBL_CHECK_EQ(top.y, 8000.0f);
    // A NaN position counts as below the minimum.
    Script nan{{100, 100}};
    SurfaceCandidate bad = SurfaceCandidateAround(std::numeric_limits<float>::quiet_NaN(), 500.0f, 0.0f, nan);
    OBL_CHECK_EQ(bad.x, 10.0f);
    OBL_CHECK_EQ(bad.y, 500.0f);
}

OBL_TEST_CASE("guest_placement.entry_point_needs_no_building_on_the_tile_and_four_neighbours")
{
    TestGrid map(8);
    OBL_CHECK(EntryPointClear(map.grid, 4, 4));
    map.grid.Cell(4, 4).m_flags = 0x10;
    OBL_CHECK(!EntryPointClear(map.grid, 4, 4));
    map.grid.Cell(4, 4).m_flags = 0;
    // Each of the diagonal neighbours 0, 2, 4, 6 of the tile counts; the other four (1, 3, 5, 7) do not.
    int blocked = 0;
    for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t x = 0; x < 8; ++x) {
            if (x == 4 && y == 4) continue;
            map.grid.Cell(x, y).m_flags = 0x10;
            blocked += !EntryPointClear(map.grid, 4, 4);
            map.grid.Cell(x, y).m_flags = 0;
        }
    OBL_CHECK_EQ(blocked, 4);
    // Only the 0x10 bit of the flags matters.
    map.grid.Cell(4, 4).m_flags = 0xffffffefu;
    OBL_CHECK(EntryPointClear(map.grid, 4, 4));
    // The tile wraps like the grid does.
    OBL_CHECK(EntryPointClear(map.grid, 4 + 8, 4 + 16));
}

OBL_TEST_CASE("guest_placement.a_room_is_free_unless_the_hotel_is_exactly_full")
{
    OBL_CHECK(HasRoom(10, 3));
    OBL_CHECK(!HasRoom(10, 10));
    OBL_CHECK(HasRoom(10, 11));
    OBL_CHECK(HasRoom(0, 1));
}
