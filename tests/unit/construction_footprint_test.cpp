#include "unit_test.h"

#include "game/construction/construction_footprint.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace obl::construction;

namespace {

constexpr uint32_t kGridSize = 16;

// A small wrapping grid; the walk runs east along a row (direction 2) and moves one row south between rows (direction 4).
struct FootprintEnv {
    ClearBuilding building;
    std::vector<uint32_t> flags = std::vector<uint32_t>(kGridSize * kGridSize, 0);
    std::vector<uint16_t> depth = std::vector<uint16_t>(kGridSize * kGridSize, 0);
    std::vector<std::pair<uint32_t, uint32_t>> rebuilds;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> flagsSeenByRebuild;

    bool Building(int, ClearBuilding* out)
    {
        *out = building;
        return true;
    }
    void Orient(TileXY*, uint32_t, uint32_t, int* columnDirection, int* rowDirection, uint32_t)
    {
        *columnDirection = 2;
        *rowDirection = 4;
    }
    TileXY Neighbour(TileXY tile, int direction) const
    {
        if (direction == 2) return {tile.x + 1, tile.y};
        return {tile.x, tile.y + 1};
    }
    uint32_t Seen(uint32_t x, uint32_t y) { return flagsSeenByRebuild[{x, y}]; }
    uint32_t Index(TileXY tile) const { return (tile.y % kGridSize) * kGridSize + tile.x % kGridSize; }
    ClearCell Cell(TileXY tile) { return {&flags[Index(tile)], &depth[Index(tile)]}; }
    void RebuildTile(TileXY tile)
    {
        rebuilds.emplace_back(tile.x, tile.y);
        flagsSeenByRebuild[{tile.x, tile.y}] = flags[Index(tile)];
    }
};

struct Layer {
    std::vector<int32_t> cells = std::vector<int32_t>(25 * 25, 0);
};

struct ApplyEnv : FootprintEnv {
    Layer terrain, work, z, body;
    int plates[3] = {1, 3, 7};
    int draws = 0, value = 5;
    uint32_t plateCount = 3, rotation = 0;
    int width = 1, height = 1;
    std::vector<std::string> effects;
    std::vector<uint32_t> observed;
    bool Building(int, ApplyBuilding* out)
    {
        out->exists = true;
        out->rotation = rotation;
        out->gridWidth = width;
        out->gridHeight = height;
        out->terrainLayer = terrain.cells.data();
        out->workLayer = work.cells.data();
        out->depthLayer = z.cells.data();
        out->bodyLayer = body.cells.data();
        out->surfaces = plates;
        out->surfaceCount = plateCount;
        return true;
    }
    int Rand()
    {
        ++draws;
        return value;
    }
    void RebuildTile(TileXY tile, int removeNeighbours)
    {
        effects.push_back("rebuild " + std::to_string(removeNeighbours));
        observed.push_back(flags[Index(tile)]);
        // A delegated rebuild may change derived bits: preserve them in later writes.
        flags[Index(tile)] |= 0x120000;
    }
    void SetSurface(TileXY tile, int type)
    {
        effects.push_back("surface " + std::to_string(type));
        flags[Index(tile)] = (flags[Index(tile)] & ~kCellSurface) | (static_cast<uint32_t>(type) << 24);
    }
    void RemoveLitter(TileXY) { effects.push_back("litter"); }
};

} // namespace

OBL_TEST_CASE("construction.clear_footprint_restores_the_marker_from_the_terrain_bits")
{
    Layer layer;
    for (int i = 0; i < 9; ++i) layer.cells[(i / 3) * 25 + i % 3] = 1; // a 1x1 building: the (w+2) x (h+2) area is 3x3
    FootprintEnv env;
    env.building = {true, 4, 4, 0, 1, 1, layer.cells.data()};
    // Row 0: land, shallow, deep; the building body, the surround and the markers must go.
    env.flags[env.Index({4, 4})] = 0x1 | 0x10 | 0x80 | 0x800 | 0x0f000000 | 0x10000000;
    env.flags[env.Index({5, 4})] = 0x4 | 0x10 | 0x40000000;
    env.flags[env.Index({6, 4})] = 0x8 | 0x10 | 0x800;
    env.depth[env.Index({4, 4})] = 0x190;
    ClearFootprint(1, env);
    OBL_CHECK_EQ(env.flags[env.Index({4, 4})], 0x1u | 0x10000000u | 0x80000000u);
    OBL_CHECK_EQ(env.flags[env.Index({5, 4})], 0x4u | 0x40000000u);
    OBL_CHECK_EQ(env.flags[env.Index({6, 4})], 0x8u | 0x20000000u);
    OBL_CHECK_EQ(env.depth[env.Index({4, 4})], 0);
    // The rebuild sees the cell without the marker and without the body flags.
    OBL_CHECK_EQ(env.Seen(4, 4), 0x1u | 0x10000000u);
    OBL_CHECK_EQ(env.Seen(5, 4), 0x4u);
    OBL_CHECK_EQ(env.Seen(6, 4), 0x8u);
    OBL_CHECK_EQ(env.rebuilds.size(), 9u);
    OBL_CHECK((env.rebuilds[0] == std::pair<uint32_t, uint32_t>{4, 4}));
    OBL_CHECK((env.rebuilds[3] == std::pair<uint32_t, uint32_t>{4, 5}));
    OBL_CHECK((env.rebuilds[8] == std::pair<uint32_t, uint32_t>{6, 6}));
}

