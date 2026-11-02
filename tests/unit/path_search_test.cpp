#include "unit_test.h"
#include "game/map/path_search.h"

#include "game/map/grid_state.h"
#include <array>

using namespace obl;
namespace {
struct Fixture {
    obl::map::Grid grid{};
    std::vector<map::GridCell>& cells = grid.m_cells;
    path::PathSearch search{};
    Fixture(uint32_t width = 16, uint32_t height = 16)
    {
        grid.m_width = width;
        grid.m_height = height;
        grid.m_tileSize = 64;
        cells.resize(width * height);
        search.m_grid = &grid;
        search.m_search = 7;
        path::PrepareNodes(search);
        path::ClearOpenList(search);
    }
    path::Node& Node(uint32_t x, uint32_t y) { return path::NodeAt(search, x, y); }
};
} // namespace

OBL_TEST_CASE("path_search.prepare_nodes_gives_tiles_and_keeps_stale_state")
{
    Fixture f(16, 16);
    OBL_REQUIRE(f.search.m_nodes.size() == 256u);
    OBL_CHECK_EQ(f.Node(5, 9).m_tileX, 5u);
    OBL_CHECK_EQ(f.Node(5, 9).m_tileY, 9u);
    OBL_CHECK(&f.Node(16 + 5, 9) == &f.Node(5, 9)); // coordinates wrap like the cells
    f.Node(5, 9).m_g = 1234;
    path::PrepareNodes(f.search);
    OBL_CHECK_EQ(f.Node(5, 9).m_g, 1234u); // a sized array is not cleared between searches
}

OBL_TEST_CASE("path_search.clear_resets_the_open_head")
{
    Fixture f;
    auto& h = f.search.m_openHead;
    h.m_open = h.m_closed = 1;
    h.m_successors[3] = &f.search.m_nodes[2];
    f.search.m_endTile = &f.search.m_nodes[1];
    path::ClearOpenList(f.search);
    OBL_CHECK_EQ(h.m_search[1], 7u);
    OBL_CHECK_EQ(h.m_search[0], 0u);
    OBL_CHECK_EQ(h.m_tileX, 0xffffffffu);
    OBL_CHECK(h.m_successors[3] == nullptr);
    OBL_CHECK(f.search.m_endTile == nullptr);
}

OBL_TEST_CASE("path_search.expand_direction_order_and_alternating_costs")
{
    Fixture f;
    auto& root = f.Node(8, 8);
    root.m_search[0] = 7;
    std::vector<grid_math::TileXY> visited;
    path::ExpandNeighbours(f.search, root, [&](uint32_t x, uint32_t y) {
        visited.push_back({x, y});
        return true;
    });
    OBL_REQUIRE(visited.size() == 8);
    for (int i = 0; i < 8; ++i) {
        const auto n = grid_math::Neighbour(16, 16, 8, 8, (i + 7) % 8);
        OBL_CHECK_EQ(visited[i].x, n.x);
        OBL_CHECK_EQ(visited[i].y, n.y);
        OBL_REQUIRE(root.m_successors[i] == &f.Node(n.x, n.y));
        OBL_CHECK_EQ(root.m_successors[i]->m_g, i % 2 ? 0x800u : 0x1000u);
    }
}

OBL_TEST_CASE("path_search.open_ties_and_stale_early_return")
{
    Fixture f;
    auto& a = f.search.m_nodes[0];
    auto& b = f.search.m_nodes[1];
    auto& stale = f.search.m_nodes[2];
    a.m_search[0] = b.m_search[0] = 7;
    a.m_f = b.m_f = 100;
    a.m_nextOpen = &stale;
    a.m_search[1] = 6;
    path::InsertOpenTile(f.search, a);
    OBL_CHECK(a.m_nextOpen == &stale); // empty-list insertion leaves a stale link in place
    OBL_CHECK_EQ(a.m_search[1], 6u);
    path::InsertOpenTile(f.search, b);
    OBL_CHECK(f.search.m_openHead.m_nextOpen == &b);
    OBL_CHECK(b.m_nextOpen == &a); // equal f goes before the existing cell
    OBL_CHECK_EQ(f.search.m_openHead.m_search[0], 7u);
    b.m_g = 1000;
    b.m_h = 100;
    a.m_g = 9000;
    a.m_h = 100;
    a.m_open = 1;
    path::RelaxNeighbour(f.search, b, 0, 0, 0x800);
    OBL_CHECK_EQ(a.m_g, 3048u);
    OBL_CHECK(f.search.m_openHead.m_nextOpen == &b); // an improvement never reorders the open list
}

