#include "unit_test.h"

#include "game/map/grid_state.h"
#include "game/objects/guest_walk_routes.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

using namespace obl;
using namespace obl::guest_walk;

namespace {

// A grid of size x size cells (16 by default, 128 like the game's where the column constants of the code matter) whose flags the test sets.
struct TestGrid {
    obl::map::Grid grid{};
    std::vector<map::GridCell>& cells = grid.m_cells;
    explicit TestGrid(uint32_t size = 16)
    {
        grid.m_tileSize = 64;
        grid.m_width = size;
        grid.m_height = size;
        cells.resize(size * size);
    }
    void Set(uint32_t x, uint32_t y, uint32_t flags) { cells[grid.TileIndex(x, y)].m_flags = flags; }
    uint32_t Flags(uint32_t x, uint32_t y) const { return grid.Cell(x, y).m_flags; }
};

TileXY Step(const TestGrid& g, TileXY tile, int direction)
{
    return grid_math::Neighbour(g.grid.m_width, g.grid.m_height, tile.x, tile.y, direction);
}

// A scripted generator: the next value of the list, then zeros.
struct Script {
    std::vector<int32_t> values;
    size_t next = 0;
    int32_t operator()() { return next < values.size() ? values[next++] : (++next, 0); }
};

// A draw that gives the wanted offset.
int32_t DrawFor(int32_t offset)
{
    for (int32_t draw = 0; draw <= 32767; ++draw)
        if (DanceOffset(draw) == offset) return draw;
    return -1;
}

} // namespace

// ---- StartPathWalk ----------------------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("guest_walk_routes.getonpath_tile_flags")
{
    OBL_CHECK(IsGetonTile(0x10000000));
    OBL_CHECK(IsGetonTile(0x80));
    OBL_CHECK(IsGetonTile(0x10008000)); // the first alternative does not look at the blocked bit
    OBL_CHECK(IsGetonTile(0x2));
    OBL_CHECK(!IsGetonTile(0x2 | 0x10)); // bit 2 needs neither a building body ...
    OBL_CHECK(!IsGetonTile(0x2 | 0x8000)); // ... nor the blocked bit
    OBL_CHECK(IsGetonTile(0x2 | 0x80 | 0x8000)); // unless 0x80 admits it anyway
    OBL_CHECK(!IsGetonTile(0));
    OBL_CHECK(!IsGetonTile(0x40000000 | 0x20000000 | 0x100));
}

OBL_TEST_CASE("guest_walk_routes.getonpath_takes_the_first_ray_of_the_first_round")
{
    TestGrid g;
    const TileXY start{7, 8};
    const TileXY r0 = Step(g, Step(g, start, 0), 0), r1 = Step(g, start, 2), r3 = Step(g, start, 6);
    g.Set(r0.x, r0.y, 0x80);
    g.Set(r1.x, r1.y, 0x80);
    g.Set(r3.x, r3.y, 0x80);
    const auto found = FindGetonTile(g.grid, start);
    OBL_CHECK(found.has_value());
    OBL_CHECK_EQ(found->x, r1.x);
    OBL_CHECK_EQ(found->y, r1.y);
}

OBL_TEST_CASE("guest_walk_routes.getonpath_ends_in_nothing_when_no_ray_meets_a_tile")
{
    TestGrid g;
    OBL_CHECK(!FindGetonTile(g.grid, {3, 3}).has_value());
    // A tile on the ray after a wrap of the 16-tile grid is found: ray 2 (direction 4) walks x by -1 each step with y + 1 on odd columns.
    TileXY tile{3, 3};
    for (int i = 0; i < 20; ++i) tile = Step(g, tile, 4);
    g.Set(tile.x, tile.y, 0x10000000);
    const auto found = FindGetonTile(g.grid, {3, 3});
    OBL_CHECK(found.has_value());
}

// ---- SurfToShore --------------------------------------------------------------------------------------------------------------------------

namespace {

// the expected walk: three flags and an index that is set to 4 and then incremented.
std::vector<TileXY> ExpectedSurfTiles(const TestGrid& g, TileXY start, bool& sent)
{
    TileXY tiles[8];
    unsigned char done[8] = {};
    for (TileXY& t : tiles) t = start;
    std::vector<TileXY> emitted;
    bool found = false;
    for (;;) {
        bool any = false;
        int i = 0;
        do {
            if (done[i] == 0) {
                if (tiles[i].x == 0 || tiles[i].x == 0x7f || tiles[i].y == 0) {
                    done[i] = 1;
                } else {
                    any = true;
                    const TileXY before = tiles[i];
                    tiles[i] = Step(g, tiles[i], i);
                    if (g.Flags(tiles[i].x, tiles[i].y) & 0x8030) done[i] = 1;
                    if (g.Flags(tiles[i].x, tiles[i].y) & 0x100) {
                        found = true;
                        emitted.push_back(before);
                        i = 4;
                    }
                }
            }
            ++i;
        } while (i < 8);
        if (!any) {
            sent = false;
            return emitted;
        }
        if (found) {
            sent = true;
            return emitted;
        }
    }
}

} // namespace

