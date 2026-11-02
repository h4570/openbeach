// Tile geometry of the staggered terrain grid (src/game/map/grid_math.h): neighbours per column parity and tile -> world points.

#include "unit_test.h"

#include "game/map/grid_math.h"

#include <cmath>
#include <cstdint>

using namespace obl::grid_math;

namespace {

struct Offset {
    int dx;
    int dy;
};

// Neighbour offsets per direction for an even / odd column x (Grid::GetNeighbour).
constexpr Offset kEven[8] = {{1, -1}, {2, 0}, {1, 0}, {0, 1}, {-1, 0}, {-2, 0}, {-1, -1}, {0, -1}};
constexpr Offset kOdd[8] = {{1, 0}, {2, 0}, {1, 1}, {0, 1}, {-1, 1}, {-2, 0}, {-1, 0}, {0, -1}};

} // namespace

OBL_TEST_CASE("grid_math.neighbour_offsets_per_column_parity")
{
    for (uint32_t x : {4u, 5u, 40u, 41u}) {
        const Offset* table = (x & 1) ? kOdd : kEven;
        for (int d = 0; d < 8; ++d) {
            TileXY n = Neighbour(128, 128, x, 10, d);
            OBL_CHECK_EQ(n.x, static_cast<uint32_t>(static_cast<int>(x) + table[d].dx));
            OBL_CHECK_EQ(n.y, static_cast<uint32_t>(10 + table[d].dy));
        }
    }
}

OBL_TEST_CASE("grid_math.opposite_direction_returns_to_the_tile")
{
    // Direction d and (d + 4) % 8 undo each other in both parities: the table is a consistent staggered adjacency.
    for (uint32_t x = 4; x < 12; ++x) {
        for (uint32_t y = 4; y < 8; ++y) {
            for (int d = 0; d < 8; ++d) {
                TileXY n = Neighbour(128, 128, x, y, d);
                TileXY back = Neighbour(128, 128, n.x, n.y, (d + 4) % 8);
                OBL_CHECK_EQ(back.x, x);
                OBL_CHECK_EQ(back.y, y);
            }
        }
    }
}

OBL_TEST_CASE("grid_math.neighbour_wraps_with_32_bit_unsigned_arithmetic")
{
    // Power-of-two dimensions wrap like a torus.
    TileXY a = Neighbour(128, 128, 0, 0, 5); // even column, x - 2
    OBL_CHECK_EQ(a.x, 126u);
    OBL_CHECK_EQ(a.y, 0u);
    TileXY b = Neighbour(128, 128, 2, 0, 7); // y - 1
    OBL_CHECK_EQ(b.y, 127u);
    TileXY c = Neighbour(100, 77, 0, 0, 4);
    OBL_CHECK_EQ(c.x, 0xffffffffu % 100u);
    OBL_CHECK_EQ(c.y, 0u);
    // Directions outside 0..7 only reduce the tile.
    TileXY d = Neighbour(128, 128, 130, 131, 8);
    OBL_CHECK_EQ(d.x, 2u);
    OBL_CHECK_EQ(d.y, 3u);
    TileXY e = Neighbour(128, 128, 3, 4, -1);
    OBL_CHECK_EQ(e.x, 3u);
    OBL_CHECK_EQ(e.y, 4u);
}

OBL_TEST_CASE("grid_math.tile_to_world_point_kinds")
{
    // tile size 64: odd columns start at (32 x, 64 y), even columns are 32 higher.
    WorldPoint odd = TileToWorldPoint(64, 3, 2, 0);
    OBL_CHECK_EQ(odd.x, 96.0f);
    OBL_CHECK_EQ(odd.y, 128.0f);
    OBL_CHECK_EQ(odd.z, 0.0f);
    WorldPoint even = TileToWorldPoint(64, 4, 2, 0);
    OBL_CHECK_EQ(even.x, 128.0f);
    OBL_CHECK_EQ(even.y, 96.0f);

    WorldPoint k1 = TileToWorldPoint(64, 3, 2, 1);
    OBL_CHECK_EQ(k1.x, 96.0f);
    OBL_CHECK_EQ(k1.y, 191.0f);
    WorldPoint k2 = TileToWorldPoint(64, 3, 2, 2);
    OBL_CHECK_EQ(k2.x, 64.0f);
    OBL_CHECK_EQ(k2.y, 160.0f);
    WorldPoint k3 = TileToWorldPoint(64, 3, 2, 3);
    OBL_CHECK_EQ(k3.x, 128.0f);
    OBL_CHECK_EQ(k3.y, 160.0f);
    WorldPoint k4 = TileToWorldPoint(64, 3, 2, 4);
    OBL_CHECK_EQ(k4.x, 96.0f);
    OBL_CHECK_EQ(k4.y, 160.0f);
    // Kinds outside 1..4 answer the origin.
    for (int kind : {-1, 0, 5, 6, 1000}) {
        WorldPoint p = TileToWorldPoint(64, 3, 2, kind);
        OBL_CHECK_EQ(p.x, 96.0f);
        OBL_CHECK_EQ(p.y, 128.0f);
    }
}