OBL_TEST_CASE("construction.clear_footprint_skips_unmasked_and_margin_cells")
{
    Layer layer;
    layer.cells[0] = 0; // no terrain mask: untouched, no rebuild
    layer.cells[1] = 0x200; // margin: untouched
    layer.cells[2] = 3;
    FootprintEnv env;
    env.building = {true, 0, 0, 0, 1, 1, layer.cells.data()};
    for (auto& f : env.flags) f = 0x1 | 0x10 | 0x80000000;
    for (auto& d : env.depth) d = 7;
    ClearFootprint(1, env);
    OBL_CHECK_EQ(env.flags[0], 0x1u | 0x10u | 0x80000000u);
    OBL_CHECK_EQ(env.flags[1], 0x1u | 0x10u | 0x80000000u);
    OBL_CHECK_EQ(env.depth[1], 7);
    OBL_CHECK_EQ(env.flags[2], 0x1u | 0x80000000u); // the marker is cleared and put back
    OBL_CHECK_EQ(env.depth[2], 0);
    OBL_CHECK_EQ(env.rebuilds.size(), 1u);
}

OBL_TEST_CASE("construction.clear_footprint_leaves_a_cell_without_terrain_bits_without_a_marker")
{
    Layer layer;
    layer.cells[0] = 1;
    FootprintEnv env;
    env.building = {true, 2, 2, 0, 1, 1, layer.cells.data()};
    env.flags[env.Index({2, 2})] = 0x10 | 0xc0000000u;
    ClearFootprint(1, env);
    OBL_CHECK_EQ(env.flags[env.Index({2, 2})], 0u);
}

OBL_TEST_CASE("construction.clear_footprint_reads_the_turned_area_by_column")
{
    Layer layer;
    layer.cells[1 * 25 + 0] = 1; // the turned area reads row r, column c at c*25 + r: this is row 0, column 1
    FootprintEnv env;
    env.building = {true, 2, 2, 1, 1, 2, layer.cells.data()}; // w=1, h=2 turned: rows = w + 2 = 3, columns = h + 2 = 4
    for (auto& f : env.flags) f = 0x1;
    ClearFootprint(1, env);
    // Only the (row 0, column 1) cell is masked, at tile (3, 2).
    OBL_CHECK_EQ(env.rebuilds.size(), 1u);
    OBL_CHECK((env.rebuilds[0] == std::pair<uint32_t, uint32_t>{3, 2}));
}

OBL_TEST_CASE("construction.clear_footprint_wraps_the_grid_and_ignores_missing_buildings")
{
    Layer layer;
    layer.cells[0] = 1;
    FootprintEnv env;
    env.building = {true, 15, 15, 0, 0, 0, layer.cells.data()};
    env.flags[env.Index({15, 15})] = 0x2 | 0x10;
    ClearFootprint(1, env);
    OBL_CHECK_EQ(env.flags[env.Index({15, 15})], 0x2u | 0x80000000u);

    FootprintEnv none;
    none.building = {false, 0, 0, 0, 1, 1, layer.cells.data()};
    for (auto& f : none.flags) f = 0x10;
    ClearFootprint(1, none);
    OBL_CHECK_EQ(none.rebuilds.size(), 0u);
    none.building = {true, 0, 0, 0, 1, 1, nullptr}; // no footprint
    ClearFootprint(1, none);
    OBL_CHECK_EQ(none.rebuilds.size(), 0u);
    OBL_CHECK_EQ(none.flags[0], 0x10u);
}