OBL_TEST_CASE("guest_walk_routes.surf_to_beach_matches_the_expected_tiles_on_random_grids")
{
    uint32_t seed = 12345;
    const auto next = [&] { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    for (int grid = 0; grid < 40; ++grid) {
        TestGrid g(128); // the end-of-map column is a constant (0x7f) of the code
        for (uint32_t y = 0; y < 128; ++y)
            for (uint32_t x = 0; x < 128; ++x) {
                uint32_t flags = 0;
                if (next() % 40 == 0) flags |= 0x100;
                if (next() % 6 == 0) flags |= 0x20;
                if (next() % 9 == 0) flags |= 0x8000;
                g.Set(x, y, flags);
            }
        for (int s = 0; s < 8; ++s) {
            const TileXY start{next() % 128, next() % 128};
            bool expectedSent = false;
            const std::vector<TileXY> expected = ExpectedSurfTiles(g, start, expectedSent);
            std::vector<TileXY> actual;
            const bool sent = SurfToBeachRays(g.grid, start, [&](TileXY at) { actual.push_back(at); });
            OBL_CHECK_EQ(sent, expectedSent);
            OBL_CHECK_EQ(actual.size(), expected.size());
            for (size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
                OBL_CHECK_EQ(actual[i].x, expected[i].x);
                OBL_CHECK_EQ(actual[i].y, expected[i].y);
            }
        }
    }
}

OBL_TEST_CASE("guest_walk_routes.surf_to_beach_goes_on_with_ray_five_after_a_find")
{
    TestGrid g(128);
    const TileXY start{7, 8};
    // Rays 0 and 5 both meet an edge on their first step: after ray 0 the loop index becomes 4 and then 5, so ray 5 is stepped in the same round; ray 1 is
    // skipped over (it comes before) and ray 4 is not looked at at all after the find.
    const TileXY e0 = Step(g, start, 0), e5 = Step(g, start, 5), e4 = Step(g, start, 4);
    g.Set(e0.x, e0.y, 0x100);
    g.Set(e5.x, e5.y, 0x100);
    g.Set(e4.x, e4.y, 0x100);
    std::vector<TileXY> emitted;
    OBL_CHECK(SurfToBeachRays(g.grid, start, [&](TileXY at) { emitted.push_back(at); }));
    // Ray 0 found first (it stood on the start tile); ray 4 (an edge too) is passed over this round; ray 5 follows. The second emit also stands on the start.
    OBL_CHECK(emitted.size() >= 2);
    OBL_CHECK_EQ(emitted[0].x, start.x);
    OBL_CHECK_EQ(emitted[0].y, start.y);
    OBL_CHECK_EQ(emitted[1].x, start.x);
    OBL_CHECK_EQ(emitted[1].y, start.y);
}

OBL_TEST_CASE("guest_walk_routes.surf_to_beach_without_an_edge_sends_nothing")
{
    TestGrid g(128);
    int calls = 0;
    OBL_CHECK(!SurfToBeachRays(g.grid, {7, 8}, [&](TileXY) { ++calls; }));
    OBL_CHECK_EQ(calls, 0);
    // A start on the map edge has every ray finished at once.
    OBL_CHECK(!SurfToBeachRays(g.grid, {0, 5}, [&](TileXY) { ++calls; }));
    OBL_CHECK(!SurfToBeachRays(g.grid, {5, 0}, [&](TileXY) { ++calls; }));
    OBL_CHECK_EQ(calls, 0);
}

// ---- WindSurfStraight ---------------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("guest_walk_routes.windsurf_counts_free_water_ahead_up_to_five")
{
    TestGrid g;
    const TileXY start{7, 8};
    for (int direction = 0; direction < 8; ++direction) {
        TileXY tile = start;
        for (int i = 0; i < 8; ++i) {
            tile = Step(g, tile, direction);
            g.Set(tile.x, tile.y, 0x4);
        }
        OBL_CHECK_EQ(WaterAhead(g.grid, start, direction), 5);
        g.Set(Step(g, start, direction).x, Step(g, start, direction).y, 0); // dry first tile: nothing ahead
        OBL_CHECK_EQ(WaterAhead(g.grid, start, direction), 0);
        const TileXY third = TileAhead(g.grid, start, direction, 3);
        g.Set(Step(g, start, direction).x, Step(g, start, direction).y, 0x4);
        g.Set(third.x, third.y, 0x4 | 0x10); // a building body on the third tile
        OBL_CHECK_EQ(WaterAhead(g.grid, start, direction), 2);
        g.Set(third.x, third.y, 0x20); // not water on the third tile
        OBL_CHECK_EQ(WaterAhead(g.grid, start, direction), 2);
        // put the board back to dry for the next direction
        TileXY wipe = start;
        for (int i = 0; i < 8; ++i) {
            wipe = Step(g, wipe, direction);
            g.Set(wipe.x, wipe.y, 0);
        }
    }
}

OBL_TEST_CASE("guest_walk_routes.windsurf_steps_are_a_draw_modulo_the_free_tiles_plus_one")
{
    OBL_CHECK_EQ(WindSurfSteps(0, 5), 1);
    OBL_CHECK_EQ(WindSurfSteps(4, 5), 5);
    OBL_CHECK_EQ(WindSurfSteps(5, 5), 1);
    OBL_CHECK_EQ(WindSurfSteps(32767, 1), 1);
    OBL_CHECK_EQ(WindSurfSteps(32767, 3), 32767 % 3 + 1);
}

OBL_TEST_CASE("guest_walk_routes.tile_ahead_walks_the_direction")
{
    TestGrid g;
    const TileXY start{7, 8};
    TileXY expected = start;
    for (int steps = 0; steps < 6; ++steps) {
        const TileXY actual = TileAhead(g.grid, start, 1, steps);
        OBL_CHECK_EQ(actual.x, expected.x);
        OBL_CHECK_EQ(actual.y, expected.y);
        expected = Step(g, expected, 1);
    }
}

// ---- GotoDancePoint -----------------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("guest_walk_routes.dance_offset_runs_from_minus_eight_to_seven")
{
    OBL_CHECK_EQ(DanceOffset(0), -8);
    OBL_CHECK_EQ(DanceOffset(32767), 7); // draw * (1/32767 as float) * -16 is -15.99999998: truncated to -15
    int32_t previous = -9;
    for (int32_t draw = 0; draw <= 32767; ++draw) {
        const int32_t offset = DanceOffset(draw);
        OBL_CHECK(offset >= -8 && offset <= 7);
        OBL_CHECK(offset >= previous);
        previous = offset;
    }
    OBL_CHECK(DrawFor(0) >= 0);
    OBL_CHECK(DrawFor(-3) >= 0);
    OBL_CHECK_EQ(DanceOffset(DrawFor(5)), 5);
}

OBL_TEST_CASE("guest_walk_routes.dance_floor_swaps_its_sides_on_odd_directions")
{
    const DanceFloor even = DanceFloorOf({4, 5}, 2, 3, 7);
    OBL_CHECK_EQ(even.outer, 3);
    OBL_CHECK_EQ(even.inner, 7);
    const DanceFloor odd = DanceFloorOf({4, 5}, 3, 3, 7);
    OBL_CHECK_EQ(odd.outer, 7);
    OBL_CHECK_EQ(odd.inner, 3);
    const DanceFloor negative = DanceFloorOf({4, 5}, -1, 3, 7); // -1 % 2 is -1 in C, not 0
    OBL_CHECK_EQ(negative.outer, 7);
    OBL_CHECK_EQ(negative.inner, 3);
    const DanceFloor zero = DanceFloorOf({4, 5}, 0, 3, 7);
    OBL_CHECK_EQ(zero.outer, 3);
}

OBL_TEST_CASE("guest_walk_routes.on_dance_floor_covers_outer_rows_of_inner_tiles")
{
    TestGrid g;
    const TileXY anchor{8, 8};
    const DanceFloor floor{anchor, 2, 3}; // 2 rows (direction 6) of 3 tiles (direction 0)
    std::vector<TileXY> inside;
    TileXY row = anchor;
    for (int r = 0; r < 2; ++r) {
        TileXY tile = row;
        for (int c = 0; c < 3; ++c) {
            inside.push_back(tile);
            tile = Step(g, tile, 0);
        }
        row = Step(g, row, 6);
    }
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; ++x) {
            bool expected = false;
            for (const TileXY& t : inside) expected = expected || (t.x == x && t.y == y);
            OBL_CHECK_EQ(OnDanceFloor(g.grid, floor, {static_cast<int32_t>(x), static_cast<int32_t>(y)}), expected);
        }
    OBL_CHECK(!OnDanceFloor(g.grid, DanceFloor{anchor, 0, 3}, {8, 8}));
    OBL_CHECK(!OnDanceFloor(g.grid, DanceFloor{anchor, 2, 0}, {8, 8}));
    OBL_CHECK(!OnDanceFloor(g.grid, DanceFloor{anchor, -1, 3}, {8, 8}));
    OBL_CHECK(!OnDanceFloor(g.grid, floor, {-1, 8})); // a candidate off the map never matches a wrapped tile
}

