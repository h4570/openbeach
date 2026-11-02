#include "unit_test.h"
#include "game/map/path_route.h"

#include "game/map/grid_state.h"
#include <cstdlib>
#include <memory>
#include <cstring>
#include <set>

using namespace obl;
using obl::path::RouteKind;
namespace {
bool FixturePredicate(const path::PathSearch&, uint32_t, uint32_t)
{
    return true;
}

struct Fixture {
    obl::map::Grid grid{};
    std::vector<map::GridCell>& cells = grid.m_cells;
    path::PathSearch search{};
    int clears = 0;
    int allocations = 0;
    std::vector<void*> blocks;
    explicit Fixture(uint32_t width = 16, uint32_t height = 16, uint32_t flags = 0x90000000u)
    {
        grid.m_width = width;
        grid.m_height = height;
        grid.m_tileSize = 64;
        cells.resize(width * height);
        search.m_grid = &grid;
        search.m_search = 7;
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x) grid.Cell(x, y).m_flags = flags;
        path::ClearOpenList(search);
    }
    ~Fixture()
    {
        for (void* p : blocks) std::free(p);
    }
    void* Allocate(uint32_t bytes)
    {
        ++allocations;
        blocks.push_back(std::malloc(bytes ? bytes : 1));
        return blocks.back();
    }
    int Find(RouteKind kind, uint8_t option, uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, uint32_t** out)
    {
        return FindWith(kind, option, sx, sy, tx, ty, out, [&](uint32_t bytes) { return Allocate(bytes); });
    }
    template<class Alloc>
    int FindWith(RouteKind kind, uint8_t option, uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, uint32_t** out, Alloc&& allocate)
    {
        return path::FindRoute(
                kind, search, FixturePredicate, option, sx, sy, tx, ty, out,
                [&](uint32_t x, uint32_t y) { return (grid.Cell(x, y).m_flags & 0x80000000u) != 0; },
                [&] {
                    ++clears;
                    path::ClearOpenList(search);
                },
                allocate);
    }
};
bool Adjacent(uint32_t ax, uint32_t ay, uint32_t bx, uint32_t by)
{
    for (int d = 0; d < 8; ++d) {
        const auto n = grid_math::Neighbour(16, 16, ax, ay, d);
        if (n.x == bx && n.y == by) return true;
    }
    return false;
}
} // namespace

OBL_TEST_CASE("path_route.land_route_skips_both_ends_and_is_contiguous")
{
    Fixture f;
    uint32_t* tiles = nullptr;
    const int n = f.Find(RouteKind::Land, 1, 2, 3, 10, 11, &tiles);
    OBL_REQUIRE(n > 0);
    OBL_REQUIRE(tiles != nullptr);
    OBL_CHECK_EQ(f.clears, 1);
    OBL_CHECK_EQ(f.allocations, 1);
    OBL_CHECK(Adjacent(2, 3, tiles[0], tiles[1]));
    OBL_CHECK(Adjacent(tiles[(n - 1) * 2], tiles[(n - 1) * 2 + 1], 10, 11));
    for (int i = 1; i < n; ++i) OBL_CHECK(Adjacent(tiles[i * 2 - 2], tiles[i * 2 - 1], tiles[i * 2], tiles[i * 2 + 1]));
    for (int i = 0; i < n; ++i) {
        OBL_CHECK(!(tiles[i * 2] == 2 && tiles[i * 2 + 1] == 3));
        OBL_CHECK(!(tiles[i * 2] == 10 && tiles[i * 2 + 1] == 11));
    }
    OBL_CHECK(f.search.m_endTile == nullptr); // ClearOpenList forgets the end tile
}

