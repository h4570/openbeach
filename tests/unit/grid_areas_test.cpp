// Area lists, open state, boat route, the area of a world point and the save records of the terrain grid (src/game/map/grid_areas.h).

#include "unit_test.h"

#include "game/map/grid_areas.h"

#include <cstdint>
#include <vector>

using namespace obl;
using namespace obl::grid_areas;
using obl::map::GridCell;

namespace {

struct Fixture {
    Grid grid{};
    std::vector<map::GridCell>& cells = grid.m_cells;
    map::Area square;
    map::Area far;
    Fixture()
    {
        grid.m_tileSize = 64;
        grid.m_width = 32;
        grid.m_height = 32;
        cells.resize(1024);
        square.m_points = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
        square.m_maxX = 4;
        square.m_maxY = 4;
        far.m_points = {{20, 20}, {22, 20}};
        far.m_minX = 20;
        far.m_maxX = 22;
        far.m_minY = 20;
        far.m_maxY = 20;
    }
};

} // namespace

OBL_TEST_CASE("grid_areas.area_at_checks_the_range")
{
    Fixture f;
    f.grid.m_beachAreas = {f.square, f.far, map::Area{}};
    OBL_CHECK(Grid::AreaAt(f.grid.m_beachAreas, 2) == &f.grid.m_beachAreas[2]);
    OBL_CHECK(Grid::AreaAt(f.grid.m_beachAreas, 3) == nullptr);
    OBL_CHECK(Grid::AreaAt(f.grid.m_beachAreas, -1) == nullptr);
    OBL_CHECK(Grid::AreaAt(f.grid.m_seaAreas, 0) == nullptr);
}

OBL_TEST_CASE("grid_areas.open_state_by_kind")
{
    Fixture f;
    f.grid.m_beachAreas = {f.square, f.far};
    f.grid.m_seaAreas = {map::Area{}, map::Area{}};
    auto& beach = f.grid.m_beachAreas;
    auto& sea = f.grid.m_seaAreas;
    for (auto* list : {&beach, &sea})
        for (map::Area& area : *list) area.m_shut = true;
    bool valid = false;
    OBL_CHECK(!IsAreaOpen(f.grid, kKindBeach, 1, valid));
    OBL_CHECK(valid);
    OBL_CHECK(SetAreaOpen(f.grid, kKindBeach, 1));
    OBL_CHECK(!beach[1].m_shut);
    OBL_CHECK(beach[0].m_shut);
    OBL_CHECK(IsAreaOpen(f.grid, kKindBeach, 1, valid));
    // Kind 2 needs both areas open; the sea one is still shut.
    OBL_CHECK(!IsAreaOpen(f.grid, kKindBoth, 1, valid));
    OBL_CHECK(SetAreaOpen(f.grid, kKindSea, 1));
    OBL_CHECK(IsAreaOpen(f.grid, kKindBoth, 1, valid));
    OBL_CHECK(SetAreaOpen(f.grid, kKindBoth, 0));
    OBL_CHECK(!beach[0].m_shut && !sea[0].m_shut);
    // Other kinds are closed and write nothing; an index out of range is reported, not answered.
    OBL_CHECK(!IsAreaOpen(f.grid, 3, 0, valid));
    OBL_CHECK(valid);
    OBL_CHECK(SetAreaOpen(f.grid, 7, 0));
    IsAreaOpen(f.grid, kKindSea, 2, valid);
    OBL_CHECK(!valid);
    OBL_CHECK(!SetAreaOpen(f.grid, kKindBeach, -1));
    // Grid::SetAreaClosed shuts the same way.
    OBL_CHECK(SetAreaClosed(f.grid, kKindSea, 0));
    OBL_CHECK(sea[0].m_shut && !sea[1].m_shut);
    OBL_CHECK(SetAreaClosed(f.grid, kKindBoth, 1));
    OBL_CHECK(beach[1].m_shut && sea[1].m_shut);
    beach[0].m_shut = false;
    OBL_CHECK(SetAreaClosed(f.grid, 5, 0));
    OBL_CHECK(!beach[0].m_shut);
    OBL_CHECK(!SetAreaClosed(f.grid, kKindBoth, 2));
    OBL_CHECK(!SetAreaClosed(f.grid, kKindBeach, -1));
}