OBL_TEST_CASE("construction.apply_footprint_draws_once_in_every_phase_and_uses_caller_origin")
{
    for (int phase = 0; phase <= 1; ++phase)
        for (int mode = 2; mode <= 3; ++mode) {
            ApplyEnv env;
            env.terrain.cells[0] = 1;
            env.work.cells[0] = kCellBuilt;
            env.body.cells[0] = 1;
            env.flags[env.Index({15, 15})] = 1 | kCellBuilding;
            ApplyFootprint({15, 15}, phase, mode, 1, env);
            OBL_CHECK_EQ(env.draws, 1);
            OBL_CHECK_EQ(env.flags[0], 0u);
            const auto result = env.flags[env.Index({15, 15})];
            if (phase == 0)
                OBL_CHECK_EQ(result, 1u | kCellBuilding);
            else if (mode == 2)
                OBL_CHECK_EQ(result, 1u | kCellBuilding | kCellBuilt);
            else
                OBL_CHECK_EQ(result, 1u | kCellFreeLand);
        }
    ApplyEnv env;
    env.plateCount = 0;
    ApplyFootprint({0, 0}, 0, 3, 1, env);
    OBL_CHECK_EQ(env.draws, 0);
}

OBL_TEST_CASE("construction.apply_footprint_orders_surface_removal_litter_and_rebuilds")
{
    ApplyEnv env;
    env.body.cells[0] = kCellSurface;
    env.body.cells[1] = kCellSurface;
    env.flags[0] = 1 | 0x10000080 | 0x03000000;
    env.flags[1] = 1;
    ApplyFootprint({0, 0}, 0, 2, 1, env);
    OBL_CHECK_EQ(env.draws, 1);
    OBL_CHECK((env.effects ==
            std::vector<std::string>{
                    "rebuild 1", "rebuild 0", "litter", "surface 7", "rebuild 0", "rebuild 0", "litter", "surface 7", "rebuild 0"}));
    OBL_CHECK_EQ(env.observed[0], 1u | 0x03000000u);
    OBL_CHECK_EQ(env.observed[1], 1u | 0x120000u);
    OBL_CHECK_EQ(env.flags[0], 1u | 0x07000000u | 0x120000u);
    OBL_CHECK_EQ(env.flags[1], env.flags[0]);
}

OBL_TEST_CASE("construction.apply_footprint_tests_entry_terrain_and_preserves_depth_on_zero")
{
    ApplyEnv env;
    env.body.cells[0] = kCellSurface;
    env.work.cells[0] = kCellFreeLand | kCellFreeWater;
    env.terrain.cells[0] = 0x201;
    env.flags[0] = 2 | kCellFreeDeep;
    env.depth[0] = 99;
    env.z.cells[1] = 0x12345678;
    ApplyFootprint({0, 0}, 0, 2, 1, env);
    OBL_CHECK_EQ(env.flags[0], 2u | kCellBuilding | kCellFreeLand | kCellFreeWater | kCellAroundBuilding | 0x120000u);
    OBL_CHECK_EQ(env.depth[0], 99);
    OBL_CHECK_EQ(env.depth[1], 0x5678);
    OBL_CHECK((env.effects == std::vector<std::string>{"rebuild 0", "litter"}));
}

OBL_TEST_CASE("construction.apply_footprint_distinguishes_margin_masks_and_body_one")
{
    ApplyEnv env;
    env.terrain.cells[0] = 0x200;
    env.terrain.cells[1] = 0x08000001;
    env.terrain.cells[2] = 1;
    env.work.cells[2] = kCellFreeLand;
    env.body.cells[2] = 1;
    env.flags[0] = 1;
    env.flags[1] = kCellFreeLand | kCellFreeWater | kCellFreeDeep;
    env.flags[2] = 1 | kCellFreeWater | kCellFreeDeep;
    ApplyFootprint({0, 0}, 0, 2, 1, env);
    OBL_CHECK_EQ(env.flags[0], 1u | kCellFreeLand | 0x10000080u | 0x120000u);
    OBL_CHECK_EQ(env.flags[1], kCellFreeLand | kCellFreeWater | kCellFreeDeep | kCellBuilding);
    OBL_CHECK_EQ(env.flags[2], 1u | kCellBuilding | kCellAroundBuilding);
}

OBL_TEST_CASE("construction.apply_footprint_turns_non_square_layers_and_demolishes_land_only")
{
    ApplyEnv env;
    env.rotation = 1;
    env.width = 1;
    env.height = 2;
    env.body.cells[25] = 1;
    env.body.cells[50] = 1;
    env.work.cells[75] = kCellBuilt;
    env.flags[1] = kCellBuilding | 4;
    env.flags[2] = kCellBuilding | 2;
    ApplyFootprint({0, 0}, 1, 3, 1, env);
    OBL_CHECK_EQ(env.flags[1], 4u);
    OBL_CHECK_EQ(env.flags[2], 2u | kCellFreeLand);
    ApplyFootprint({0, 0}, 1, 2, 1, env);
    OBL_CHECK_EQ(env.flags[3], kCellBuilt);
}