OBL_TEST_CASE("path_route.sea_route_keeps_the_whole_chain")
{
    Fixture f;
    uint32_t* land = nullptr;
    uint32_t* sea = nullptr;
    const int landCount = f.Find(RouteKind::Land, 0, 2, 3, 10, 11, &land);
    const int seaCount = f.Find(RouteKind::Sea, 0, 2, 3, 10, 11, &sea);
    OBL_CHECK_EQ(seaCount, landCount + 2);
    OBL_REQUIRE(sea != nullptr);
    OBL_CHECK_EQ(sea[0], 2u);
    OBL_CHECK_EQ(sea[1], 3u);
    OBL_CHECK_EQ(sea[(seaCount - 1) * 2], 10u);
    OBL_CHECK_EQ(sea[(seaCount - 1) * 2 + 1], 11u);
    // The sea flag is on only while the search runs.
    OBL_CHECK_EQ(f.search.m_seaMode, 0);
}

OBL_TEST_CASE("path_route.mode_bytes_and_search_counter")
{
    Fixture f;
    uint32_t* tiles = nullptr;
    f.search.m_seaMode = 1;
    f.search.m_pavementMode = 1;
    f.search.m_shallowMode = 1;
    f.Find(RouteKind::Land, 1, 1, 1, 6, 6, &tiles);
    OBL_CHECK_EQ(f.search.m_shallowMode, 1);
    OBL_CHECK_EQ(f.search.m_pavementMode, 0);
    OBL_CHECK_EQ(f.search.m_seaMode, 1); // sea byte left alone
    OBL_CHECK_EQ(f.search.m_search, 8u);
    OBL_CHECK(f.search.m_isTileAllowed == FixturePredicate);
    f.Find(RouteKind::LandOrBlocked, 0x2a, 1, 1, 6, 6, &tiles);
    OBL_CHECK_EQ(f.search.m_shallowMode, 0x2a); // the raw byte, not a normalised bool
    f.Find(RouteKind::Pavement, 0, 1, 1, 6, 6, &tiles);
    OBL_CHECK_EQ(f.search.m_pavementMode, 1);
    OBL_CHECK_EQ(f.search.m_shallowMode, 0);
    OBL_CHECK_EQ(f.search.m_search, 10u);
    f.Find(RouteKind::Sea, 0, 1, 1, 6, 6, &tiles);
    OBL_CHECK_EQ(f.search.m_seaMode, 0);
    OBL_CHECK_EQ(f.search.m_pavementMode, 0);
    OBL_CHECK_EQ(f.search.m_startX, 1u);
    OBL_CHECK_EQ(f.search.m_startY, 1u);
}

OBL_TEST_CASE("path_route.failures_do_not_clear_or_allocate")
{
    Fixture f;
    uint32_t* tiles = nullptr;
    OBL_CHECK_EQ(f.Find(RouteKind::Land, 0, 4, 4, 4, 4, &tiles), 0); // equal endpoints
    f.grid.Cell(9, 9).m_flags = 0;
    OBL_CHECK_EQ(f.Find(RouteKind::Land, 0, 4, 4, 9, 9, &tiles), 0); // blocked stop
    OBL_CHECK_EQ(f.Find(RouteKind::Pavement, 0, 9, 9, 4, 4, &tiles), 0); // blocked start
    f.search.m_seaMode = 1;
    OBL_CHECK_EQ(f.Find(RouteKind::Sea, 0, 9, 9, 4, 4, &tiles), 0);
    OBL_CHECK_EQ(f.search.m_seaMode, 0); // reset on every exit of the sea route
    // A wall makes the stop unreachable: Search fails after having been run.
    Fixture g;
    // The grid wraps vertically, so a second wall at y == 0 closes the detour.
    for (uint32_t x = 0; x < 16; ++x) g.grid.Cell(x, 8).m_flags = g.grid.Cell(x, 0).m_flags = 0x40000000u;
    OBL_CHECK_EQ(g.Find(RouteKind::Land, 0, 4, 2, 4, 12, &tiles), 0);
    OBL_CHECK_EQ(f.clears + g.clears, 0);
    OBL_CHECK_EQ(f.allocations + g.allocations, 0);
    OBL_CHECK(tiles == nullptr);
}

