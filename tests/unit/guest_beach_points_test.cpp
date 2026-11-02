#include "unit_test.h"
#include "game/objects/guest_beach_points.h"

#include "game/map/grid_state.h"
#include <cmath>
#include <limits>
#include <vector>

using namespace obl;
using namespace obl::guest_beach;
using map::Area;
using map::GridCell;

namespace {

// A small staggered grid of 64-unit tiles with the given flags on every cell.
struct TestGrid {
    obl::map::Grid grid{};
    std::vector<map::GridCell>& cells = grid.m_cells;
    TestGrid(uint32_t size, uint32_t flags)
    {
        grid.m_tileSize = 64;
        grid.m_width = grid.m_height = size;
        cells.resize(size * size);
        for (GridCell& cell : cells) cell.m_flags = flags;
    }
    uint32_t& Flags(uint32_t x, uint32_t y) { return grid.Cell(x, y).m_flags; }
};

Area MakeArea(const std::vector<uint32_t>& points, int minX, int minY, int maxX, int maxY, uint8_t shut = 0)
{
    Area area;
    for (size_t i = 0; i + 1 < points.size(); i += 2) area.m_points.push_back({points[i], points[i + 1]});
    area.m_minX = minX;
    area.m_minY = minY;
    area.m_maxX = maxX;
    area.m_maxY = maxY;
    area.m_shut = shut != 0;
    return area;
}

// Returns the scripted draws in order and counts them; afterwards `fallback` forever (the tests assert the count).
struct Script {
    std::vector<int> draws;
    int fallback = 0;
    int used = 0;
    int operator()() { return used < static_cast<int>(draws.size()) ? draws[used++] : (++used, fallback); }
};

constexpr int kHalf = 16384; // rand * 1/32767 * 4 = 2.00006: truncates to 2
const float kOrigin[3] = {0.0f, 0.0f, 0.0f};

} // namespace

OBL_TEST_CASE("guest_beach.repeat_action_uses_unsigned_max_and_keeps_unmapped_desires")
{
    std::vector<int> max(26);
    max[0] = 3;
    max[1] = 100;
    max[2] = static_cast<int>(0x80000000u); // loaded as 2^31, not as a negative number
    max[3] = 4;
    OBL_CHECK_EQ(RepeatAction(0, max, 0.5f, 9), 1); // trunc(1.5)
    OBL_CHECK_EQ(RepeatAction(1, max, 0.99f, 9), 99); // 0.99f is slightly above 0.99: 99.0000009 truncates to 99
    OBL_CHECK_EQ(RepeatAction(2, max, 1.0f, 9), static_cast<int32_t>(0x80000000u)); // only 0 is replaced by 1 (unsigned compare)
    OBL_CHECK_EQ(RepeatAction(3, max, -0.5f, 9), -2); // negative stays negative
    OBL_CHECK_EQ(RepeatAction(3, max, std::numeric_limits<float>::quiet_NaN(), 9), 1); // the float-to-int conversion of NaN is 0, then 1
    OBL_CHECK_EQ(RepeatAction(3, max, 0.0f, 9), 1);
    for (int unmapped : {4, 12, 13, 16, 21, 25, -1}) {
        OBL_CHECK_EQ(RepeatAction(unmapped, max, 1.0f, 7), 7);
        OBL_CHECK_EQ(RepeatAction(unmapped, max, 1.0f, 0), 1); // an unset repeat is still raised to 1
    }
}

OBL_TEST_CASE("guest_beach.sunbathe_takes_the_first_free_sand_tile_of_the_nearest_vertexs_node")
{
    TestGrid g(16, 2); // sand everywhere
    std::vector<uint32_t> nearPoints{5, 5}, farPoints{12, 12};
    Area farNode = MakeArea(farPoints, 10, 10, 14, 14);
    Area nearNode = MakeArea(nearPoints, 4, 4, 8, 8);
    // The far node comes first in the chain but the guest stands next to the near node's vertex.
    g.grid.m_beachAreas = {farNode, nearNode};
    const grid_math::WorldPoint vertex = grid_math::TileToWorldPoint(64, 5, 5, 4);
    const float guest[3] = {vertex.x, vertex.y, 0.0f};

    BeachPoints out{};
    Script script;
    script.draws = {0, 0};
    OBL_CHECK(FindSunbathePoint(g.grid, guest, script, out) == Found::Done);
    OBL_CHECK_EQ(script.used, 2);
    const grid_math::WorldPoint tile = grid_math::TileToWorldPoint(64, 4, 4, 4);
    OBL_CHECK_EQ(out.vertex[0], vertex.x);
    OBL_CHECK_EQ(out.vertex[1], vertex.y);
    OBL_CHECK_EQ(out.target[0], tile.x);
    OBL_CHECK_EQ(out.target[1], tile.y);

    // A building next to the first draw rejects it; the second draw (2, 2 tiles in) is accepted.
    g.Flags(4, 4) |= 0x10;
    script = Script{{0, 0, kHalf, kHalf}};
    OBL_CHECK(FindSunbathePoint(g.grid, guest, script, out) == Found::Done);
    OBL_CHECK_EQ(script.used, 4);
    const grid_math::WorldPoint second = grid_math::TileToWorldPoint(64, 6, 6, 4);
    OBL_CHECK_EQ(out.target[0], second.x);
    OBL_CHECK_EQ(out.target[1], second.y);
}