OBL_TEST_CASE("construction.apply_footprint_litter_decision_uses_name_and_job_status")
{
    struct Object {
        bool litter;
        int status;
        int deaths = 0;
    } available{true, 4}, claimed{true, 5}, other{false, 1}, unrecognized{true, 0};
    struct Env {
        std::vector<Object*> objects;
        int queries = 0;
        auto Objects(TileXY) { return objects; }
        bool IsLitter(Object* object) { return object->litter; }
        int JobStatus(Object* object)
        {
            ++queries;
            return object->status;
        }
        void RemoveLitterObject(Object* object) { ++object->deaths; }
    } env{{&available, &claimed, &other, &unrecognized}};
    RemoveFootprintLitter({0, 0}, env);
    OBL_CHECK_EQ(env.queries, 3);
    OBL_CHECK_EQ(available.deaths, 1);
    OBL_CHECK_EQ(claimed.deaths, 0);
    OBL_CHECK_EQ(other.deaths, 0);
    OBL_CHECK_EQ(unrecognized.deaths, 1);
}

namespace {

// Directions of the game's Grid::GetNeighbour on a flat wrapping grid: 0 = +x, 2 = +y, 4 = -x, 6 = -y (any consistent assignment works for the
// walk's shape; the test reads the order the cells are touched in).
struct GridApplyEnv {
    BuildingToGrid building;
    std::vector<uint32_t> flags = std::vector<uint32_t>(kGridSize * kGridSize, 0);
    std::vector<uint16_t> depth = std::vector<uint16_t>(kGridSize * kGridSize, 0);
    std::vector<std::pair<uint32_t, uint32_t>> rebuilds;
    std::vector<std::string> calls;
    TileXY serveTile{9, 9};
    float top[3] = {};

    bool Building(int, BuildingToGrid* out)
    {
        *out = building;
        return true;
    }
    void ApplyFootprint(TileXY origin, int phase, int mode, int id)
    {
        calls.push_back("apply " + std::to_string(origin.x) + "," + std::to_string(origin.y) + "," + std::to_string(phase) + "," +
                std::to_string(mode) + "," + std::to_string(id));
    }
    TileXY Neighbour(TileXY tile, int direction) const
    {
        const uint32_t n = kGridSize;
        switch (direction) {
        case 0: return {(tile.x + 1) % n, tile.y};
        case 2: return {tile.x, (tile.y + 1) % n};
        case 4: return {(tile.x + n - 1) % n, tile.y};
        default: return {tile.x, (tile.y + n - 1) % n};
        }
    }
    uint32_t Index(TileXY tile) const { return (tile.y % kGridSize) * kGridSize + tile.x % kGridSize; }
    ClearCell Cell(TileXY tile) { return {&flags[Index(tile)], &depth[Index(tile)]}; }
    void RebuildTile(TileXY tile) { rebuilds.emplace_back(tile.x, tile.y); }
    void FootprintOffsetPoint(TileXY, uint32_t width, uint32_t height, float x, float y, uint32_t rotation, float* out)
    {
        calls.push_back("offset " + std::to_string(width) + "," + std::to_string(height) + "," + std::to_string(static_cast<int>(x)) + "," +
                std::to_string(static_cast<int>(y)) + "," + std::to_string(rotation));
        out[0] = 1.0f;
        out[1] = 2.0f;
    }
    TileXY WorldToTile(float, float) { return serveTile; }
    void TileToWorld(TileXY tile, float* out)
    {
        out[0] = static_cast<float>(tile.x);
        out[1] = static_cast<float>(tile.y);
        out[2] = 0.5f;
    }
    void SetTopWorldPosition(const float* point)
    {
        for (int i = 0; i < 3; ++i) top[i] = point[i];
    }
};

BuildingToGrid PlainBuilding(uint32_t width, uint32_t height, uint32_t rotation)
{
    BuildingToGrid b;
    b.exists = true;
    b.tileX = 4;
    b.tileY = 5;
    b.rotation = rotation;
    b.defWidth = width;
    b.defHeight = height;
    b.objectGridWidth = static_cast<uint8_t>(rotation & 1 ? height : width);
    b.objectGridHeight = static_cast<uint8_t>(rotation & 1 ? width : height);
    b.serveOffsetX = -3;
    b.serveOffsetY = 7;
    return b;
}

} // namespace