OBL_TEST_CASE("grid_math.tile_to_world_point_odd_and_degenerate_sizes")
{
    // 63: half = 31 (rounds down), kind 1 adds 62.
    WorldPoint p = TileToWorldPoint(63, 5, 1, 1);
    OBL_CHECK_EQ(p.x, 155.0f);
    OBL_CHECK_EQ(p.y, 63.0f + 62.0f);
    WorldPoint q = TileToWorldPoint(-63, 2, 3, 0);
    OBL_CHECK_EQ(q.x, -62.0f); // half = -31, x = -31 * 2
    OBL_CHECK_EQ(q.y, -189.0f + 31.0f); // -63 * 3 - (-31), even column
    // Size 0 collapses everything to the origin.
    WorldPoint z = TileToWorldPoint(0, 9, 9, 4);
    OBL_CHECK_EQ(z.x, 0.0f);
    OBL_CHECK_EQ(z.y, 0.0f);
}

OBL_TEST_CASE("grid_math.tile_index_wraps_both_coordinates")
{
    OBL_CHECK_EQ(TileIndex(128, 128, 3, 2), 2u * 128u + 3u);
    OBL_CHECK_EQ(TileIndex(128, 128, 130, 129), 1u * 128u + 2u);
    OBL_CHECK_EQ(TileIndex(100, 77, 105, 80), 3u * 100u + 5u);
}

OBL_TEST_CASE("grid_math.adjacency_and_neighbour_indices_follow_neighbour")
{
    for (uint32_t x : {0u, 1u, 126u, 127u}) {
        for (uint32_t y : {0u, 5u, 127u}) {
            uint32_t indexes[8];
            NeighbourIndices(128, 128, x, y, indexes);
            for (int d = 0; d < 8; ++d) {
                const TileXY n = Neighbour(128, 128, x, y, d);
                OBL_CHECK_EQ(indexes[d], TileIndex(128, 128, n.x, n.y));
                OBL_CHECK(AreTilesAdjacent(128, 128, x, y, n.x, n.y));
            }
            OBL_CHECK(!AreTilesAdjacent(128, 128, x, y, x, y));
            // Four columns away is never a neighbour (reaches at most two).
            OBL_CHECK(!AreTilesAdjacent(128, 128, x, y, (x + 4) % 128, y));
        }
    }
    // `other` is compared as given: an unwrapped coordinate does not match.
    OBL_CHECK(!AreTilesAdjacent(128, 128, 127, 5, 128, 5));
}

OBL_TEST_CASE("grid_math.world_point_to_tile_inverts_tile_points")
{
    // Every inner point of a tile (centre, and the points a few units from the diamond edges) maps back to that tile.
    for (uint32_t x = 2; x < 20; ++x) {
        for (uint32_t y = 2; y < 20; ++y) {
            const WorldPoint centre = TileToWorldPoint(64, x, y, 4);
            for (float dx : {0.0f, 6.0f, -6.0f}) {
                for (float dy : {0.0f, 6.0f, -6.0f}) {
                    if (dx != 0.0f && dy != 0.0f && std::abs(dx) + std::abs(dy) > 8.0f) continue;
                    const TileXY t = WorldPointToTile(64, 128, 128, centre.x + dx, centre.y + dy);
                    OBL_CHECK_EQ(t.x, x);
                    OBL_CHECK_EQ(t.y, y);
                }
            }
        }
    }
}

OBL_TEST_CASE("grid_math.world_point_to_tile_edge_cases")
{
    const TileXY origin = WorldPointToTile(64, 128, 128, 0.0f, 0.0f);
    OBL_CHECK_EQ(origin.x, 0u);
    OBL_CHECK_EQ(origin.y, 0u);
    // Negative coordinates become huge unsigned values and wrap with the dimensions.
    const TileXY negative = WorldPointToTile(64, 128, 128, -1.0f, -1.0f);
    OBL_CHECK(negative.x < 128u);
    OBL_CHECK(negative.y < 128u);
    const TileXY degenerate = WorldPointToTile(1, 8, 8, 5.0f, 5.0f);
    OBL_CHECK_EQ(degenerate.x, 0u);
    OBL_CHECK_EQ(degenerate.y, 0u);
}