OBL_TEST_CASE("guest_walk_routes.dance_search_discards_the_first_candidate")
{
    TestGrid g;
    const TileXY guest{8, 8};
    const DanceFloor floor{{8, 8}, 4, 4};
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; ++x) g.Set(x, y, 0x800); // every tile carries the built flag

    // A floor tile that is not the guest's own and lies within the draw's reach.
    int fitX = 0, fitY = 0;
    bool haveFit = false;
    for (int dy = -8; dy <= 7 && !haveFit; ++dy)
        for (int dx = -8; dx <= 7 && !haveFit; ++dx)
            if ((dx != 0 || dy != 0) && OnDanceFloor(g.grid, floor, {8 + dx, 8 + dy})) {
                fitX = dx;
                fitY = dy;
                haveFit = true;
            }
    OBL_CHECK(haveFit);

    // The first candidate, even a perfect one, only starts the loop (the "off the floor" flag starts set); the second is on the floor and is taken.
    Script script;
    script.values = {DrawFor(fitX), DrawFor(fitY), DrawFor(fitX), DrawFor(fitY)};
    auto found = FindDanceTile(g.grid, guest, floor, std::ref(script));
    OBL_CHECK(found.has_value());
    OBL_CHECK_EQ(script.next, 4u); // two draws for the discarded candidate, two for the one taken
    OBL_CHECK_EQ(found->x, 8 + fitX);
    OBL_CHECK_EQ(found->y, 8 + fitY);

    // The guest's own tile is refused: a floor of that one tile never fits.
    Script own;
    own.values = {DrawFor(0), DrawFor(0), DrawFor(0), DrawFor(0), DrawFor(0), DrawFor(0)};
    OBL_CHECK(!FindDanceTile(g.grid, guest, DanceFloor{{8, 8}, 1, 1}, std::ref(own), 3).has_value());
}