OBL_TEST_CASE("grid_areas.boat_route_records")
{
    Fixture f;
    map::BoatRoute& route = f.grid.m_boat;
    AddBoatPoint(route, 1.0f, 2.0f, 3.0f, 0);
    AddBoatPoint(route, 4.0f, 5.0f, 6.0f, 1);
    AddBoatPoint(route, 7.0f, 8.0f, 9.0f, 2);
    OBL_CHECK_EQ(route.m_numPoints, 3);
    OBL_CHECK_EQ(route.m_unloadPointIndex, 1);
    OBL_CHECK_EQ(route.m_destroyPointIndex, 2);
    OBL_CHECK(route.m_points[1].y == 5.0f);
    OBL_CHECK(route.m_points[2].z == 9.0f);
    SetBoatEnd(route.m_entry, 1.0f, 2.0f, 3.0f, 4, 5.0f, 6.0f);
    SetBoatEnd(route.m_exit, 7.0f, 8.0f, 9.0f, 10, 11.0f, 12.0f);
    OBL_CHECK(route.m_entry.m_pos[2] == 3.0f && route.m_entry.m_dir[1] == 6.0f && route.m_entry.m_number == 4);
    OBL_CHECK(route.m_exit.m_pos[0] == 7.0f && route.m_exit.m_dir[0] == 11.0f && route.m_exit.m_number == 10);
    // The list is bounded: points past the tenth are dropped.
    for (int i = 0; i < 12; ++i) AddBoatPoint(route, 0.0f, 0.0f, 0.0f, 0);
    OBL_CHECK_EQ(route.m_numPoints, map::kMaxBoatPoints);
}

OBL_TEST_CASE("grid_areas.point_inside_a_rectangle_wins_with_the_last_node")
{
    Fixture f;
    f.grid.m_beachAreas = {f.square, f.far, f.square}; // a third area over the same rectangle
    int result = -9;
    // Tile (2, 2) of the staggered grid: world x = 2 * 32, y = 2 * 64 plus a little.
    OBL_CHECK(FindAreaOfPoint(f.grid, f.grid.m_beachAreas, 70.0f, 140.0f, 0.0f, result));
    OBL_CHECK_EQ(result, 2);
}

OBL_TEST_CASE("grid_areas.nearest_vertex_search_without_a_containing_rectangle")
{
    Fixture f;
    f.grid.m_beachAreas = {f.square, f.far};
    int result = -9;
    // Far from every rectangle and next to the vertex (20, 20) of the second node: the vertex of node 1 is nearer than the first node's own.
    const float x = 21 * 32.0f, y = 25 * 64.0f;
    OBL_CHECK(FindAreaOfPoint(f.grid, f.grid.m_beachAreas, x, y, 0.0f, result));
    OBL_CHECK_EQ(result, 1);
    // Next to the corner of the first node: no later vertex beats it, the answer is 0.
    OBL_CHECK(FindAreaOfPoint(f.grid, f.grid.m_beachAreas, 5 * 32.0f + 5.0f, 5 * 64.0f, 0.0f, result));
    OBL_CHECK_EQ(result, 0);
}

OBL_TEST_CASE("grid_areas.find_reports_the_cases_that_would_crash")
{
    Fixture f;
    int result = 0;
    OBL_CHECK(!FindAreaOfPoint(f.grid, {}, 1.0f, 1.0f, 0.0f, result));
    f.square.m_points.clear();
    OBL_CHECK(!FindAreaOfPoint(f.grid, {f.square}, 1000.0f, 1000.0f, 0.0f, result));
    f.grid.m_tileSize = 1;
    OBL_CHECK(!FindAreaOfPoint(f.grid, {f.far}, 1.0f, 1.0f, 0.0f, result));
}

OBL_TEST_CASE("grid_areas.make_area_copies_the_points_and_takes_the_outer_corners")
{
    const uint32_t points[6] = {1, 2, 3, 4, 5, 6};
    const int32_t integers[9] = {10, 11, 12, 13, 14, 15, 16, 17, 18};
    const map::Area area = MakeArea(points, 3, integers);
    OBL_REQUIRE(area.NumPoints() == 3u);
    OBL_CHECK(area.m_points[0].x == 1 && area.m_points[0].y == 2);
    OBL_CHECK(area.m_points[2].x == 5 && area.m_points[2].y == 6);
    OBL_CHECK_EQ(area.m_minX, 10);
    OBL_CHECK_EQ(area.m_minY, 11);
    OBL_CHECK_EQ(area.m_maxX, 16);
    OBL_CHECK_EQ(area.m_maxY, 17);
    OBL_CHECK(!area.m_shut);
}
