#include "unit_test.h"
#include "game/map/grid_math.h"

// Footprint helpers with multi-tile footprints: the single-tile cases in grid_math_test.cpp never enter the
// stepping loops of rotations 1-3.
using namespace obl::grid_math;

OBL_TEST_CASE("grid_footprint_walk.traversal_rotation1_steps_columns_minus_one")
{
    const GridDims grid{64, 128, 128};
    bool touched = false;
    TileXY origin{4, 7};
    const TraversalDirections d = OrientFootprintTraversal(grid, grid, origin, 2, 4, 1, touched);
    TileXY expected{4, 8};
    for (int i = 0; i < 3; ++i) expected = Neighbour(128, 128, expected.x, expected.y, 6);
    OBL_CHECK(touched);
    OBL_CHECK_EQ(origin.x, expected.x);
    OBL_CHECK_EQ(origin.y, expected.y);
    OBL_CHECK_EQ(d.columnDirection, 2);
    OBL_CHECK_EQ(d.rowDirection, 0);
}

OBL_TEST_CASE("grid_footprint_walk.traversal_single_column_does_not_step")
{
    const GridDims grid{64, 128, 128};
    bool touched = false;
    TileXY origin{4, 7};
    OrientFootprintTraversal(grid, grid, origin, 5, 1, 1, touched);
    OBL_CHECK_EQ(origin.x, 4u);
    OBL_CHECK_EQ(origin.y, 8u);
    origin = {4, 7};
    OrientFootprintTraversal(grid, grid, origin, 0, 0, 3, touched); // non-positive counts: no steps
    OBL_CHECK_EQ(origin.x, 4u);
    OBL_CHECK_EQ(origin.y, 8u);
}

OBL_TEST_CASE("grid_footprint_walk.offset_point_rotation1_walks_width")
{
    const GridDims grid{64, 128, 128};
    uint32_t x = 3, y = 2;
    StepEast(grid, x, y);
    StepEast(grid, x, y);
    const WorldPoint o = TileToWorldPoint(64, x, y, 0);
    PathPoint p{};
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 3, 1, 1.0f, 2.0f, 1, p));
    OBL_CHECK_EQ(p.x, o.x + 32.0f + 2.0f);
    OBL_CHECK_EQ(p.y, o.y + 32.0f - 1.0f);
}

OBL_TEST_CASE("grid_footprint_walk.offset_point_rotation2_walks_height_then_width")
{
    const GridDims grid{64, 128, 128};
    uint32_t x = 3, y = 2;
    StepEast(grid, x, y);
    for (int i = 0; i < 2; ++i) {
        const TileXY n = Neighbour(128, 128, x, y, 6);
        x = n.x;
        y = n.y;
    }
    const WorldPoint o = TileToWorldPoint(64, x, y, 0);
    PathPoint p{};
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 3, 2, 1.0f, 2.0f, 2, p));
    OBL_CHECK_EQ(p.x, o.x - 1.0f);
    OBL_CHECK_EQ(p.y, o.y - 2.0f);
}

OBL_TEST_CASE("grid_footprint_walk.offset_point_rotation3_walks_height")
{
    const GridDims grid{64, 128, 128};
    uint32_t x = 3, y = 2;
    for (int i = 0; i < 2; ++i) {
        const TileXY n = Neighbour(128, 128, x, y, 6);
        x = n.x;
        y = n.y;
    }
    const WorldPoint o = TileToWorldPoint(64, x, y, 2);
    PathPoint p{};
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 1, 3, 1.0f, 2.0f, 3, p));
    OBL_CHECK_EQ(p.x, o.x - 2.0f);
    OBL_CHECK_EQ(p.y, o.y + 1.0f);
}