OBL_TEST_CASE("guest_walk_routes.dance_search_needs_the_built_flag_and_a_candidate_on_the_map")
{
    TestGrid g;
    const DanceFloor floor{{2, 2}, 8, 8};
    // No tile carries the built flag: nothing fits however long it is drawn.
    int32_t draw = 0;
    OBL_CHECK(!FindDanceTile(g.grid, {2, 2}, floor, [&] { return (draw = (draw + 977) % 32768); }, 200).has_value());

    // With the flag everywhere, a guest at column 1 whose candidates lie at x = -2 is refused: the raw candidate is off the map, no wrapped floor tile equals it.
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; ++x) g.Set(x, y, 0x800);
    Script left;
    for (int i = 0; i < 8; ++i) {
        left.values.push_back(DrawFor(-3));
        left.values.push_back(DrawFor(0));
    }
    OBL_CHECK(!FindDanceTile(g.grid, {1, 2}, DanceFloor{{15, 2}, 3, 3}, std::ref(left), 3).has_value());
}

OBL_TEST_CASE("guest_walk_routes.dance_goal_matches_the_expected_goal")
{
    // x = (float)(((draw * k) * 20 + px) - 10) truncated, y = ((draw * k) * 20 + py) - 10 left a double.
    const float points[][2] = {{0.0f, 0.0f}, {1234.5f, 678.25f}, {4095.0f, 8191.5f}, {16777216.0f, 3e9f}, {-70.5f, -1e9f}};
    const int32_t draws[] = {0, 1, 100, 16383, 16384, 32767};
    for (const auto& p : points)
        for (int32_t dx : draws)
            for (int32_t dy : draws) {
                const double k = static_cast<double>(script::kRandUnit);
                double x = static_cast<double>(dx) * k;
                x = x * 20.0;
                x = x + static_cast<double>(p[0]);
                x = x - 10.0;
                const float roundedX = static_cast<float>(x);
                double y = static_cast<double>(dy) * k;
                y = y * 20.0;
                y = y + static_cast<double>(p[1]);
                y = y - 10.0;
                const Goal expected{FloatToIntWrap(static_cast<double>(roundedX)), FloatToIntWrap(y)};
                const Goal actual = DanceGoal(p[0], p[1], dx, dy);
                OBL_CHECK_EQ(actual.x, expected.x);
                OBL_CHECK_EQ(actual.y, expected.y);
            }
}

OBL_TEST_CASE("guest_walk_routes.dance_goal_y_is_not_rounded_to_float")
{
    // Near 2^24 a float cannot hold the odd sum a double can: the y goal sees the exact one, the x goal the rounded one.
    const float big = 16777216.0f;
    const Goal goal = DanceGoal(big, big, 0, 0);
    OBL_CHECK_EQ(goal.y, static_cast<int32_t>(16777216.0 - 10.0));
    OBL_CHECK_EQ(goal.x, static_cast<int32_t>(static_cast<float>(16777216.0 - 10.0)));
}