OBL_TEST_CASE("grid_math.world_center_is_rounded_once")
{
    const PathPoint c = TileToWorldCenter(64, 3, 2);
    OBL_CHECK_EQ(c.x, 96.0f);
    OBL_CHECK_EQ(c.y, 160.0f);
    const PathPoint e = TileToWorldCenter(64, 4, 2);
    OBL_CHECK_EQ(e.x, 128.0f);
    OBL_CHECK_EQ(e.y, 128.0f);
}

OBL_TEST_CASE("grid_math.footprint_crosses_map_boundary")
{
    const GridDims grid{64, 128, 128};
    OBL_CHECK(!FootprintCrossesMapBoundary(grid, 10, 10, 2, 2));
    // Three columns left of column 1 wrap around the edge: the corner ends on the far side.
    OBL_CHECK(FootprintCrossesMapBoundary(grid, 1, 5, 3, 1));
    // Empty footprints never wrap.
    OBL_CHECK(!FootprintCrossesMapBoundary(grid, 0, 0, 0, 0));
    OBL_CHECK(!FootprintCrossesMapBoundary(grid, 0, 0, -1, -1));
}

OBL_TEST_CASE("grid_math.orient_footprint_traversal")
{
    const GridDims grid{64, 128, 128};
    bool touched = false;
    TileXY origin{5, 7};
    TraversalDirections d = OrientFootprintTraversal(grid, grid, origin, 3, 3, 2, touched);
    OBL_CHECK(touched);
    OBL_CHECK_EQ(origin.x, 5u);
    OBL_CHECK_EQ(origin.y, 8u); // the row always moves down by one first
    OBL_CHECK_EQ(d.columnDirection, 6);
    OBL_CHECK_EQ(d.rowDirection, 0);

    // Rotation 0: two steps east (rows - 1) then one step west (columns - 1) from (4, 8): (6, 7), then (5, 6).
    origin = {4, 7};
    d = OrientFootprintTraversal(grid, grid, origin, 3, 2, 0, touched);
    OBL_CHECK_EQ(d.columnDirection, 2);
    OBL_CHECK_EQ(d.rowDirection, 4);
    OBL_CHECK_EQ(origin.x, 5u);
    OBL_CHECK_EQ(origin.y, 6u);

    // Rotation 3 walks direction 0 rows - 1 times.
    origin = {4, 7};
    d = OrientFootprintTraversal(grid, grid, origin, 2, 9, 3, touched);
    const TileXY expected = Neighbour(128, 128, 4, 8, 0);
    OBL_CHECK_EQ(origin.x, expected.x);
    OBL_CHECK_EQ(origin.y, expected.y);
    OBL_CHECK_EQ(d.columnDirection, 6);
    OBL_CHECK_EQ(d.rowDirection, 4);

    // A rotation above 3 only wraps the origin.
    origin = {200, 127};
    OrientFootprintTraversal(grid, grid, origin, 2, 2, 4, touched);
    OBL_CHECK(!touched);
    OBL_CHECK_EQ(origin.x, 72u);
    OBL_CHECK_EQ(origin.y, 0u);
}

OBL_TEST_CASE("grid_math.footprint_offset_point")
{
    const GridDims grid{64, 128, 128};
    PathPoint p{};
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 1, 1, 1.0f, 2.0f, 0, p));
    OBL_CHECK_EQ(p.x, 97.0f); // origin (96, 128), bottom corner +63, offsets (1, 2)
    OBL_CHECK_EQ(p.y, 193.0f);
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 1, 1, 1.0f, 2.0f, 1, p));
    OBL_CHECK_EQ(p.x, 130.0f); // (96 + 32 + offsetY, 128 + 32 - offsetX)
    OBL_CHECK_EQ(p.y, 159.0f);
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 1, 1, 1.0f, 2.0f, 2, p));
    OBL_CHECK_EQ(p.x, 95.0f);
    OBL_CHECK_EQ(p.y, 126.0f);
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 1, 1, 1.0f, 2.0f, 3, p));
    OBL_CHECK_EQ(p.x, 62.0f); // left point (64, 160): x - offsetY, y + offsetX
    OBL_CHECK_EQ(p.y, 161.0f);
    OBL_CHECK(!FootprintOffsetPoint(grid, 3, 2, 1, 1, 1.0f, 2.0f, 4, p));
}