OBL_TEST_CASE("path_route.adjacent_endpoints_have_no_interior")
{
    Fixture f;
    const auto n = grid_math::Neighbour(16, 16, 5, 5, 3);
    uint32_t* tiles = nullptr;
    OBL_CHECK_EQ(f.Find(RouteKind::Land, 0, 5, 5, n.x, n.y, &tiles), 0);
    OBL_CHECK_EQ(f.Find(RouteKind::Pavement, 0, 5, 5, n.x, n.y, &tiles), 0);
    OBL_CHECK(tiles == nullptr); // no array: the interior is empty
    OBL_CHECK_EQ(f.allocations, 0);
    OBL_CHECK_EQ(f.clears, 2); // the search succeeded, so the open list is still cleared
    OBL_CHECK_EQ(f.Find(RouteKind::Sea, 0, 5, 5, n.x, n.y, &tiles), 2);
    OBL_REQUIRE(tiles != nullptr);
    OBL_CHECK_EQ(tiles[0], 5u);
    OBL_CHECK_EQ(tiles[2], n.x);
}

OBL_TEST_CASE("path_route.allocation_failure_leaves_a_null_array")
{
    Fixture f;
    uint32_t* tiles = reinterpret_cast<uint32_t*>(0x1234);
    const int n = path::FindRoute(
            RouteKind::Land, f.search, FixturePredicate, 0, 1, 1, 8, 8, &tiles,
            [&](uint32_t x, uint32_t y) { return (f.grid.Cell(x, y).m_flags & 0x80000000u) != 0; }, [&] { path::ClearOpenList(f.search); },
            [](uint32_t) -> void* { return nullptr; });
    OBL_CHECK(n > 0);
    OBL_CHECK(tiles == nullptr);
}

OBL_TEST_CASE("path_route.create_builds_route_and_cursor")
{
    Fixture f;
    bool found = true;
    uint32_t* written = nullptr;
    auto cursor = path::CreateRoute(RouteKind::LandOrBlocked, 2, 3, 10, 11, &found, 12.5f, [&](const auto& allocate, uint32_t** out) {
        const int n = f.FindWith(RouteKind::LandOrBlocked, 0, 2, 3, 10, 11, out, allocate);
        written = *out;
        return n;
    });
    OBL_REQUIRE(cursor != nullptr);
    OBL_CHECK(found); // success leaves the caller's value alone
    const auto& data = *cursor->m_route;
    OBL_CHECK_EQ(data.m_referenceCount, 1);
    OBL_CHECK_EQ(data.m_pathType, 4);
    OBL_CHECK_EQ(data.m_start[0], 2);
    OBL_CHECK_EQ(data.m_start[1], 3);
    OBL_CHECK_EQ(data.m_stop[0], 10);
    OBL_CHECK_EQ(data.m_stop[1], 11);
    OBL_CHECK(data.m_timeStamp == 12.5f);
    OBL_CHECK_EQ(data.m_valid, 1);
    OBL_CHECK(!data.m_steps.empty());
    OBL_CHECK(reinterpret_cast<const uint32_t*>(data.m_steps.data()) == written); // the search wrote into the route's own steps
    OBL_CHECK_EQ(cursor->m_reverse, 0);
    OBL_CHECK_EQ(cursor->m_currentStep, 0);
    OBL_CHECK_EQ(cursor->m_route.use_count(), 1);
    // The same search into a separate array gives the same tiles.
    Fixture g;
    uint32_t* tiles = nullptr;
    const int n = g.Find(RouteKind::LandOrBlocked, 0, 2, 3, 10, 11, &tiles);
    OBL_REQUIRE(n == static_cast<int>(data.m_steps.size()));
    for (int i = 0; i < n; ++i) {
        OBL_CHECK_EQ(data.m_steps[i].x, tiles[2 * i]);
        OBL_CHECK_EQ(data.m_steps[i].y, tiles[2 * i + 1]);
    }
}