OBL_TEST_CASE("guest_beach.sunbathe_falls_back_to_the_vertex_after_1000_redraws")
{
    TestGrid g(16, 0); // no sand at all
    std::vector<uint32_t> points{5, 5};
    Area node = MakeArea(points, 4, 4, 8, 8);
    g.grid.m_beachAreas = {node};
    BeachPoints out{};
    Script script;
    OBL_CHECK(FindSunbathePoint(g.grid, kOrigin, script, out) == Found::Done);
    OBL_CHECK_EQ(script.used, 2 * 1001); // the first draw and 1000 redraws, two numbers each
    OBL_CHECK_EQ(out.target[0], out.vertex[0]);
    OBL_CHECK_EQ(out.target[1], out.vertex[1]);
    OBL_CHECK_EQ(out.target[2], out.vertex[2]);

    // The 1000th redraw falls back even when it would have been accepted.
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; ++x) g.Flags(x, y) = 2;
    g.Flags(4, 4) = 0; // every draw (0, 0) lands on this tile; the draw after 1000 failures is (kHalf, kHalf) -> tile (6, 6), free sand
    script = Script{};
    script.draws.assign(2 * 1000, 0);
    script.draws.push_back(kHalf);
    script.draws.push_back(kHalf);
    OBL_CHECK(FindSunbathePoint(g.grid, kOrigin, script, out) == Found::Done);
    OBL_CHECK_EQ(script.used, 2 * 1001);
    OBL_CHECK_EQ(out.target[0], out.vertex[0]);
}

OBL_TEST_CASE("guest_beach.sunbathe_reports_unhandled_when_it_cannot_model_the_chain")
{
    TestGrid g(8, 2);
    BeachPoints out{};
    Script script;
    OBL_CHECK(FindSunbathePoint(g.grid, kOrigin, script, out) == Found::Unhandled); // no beach
    std::vector<uint32_t> none;
    Area empty = MakeArea(none, 0, 0, 1, 1);
    g.grid.m_beachAreas = {empty};
    OBL_CHECK(FindSunbathePoint(g.grid, kOrigin, script, out) == Found::Unhandled);
    OBL_CHECK_EQ(script.used, 0);
}

OBL_TEST_CASE("guest_beach.swim_skips_shut_nodes_only_until_the_first_vertex_search_runs_out")
{
    TestGrid g(16, 4); // water everywhere
    std::vector<uint32_t> shutPoints{2, 2}, blockedPoints{6, 6}, openPoints{9, 9};
    Area last = MakeArea(openPoints, 8, 8, 12, 12, 1); // shut, but reached by the hand-over
    Area blocked = MakeArea(blockedPoints, 4, 4, 8, 8); // first node with flag 0
    Area shut = MakeArea(shutPoints, 0, 0, 2, 2, 1); // skipped by the initial filter
    BeachPoints out{};
    Script script;
    script.draws = {0, 0};

    // A shut chain gives nothing.
    g.grid.m_seaAreas = {shut};
    OBL_CHECK(FindSwimPoint(g.grid, 0.0f, 0.0f, script, out) == Found::Nothing);
    OBL_CHECK_EQ(script.used, 0);

    g.grid.m_seaAreas = {shut, blocked, last};
    g.Flags(6, 6) |= 0x10; // its only vertex carries a building: the node runs out and hands over to the shut node
    OBL_CHECK(FindSwimPoint(g.grid, 0.0f, 0.0f, script, out) == Found::Done);
    const grid_math::WorldPoint shore = grid_math::TileToWorldPoint(64, 9, 9, 4);
    OBL_CHECK_EQ(out.vertex[0], shore.x);
    OBL_CHECK_EQ(out.vertex[1], shore.y);
    // The target tile comes from the winning (shut) node's rectangle: (8, 8) from the draw (0, 0), as a tile origin.
    const grid_math::WorldPoint origin = grid_math::TileToWorldPoint(64, 8, 8, 0);
    OBL_CHECK_EQ(out.target[0], origin.x);
    OBL_CHECK_EQ(out.target[1], origin.y);

    // Without the building the first open node's vertex is accepted; the later shut node is not considered for a nearer one.
    g.Flags(6, 6) &= ~0x10u;
    script = Script{{0, 0}};
    OBL_CHECK(FindSwimPoint(g.grid, 5000.0f, 5000.0f, script, out) == Found::Done);
    const grid_math::WorldPoint first = grid_math::TileToWorldPoint(64, 6, 6, 4);
    OBL_CHECK_EQ(out.vertex[0], first.x);
    OBL_CHECK_EQ(out.vertex[1], first.y);
}