OBL_TEST_CASE("grid_math.orient_footprint_traversal_rotation_one_walks_columns")
{
    const GridDims grid{64, 128, 128};
    bool touched = false;
    // Rotation 1 walks direction 6 columns - 1 times from the wrapped origin and answers directions (2, 0).
    TileXY origin{5, 7};
    const TraversalDirections d = OrientFootprintTraversal(grid, grid, origin, 2, 4, 1, touched);
    TileXY expected = {5, 8};
    for (int i = 0; i < 3; ++i) expected = Neighbour(128, 128, expected.x, expected.y, 6);
    OBL_CHECK(touched);
    OBL_CHECK_EQ(origin.x, expected.x);
    OBL_CHECK_EQ(origin.y, expected.y);
    OBL_CHECK_EQ(d.columnDirection, 2);
    OBL_CHECK_EQ(d.rowDirection, 0);

    // A single column does not move at all.
    origin = {5, 7};
    OrientFootprintTraversal(grid, grid, origin, 2, 1, 1, touched);
    OBL_CHECK_EQ(origin.x, 5u);
    OBL_CHECK_EQ(origin.y, 8u);
}

OBL_TEST_CASE("grid_math.footprint_offset_point_multi_tile_walks")
{
    const GridDims grid{64, 128, 128};
    PathPoint p{};
    const auto walk = [&](uint32_t x, uint32_t y, int steps, int direction) {
        TileXY t{x, y};
        for (int i = 0; i < steps; ++i) t = Neighbour(128, 128, t.x, t.y, direction);
        return t;
    };

    // Rotation 1: width - 1 steps east, then the tile centre turned by the offsets.
    TileXY t = walk(3, 2, 2, 0);
    WorldPoint o = TileToWorldPoint(64, t.x, t.y, 0);
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 3, 2, 1.0f, 2.0f, 1, p));
    OBL_CHECK_EQ(p.x, static_cast<float>(32.0 + o.x + 2.0));
    OBL_CHECK_EQ(p.y, static_cast<float>(32.0 + o.y - 1.0));

    // Rotation 2: height - 1 steps east, then width - 1 steps in direction 6, offsets subtracted from the corner.
    t = walk(3, 2, 1, 0);
    t = walk(t.x, t.y, 2, 6);
    o = TileToWorldPoint(64, t.x, t.y, 0);
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 3, 2, 1.0f, 2.0f, 2, p));
    OBL_CHECK_EQ(p.x, static_cast<float>(o.x - 1.0));
    OBL_CHECK_EQ(p.y, static_cast<float>(o.y - 2.0));

    // Rotation 3: height - 1 steps in direction 6, left point (kind 2) plus the offsets turned.
    t = walk(3, 2, 2, 6);
    o = TileToWorldPoint(64, t.x, t.y, 2);
    OBL_CHECK(FootprintOffsetPoint(grid, 3, 2, 1, 3, 1.0f, 2.0f, 3, p));
    OBL_CHECK_EQ(p.x, static_cast<float>(o.x - 2.0));
    OBL_CHECK_EQ(p.y, static_cast<float>(o.y + 1.0));
}

OBL_TEST_CASE("grid_math.placement_predicates")
{
    // Scenery: mask 1 needs bit 0 and no bit 28; other masks need a shared bit and no bit 4; all need a class above 0x20000000.
    OBL_CHECK(CanPlaceSceneryTile(0x40000001u, 1));
    OBL_CHECK(!CanPlaceSceneryTile(0x50000001u, 1));
    OBL_CHECK(!CanPlaceSceneryTile(0x00000001u, 1)); // class not above 0x20000000
    OBL_CHECK(CanPlaceSceneryTile(0x40000004u, 4));
    OBL_CHECK(!CanPlaceSceneryTile(0x40000014u, 4));
    OBL_CHECK(!CanPlaceSceneryTile(0x40000008u, 4));

    // Building edge: neither low bit in the mask -> bit 8 of the cell must be clear.
    OBL_CHECK(CanPlaceBuildingEdge(0x20000000u, 0));
    OBL_CHECK(!CanPlaceBuildingEdge(0x20000100u, 0));
    OBL_CHECK(!CanPlaceBuildingEdge(0x20000040u, 0)); // bit 6 blocks
    OBL_CHECK(CanPlaceBuildingEdge(0x20000001u, 1));
    OBL_CHECK(!CanPlaceBuildingEdge(0x20000002u, 1));
    OBL_CHECK(CanPlaceBuildingEdge(0x20000002u, 2));
    OBL_CHECK(CanPlaceBuildingEdge(0x20000002u, 3));
    OBL_CHECK(!CanPlaceBuildingEdge(0x20000004u, 3));
    OBL_CHECK(!CanPlaceBuildingEdge(0u, 3)); // neither a mask bit nor 0xe0000100
}