OBL_TEST_CASE("path_route.create_path_types")
{
    Fixture f;
    for (auto [kind, type] :
            {std::pair{RouteKind::Land, 1}, {RouteKind::LandOrBlocked, 4}, {RouteKind::Sea, 3}, {RouteKind::Pavement, 0}}) {
        bool found = true;
        auto cursor = path::CreateRoute(kind, 1, 1, 9, 9, &found, 0,
                [&](const auto& allocate, uint32_t** out) { return f.FindWith(kind, 0, 1, 1, 9, 9, out, allocate); });
        OBL_REQUIRE(cursor != nullptr);
        OBL_CHECK_EQ(cursor->m_route->m_pathType, type);
    }
}

OBL_TEST_CASE("path_route.create_without_route_clears_found_and_returns_null")
{
    Fixture f;
    bool found = true;
    auto cursor = path::CreateRoute(RouteKind::Land, 1, 1, 1, 1, &found, 0,
            [&](const auto& allocate, uint32_t** out) { return f.FindWith(RouteKind::Land, 0, 1, 1, 1, 1, out, allocate); });
    OBL_CHECK(cursor == nullptr);
    OBL_CHECK(!found);
    // A positive count with a missing array is also "not found".
    found = true;
    cursor = path::CreateRoute(RouteKind::Land, 1, 1, 5, 5, &found, 0, [](const auto&, uint32_t**) { return 3; });
    OBL_CHECK(cursor == nullptr);
    OBL_CHECK(!found);
}

namespace {
struct Beach {
    Fixture f{16, 16, 0};
    uint32_t& Flags(uint32_t x, uint32_t y) { return f.grid.Cell(x, y).m_flags; }
};
} // namespace

OBL_TEST_CASE("path_route.prepare_accepts_matching_tile_unchanged")
{
    Beach b;
    b.Flags(5, 5) = 0x80000000u;
    uint32_t start[2] = {5, 5}, stop[2] = {5, 5};
    OBL_CHECK(path::PrepareRouteEndpoints(b.f.grid, start, stop, 0x80000000u));
    OBL_CHECK_EQ(start[0], 5u);
    OBL_CHECK_EQ(stop[1], 5u);
    // Scenery bit disqualifies the tile (and it is not a beach tile), so the search moves to a neighbour 0/2/4/6.
    b.Flags(5, 5) = 0x80008000u;
    const auto n2 = grid_math::Neighbour(16, 16, 5, 5, 2);
    b.Flags(n2.x, n2.y) = 0x80000000u;
    uint32_t s2[2] = {5, 5}, t2[2] = {5, 5};
    OBL_CHECK(path::PrepareRouteEndpoints(b.f.grid, s2, t2, 0x80000000u));
    OBL_CHECK_EQ(s2[0], n2.x);
    OBL_CHECK_EQ(s2[1], n2.y);
    OBL_CHECK_EQ(t2[0], n2.x);
    OBL_CHECK_EQ(t2[1], n2.y);
}

OBL_TEST_CASE("path_route.prepare_neighbour_search_uses_even_directions_in_order")
{
    Beach b;
    const auto d1 = grid_math::Neighbour(16, 16, 6, 6, 1);
    const auto d4 = grid_math::Neighbour(16, 16, 6, 6, 4);
    const auto d6 = grid_math::Neighbour(16, 16, 6, 6, 6);
    b.Flags(d1.x, d1.y) = 0x20000000u; // odd direction: never considered
    b.Flags(d6.x, d6.y) = 0x20000000u;
    b.Flags(d4.x, d4.y) = 0x20000000u;
    uint32_t start[2] = {6, 6}, stop[2] = {6, 6};
    OBL_CHECK(path::PrepareRouteEndpoints(b.f.grid, start, stop, 0x20000000u));
    OBL_CHECK_EQ(start[0], d4.x);
    OBL_CHECK_EQ(start[1], d4.y); // 4 comes before 6
    b.Flags(d4.x, d4.y) = 0x20008000u; // scenery neighbours are skipped
    uint32_t s2[2] = {6, 6}, t2[2] = {6, 6};
    OBL_CHECK(path::PrepareRouteEndpoints(b.f.grid, s2, t2, 0x20000000u));
    OBL_CHECK_EQ(s2[0], d6.x);
    OBL_CHECK_EQ(s2[1], d6.y);
    // Nothing usable: the call fails and leaves the caller's endpoints alone.
    b.Flags(d6.x, d6.y) = 0;
    uint32_t s3[2] = {6, 6}, t3[2] = {9, 9};
    b.Flags(9, 9) = 0x20000000u;
    OBL_CHECK(!path::PrepareRouteEndpoints(b.f.grid, s3, t3, 0x20000000u));
    OBL_CHECK_EQ(s3[0], 6u);
    OBL_CHECK_EQ(t3[0], 9u);
}