OBL_TEST_CASE("path_search.propagate_plus_one_lifo_and_stale_successor_stop")
{
    Fixture f;
    auto& root = f.search.m_nodes[0];
    auto& a = f.search.m_nodes[1];
    auto& b = f.search.m_nodes[2];
    auto& leaf = f.search.m_nodes[3];
    for (auto* c : {&root, &a, &b, &leaf}) {
        c->m_search[0] = 7;
        c->m_g = 100;
        c->m_h = 20;
    }
    root.m_g = 5;
    root.m_successors[0] = &a;
    root.m_successors[1] = &b;
    a.m_successors[0] = b.m_successors[0] = &leaf;
    path::PropagateLowerCost(f.search, root);
    OBL_CHECK_EQ(a.m_g, 6u);
    OBL_CHECK_EQ(b.m_g, 6u);
    OBL_CHECK_EQ(leaf.m_g, 7u);
    OBL_CHECK_EQ(leaf.m_f, 27u);
    OBL_CHECK(leaf.m_parent == &b); // last pushed successor wins the equal-cost race
    a.m_search[0] = 6;
    b.m_g = 100;
    path::PropagateLowerCost(f.search, root);
    OBL_CHECK_EQ(b.m_g, 100u); // a stale first entry terminates iteration
}

OBL_TEST_CASE("path_search.relax_open_precedes_closed_and_full_successors")
{
    Fixture f;
    auto& root = f.search.m_nodes[0];
    auto& child = f.search.m_nodes[1];
    auto& leaf = f.search.m_nodes[2];
    for (auto* c : {&root, &child, &leaf}) c->m_search[0] = 7;
    root.m_g = 10;
    child.m_g = 9000;
    leaf.m_g = 50000;
    child.m_open = child.m_closed = 1;
    child.m_successors[0] = &leaf;
    for (auto& slot : root.m_successors) slot = &leaf;
    path::RelaxNeighbour(f.search, root, 1, 0, 0x800);
    OBL_CHECK_EQ(child.m_g, 2058u);
    OBL_CHECK_EQ(leaf.m_g, 50000u); // open branch suppresses propagation despite closed == 1
    for (auto* slot : root.m_successors) OBL_CHECK(slot == &leaf);
    child.m_open = 0;
    child.m_g = 9000;
    path::RelaxNeighbour(f.search, root, 1, 0, 0x800);
    OBL_CHECK_EQ(leaf.m_g, 2059u);
    OBL_CHECK(child.m_parent == &root);
}

OBL_TEST_CASE("path_search.same_tile_blocked_target_and_failure")
{
    Fixture f;
    OBL_CHECK(path::Search(f.search, 3, 4, 3, 4, [](uint32_t, uint32_t) { return false; }));
    OBL_CHECK(f.search.m_endTile == &f.Node(3, 4));
    OBL_CHECK_EQ(f.search.m_endTile->m_h, 0u);
    OBL_CHECK(f.search.m_endTile->m_parent == nullptr);
    ++f.search.m_search;
    path::ClearOpenList(f.search);
    OBL_CHECK(!path::Search(f.search, 1, 1, 7, 7, [](uint32_t, uint32_t) { return false; }));
    OBL_CHECK(f.search.m_endTile == nullptr);
    OBL_CHECK_EQ(f.Node(7, 7).m_h, 0u);
    OBL_CHECK_EQ(f.Node(7, 7).m_open, 1); // popped cells keep both bytes set
    OBL_CHECK_EQ(f.Node(7, 7).m_closed, 1);
    ++f.search.m_search;
    path::ClearOpenList(f.search);
    OBL_CHECK(path::Search(f.search, 1, 1, 7, 7, [](uint32_t, uint32_t) { return true; }));
    auto* c = f.search.m_endTile;
    size_t count = 0;
    while (c && count <= f.search.m_nodes.size()) {
        ++count;
        if (!c->m_parent) break;
        c = c->m_parent;
    }
    OBL_CHECK(c == &f.Node(7, 7));
    OBL_CHECK(count <= f.search.m_nodes.size());
}