OBL_TEST_CASE("guest_beach.swim_prefers_a_strictly_nearer_free_vertex")
{
    TestGrid g(16, 4);
    std::vector<uint32_t> points{3, 3, 12, 12, 12, 13};
    Area node = MakeArea(points, 0, 0, 15, 15);
    g.grid.m_seaAreas = {node};
    BeachPoints out{};
    const grid_math::WorldPoint far = grid_math::TileToWorldPoint(64, 12, 12, 0);
    Script script;
    script.draws = {0, 0};
    OBL_CHECK(FindSwimPoint(g.grid, far.x, far.y, script, out) == Found::Done);
    const grid_math::WorldPoint expected = grid_math::TileToWorldPoint(64, 12, 12, 4);
    OBL_CHECK_EQ(out.vertex[0], expected.x);
    OBL_CHECK_EQ(out.vertex[1], expected.y);

    // Both nearer vertices touch a building (the diagonal neighbour (13, 12) of the even column 12), so the first free vertex stays.
    g.Flags(13, 12) |= 0x10;
    script = Script{{0, 0}};
    OBL_CHECK(FindSwimPoint(g.grid, far.x, far.y, script, out) == Found::Done);
    const grid_math::WorldPoint kept = grid_math::TileToWorldPoint(64, 3, 3, 4);
    OBL_CHECK_EQ(out.vertex[0], kept.x);
    OBL_CHECK_EQ(out.vertex[1], kept.y);
}

OBL_TEST_CASE("guest_beach.swim_redraw_steps_direction_six_once")
{
    TestGrid g(16, 4); // water everywhere
    std::vector<uint32_t> points{3, 3};
    Area node = MakeArea(points, 4, 4, 8, 8);
    g.grid.m_seaAreas = {node};
    // Tile (4, 4): break the tile two steps away in direction 6, leave the one-step neighbour open water.
    grid_math::TileXY one = grid_math::Neighbour(16, 16, 4, 4, 6);
    grid_math::TileXY two = grid_math::Neighbour(16, 16, one.x, one.y, 6);
    g.Flags(two.x, two.y) = 0;
    BeachPoints out{};
    Script script;
    script.draws = {0, 0, 0, 0}; // the first check (two steps) fails, the redraw of the same tile passes with one step
    script.fallback = 40000; // a quirk-free implementation would keep drawing; this makes the count tell
    OBL_CHECK(FindSwimPoint(g.grid, 0.0f, 0.0f, script, out) == Found::Done);
    OBL_CHECK_EQ(script.used, 4);
    const grid_math::WorldPoint tile = grid_math::TileToWorldPoint(64, 4, 4, 0);
    OBL_CHECK_EQ(out.target[0], tile.x);
    OBL_CHECK_EQ(out.target[1], tile.y);
}

OBL_TEST_CASE("guest_beach.swim_reports_unhandled_when_the_chain_runs_out")
{
    TestGrid g(8, 4);
    for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t x = 0; x < 8; ++x) g.Flags(x, y) |= 0x10;
    std::vector<uint32_t> points{2, 2};
    Area node = MakeArea(points, 0, 0, 4, 4);
    g.grid.m_seaAreas = {node};
    BeachPoints out{};
    Script script;
    OBL_CHECK(FindSwimPoint(g.grid, 0.0f, 0.0f, script, out) == Found::Unhandled);
    OBL_CHECK_EQ(script.used, 0);
}