OBL_TEST_CASE("path_route.prepare_beach_tile_gets_temporary_bits_and_clear_removes_them")
{
    Beach b;
    b.Flags(7, 7) = 0x100;
    for (int d = 0; d < 8; ++d) {
        const auto n = grid_math::Neighbour(16, 16, 7, 7, d);
        b.Flags(n.x, n.y) = d % 2 ? 0x100u : 0x110u; // odd directions are free beach, even ones blocked
    }
    const auto blocked = grid_math::Neighbour(16, 16, 7, 7, 0);
    const auto free = grid_math::Neighbour(16, 16, 7, 7, 1);
    uint32_t start[2] = {7, 7}, stop[2] = {7, 7};
    OBL_CHECK(path::PrepareRouteEndpoints(b.f.grid, start, stop, 0x80u));
    OBL_CHECK_EQ(b.Flags(7, 7), 0xc0000180u);
    OBL_CHECK_EQ(b.Flags(free.x, free.y), 0xc0000180u);
    OBL_CHECK_EQ(b.Flags(blocked.x, blocked.y), 0x110u);
    OBL_CHECK_EQ(start[0], 7u); // a beach tile is not moved
    // 0x2fffff7f also removes bit 28 (0x10000000) of the tiles it cleans.
    b.Flags(7, 7) |= 0x10000000u;
    path::ClearTemporaryRouteTiles(b.f.grid, start, stop);
    OBL_CHECK_EQ(b.Flags(7, 7), 0x100u);
    OBL_CHECK_EQ(b.Flags(free.x, free.y), 0x100u);
    OBL_CHECK_EQ(b.Flags(blocked.x, blocked.y), 0x110u);
}

OBL_TEST_CASE("path_route.prepare_beach_without_matching_mask_fails_but_keeps_bits")
{
    Beach b;
    b.Flags(4, 4) = 0x100;
    uint32_t start[2] = {4, 4}, stop[2] = {4, 4};
    OBL_CHECK(!path::PrepareRouteEndpoints(b.f.grid, start, stop, 0x10000000u)); // 0xc0000080 does not contain the pavement bit
    OBL_CHECK_EQ(b.Flags(4, 4), 0xc0000180u);
    OBL_CHECK(path::PrepareRouteEndpoints(b.f.grid, start, stop, 0x80000000u)); // now the temporary bit satisfies the mask
}

OBL_TEST_CASE("path_route.clear_only_touches_free_beach_endpoints")
{
    Beach b;
    b.Flags(3, 3) = 0xc0000080u; // not a beach tile: untouched even though it carries the temporary bits
    uint32_t start[2] = {3, 3}, stop[2] = {12, 12};
    b.Flags(12, 12) = 0xc0000180u;
    const auto n = grid_math::Neighbour(16, 16, 12, 12, 5);
    b.Flags(n.x, n.y) = 0xc0000180u;
    path::ClearTemporaryRouteTiles(b.f.grid, start, stop);
    OBL_CHECK_EQ(b.Flags(3, 3), 0xc0000080u);
    OBL_CHECK_EQ(b.Flags(12, 12), 0x00000180u & 0x2fffff7fu);
    OBL_CHECK_EQ(b.Flags(n.x, n.y), 0x180u & 0x2fffff7fu);
}