OBL_TEST_CASE("construction.apply_building_without_footprint_takes_rectangle_ring_and_serve_tile")
{
    GridApplyEnv env;
    env.building = PlainBuilding(2, 3, 0); // A = width = 2 rows (direction 6), B = height = 3 tiles per row (direction 0)
    for (auto& f : env.flags) f = 1 | kCellFreeLand; // land everywhere
    env.flags[env.Index({6, 4})] = 4; // shallow water: not part of the ring
    ApplyBuildingToGrid(7, env);
    // Rectangle: rows y = 5 and 4, tiles x = 4..6 in each.
    for (uint32_t y : {5u, 4u})
        for (uint32_t x : {4u, 5u, 6u}) {
            const uint32_t f = env.flags[env.Index({x, y})];
            if (x == 6 && y == 4) {
                OBL_CHECK_EQ(f, 4u | kCellBuilding);
                continue;
            } // no land bit: only 0x10 (and the free-land marker was cleared)
            OBL_CHECK_EQ(f, 1u | kCellBuilding | kCellAroundBuilding);
        }
    // Serve tile: free land + paving mark.
    OBL_CHECK_EQ(env.flags[env.Index({9, 9})], 1u | kCellFreeLand | kCellAroundBuilding);
    OBL_CHECK(env.calls.size() == 1 && env.calls[0] == "offset 2,3,-3,7,0");
    // Ring: start = neighbour 2 then 4 of (4,5) = (3,6); sides 6 x3, 0 x4, 2 x3, 4 x4 => 14 steps, every ring cell is land, so all are rebuilt.
    OBL_CHECK_EQ(env.rebuilds.size(), 14u);
    OBL_CHECK((env.rebuilds.front() == std::pair<uint32_t, uint32_t>{3, 5}));
    OBL_CHECK((env.rebuilds.back() == std::pair<uint32_t, uint32_t>{3, 6}));
    // Top position: (gridH - 1) = 2 steps in direction 0, (gridW - 1) = 1 step in direction 6 from (4,5).
    OBL_CHECK_EQ(env.top[0], 6.0f);
    OBL_CHECK_EQ(env.top[1], 4.0f);
}

OBL_TEST_CASE("construction.apply_building_odd_rotation_swaps_rectangle_and_ring")
{
    GridApplyEnv env;
    env.building = PlainBuilding(2, 3, 1); // A = height = 3 rows, B = width = 2 tiles per row
    for (auto& f : env.flags) f = 2; // water-only bit: no land bit, so nothing is paved and nothing is rebuilt
    ApplyBuildingToGrid(7, env);
    for (uint32_t y : {5u, 4u, 3u})
        for (uint32_t x : {4u, 5u}) OBL_CHECK_EQ(env.flags[env.Index({x, y})], 2u | kCellBuilding);
    OBL_CHECK_EQ(env.flags[env.Index({6, 5})], 2u);
    OBL_CHECK_EQ(env.flags[env.Index({4, 2})], 2u);
    OBL_CHECK(env.rebuilds.empty());
    OBL_CHECK_EQ(env.flags[env.Index({9, 9})], 2u | kCellFreeLand | kCellAroundBuilding);
}

OBL_TEST_CASE("construction.apply_building_with_footprint_delegates_and_skips_rectangle_and_ring")
{
    GridApplyEnv env;
    env.building = PlainBuilding(2, 3, 2);
    env.building.hasFootprint = true;
    for (auto& f : env.flags) f = 1;
    ApplyBuildingToGrid(7, env);
    OBL_CHECK(env.calls.size() == 2 && env.calls[0] == "apply 4,5,0,2,7" && env.calls[1] == "offset 2,3,-3,7,2");
    OBL_CHECK(env.rebuilds.empty());
    uint32_t touched = 0;
    for (uint32_t f : env.flags) touched += f != 1;
    OBL_CHECK_EQ(touched, 1u); // only the serve tile
    OBL_CHECK_EQ(env.flags[env.Index({9, 9})], 1u | kCellFreeLand | kCellAroundBuilding);
}

OBL_TEST_CASE("construction.apply_building_single_tile_object_does_not_move_the_top_point")
{
    GridApplyEnv env;
    env.building = PlainBuilding(1, 1, 0);
    env.building.hasFootprint = true;
    ApplyBuildingToGrid(7, env);
    OBL_CHECK_EQ(env.top[0], 4.0f);
    OBL_CHECK_EQ(env.top[1], 5.0f);
    env.building.objectGridWidth = 0;
    env.building.objectGridHeight = 0;
    ApplyBuildingToGrid(7, env);
    OBL_CHECK_EQ(env.top[0], 4.0f);
}