OBL_TEST_CASE("guest_beach.nearest_sea_takes_shut_nodes_that_swim_skips")
{
    TestGrid g(16, 4);
    std::vector<uint32_t> shutPoints{2, 2}, openPoints{9, 9};
    Area open = MakeArea(openPoints, 8, 8, 12, 12);
    Area shut = MakeArea(shutPoints, 0, 0, 4, 4, 1);
    g.grid.m_seaAreas = {shut, open};
    BeachPoints out{};
    Script script;
    script.draws = {0, 0};
    // The first node is shut but is still the start: its free vertex (the guest stands on it) wins and the target comes from its rectangle (tile (0, 0)).
    const grid_math::WorldPoint vertex = grid_math::TileToWorldPoint(64, 2, 2, 4);
    const grid_math::WorldPoint at = grid_math::TileToWorldPoint(64, 2, 2, 0);
    OBL_CHECK(FindNearestSeaPoint(g.grid, at.x, at.y, script, out) == Found::Done);
    OBL_CHECK_EQ(out.vertex[0], vertex.x);
    OBL_CHECK_EQ(out.vertex[1], vertex.y);
    const grid_math::WorldPoint origin = grid_math::TileToWorldPoint(64, 0, 0, 0);
    OBL_CHECK_EQ(out.target[0], origin.x);
    OBL_CHECK_EQ(out.target[1], origin.y);

    // A strictly nearer vertex of a later shut node wins too (swim would not look at it).
    Area first = MakeArea(openPoints, 8, 8, 12, 12);
    Area later = MakeArea(shutPoints, 0, 0, 4, 4, 1);
    g.grid.m_seaAreas = {first, later};
    const grid_math::WorldPoint near = grid_math::TileToWorldPoint(64, 2, 2, 0);
    script = Script{{0, 0}};
    OBL_CHECK(FindNearestSeaPoint(g.grid, near.x, near.y, script, out) == Found::Done);
    OBL_CHECK_EQ(out.vertex[0], vertex.x);
    script = Script{{0, 0}};
    OBL_CHECK(FindSwimPoint(g.grid, near.x, near.y, script, out) == Found::Done);
    const grid_math::WorldPoint kept = grid_math::TileToWorldPoint(64, 9, 9, 4);
    OBL_CHECK_EQ(out.vertex[0], kept.x);

    g.grid.m_seaAreas.clear();
    OBL_CHECK(FindNearestSeaPoint(g.grid, 0.0f, 0.0f, script, out) == Found::Unhandled);
    OBL_CHECK(FindSwimPoint(g.grid, 0.0f, 0.0f, script, out) == Found::Nothing);
}

OBL_TEST_CASE("guest_beach.wander_walks_to_the_middle_of_a_free_land_tile_of_the_nearest_node")
{
    TestGrid g(16, 2);
    std::vector<uint32_t> farPoints{12, 12, 13, 13}, nearPoints{5, 5};
    Area nearNode = MakeArea(nearPoints, 4, 4, 8, 8);
    Area farNode = MakeArea(farPoints, 10, 10, 14, 14);
    g.grid.m_beachAreas = {farNode, nearNode};
    const grid_math::WorldPoint vertex = grid_math::TileToWorldPoint(64, 5, 5, 0);
    const float guest[3] = {vertex.x, vertex.y, 0.0f};
    OBL_CHECK(!WanderDeclined(g.grid));

    // A building on the first drawn tile (4, 4) and a tile off the 128-map are redrawn; the draw (kHalf, kHalf) lands on (6, 6).
    g.Flags(4, 4) |= 0x10;
    Script script;
    script.draws = {0, 0, kHalf, kHalf};
    const grid_math::WorldPoint target = WanderTarget(g.grid, guest, script);
    OBL_CHECK_EQ(script.used, 4);
    const grid_math::PathPoint center = grid_math::TileToWorldCenter(64, 6, 6);
    OBL_CHECK_EQ(target.x, center.x);
    OBL_CHECK_EQ(target.y, center.y);
    OBL_CHECK_EQ(target.z, 0.0f);

    // Water is rejected as well: (6, 6) turns to water, the third draw (0, kHalf) lands on the free tile (4, 6).
    g.Flags(6, 6) = 4;
    script = Script{{0, 0, kHalf, kHalf, 0, kHalf}};
    const grid_math::WorldPoint third = WanderTarget(g.grid, guest, script);
    OBL_CHECK_EQ(script.used, 6);
    const grid_math::PathPoint expected = grid_math::TileToWorldCenter(64, 4, 6);
    OBL_CHECK_EQ(third.x, expected.x);
    OBL_CHECK_EQ(third.y, expected.y);
}

OBL_TEST_CASE("guest_beach.wander_declines_a_chain_it_does_not_model")
{
    TestGrid g(8, 2);
    OBL_CHECK(WanderDeclined(g.grid));
    std::vector<uint32_t> none;
    Area empty = MakeArea(none, 0, 0, 1, 1);
    g.grid.m_beachAreas = {empty};
    OBL_CHECK(WanderDeclined(g.grid));
    OBL_CHECK(WanderTileOk(g.grid, 3, 3));
    OBL_CHECK(!WanderTileOk(g.grid, 0xffffffffu, 3)); // a wrapped negative column wraps onto the grid but is outside the map
    g.Flags(3, 3) = 2 | 0x10;
    OBL_CHECK(!WanderTileOk(g.grid, 3, 3));
}
