#include "game/ui/gui_modes_place.h"
#include "unit_test.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using obl::grid_math::TileXY;
using obl::gui_modes_place::Rotate;
namespace gmp = obl::gui_modes_place;
namespace gm = obl::grid_math;

OBL_TEST_CASE("gui_modes_place.wheel_wrap_sign_and_zero")
{
    for (bool building : {false, true}) {
        for (int count : {1, 2, 4, 8}) {
            OBL_CHECK_EQ(Rotate(count - 1, count, count, 120, building), 0);
            OBL_CHECK_EQ(Rotate(0, count, count, -120, building), count - 1);
            OBL_CHECK_EQ(Rotate(0, count, count, 0, building), count - 1);
            OBL_CHECK_EQ(Rotate(0, count, count, 1, building), count == 1 ? 0 : 1);
        }
    }
}

OBL_TEST_CASE("gui_modes_place.building_wraps_first_animation_and_clamps_current")
{
    OBL_CHECK_EQ(Rotate(1, 4, 2, 1, true), 1);
    OBL_CHECK_EQ(Rotate(0, 4, 2, -1, true), 1);
    OBL_CHECK_EQ(Rotate(1, 2, 4, 1, true), 0);
    OBL_CHECK_EQ(Rotate(1, 2, 4, 1, false), 2);
    OBL_CHECK_EQ(Rotate(0, 2, 4, -1, false), 3);
    OBL_CHECK_EQ(Rotate(-2, 4, 4, 1, true), -1);
    OBL_CHECK_EQ(Rotate(-2, 4, 4, -1, true), 3);
}

OBL_TEST_CASE("gui_modes_place.footprint_origin_halves_dimensions_and_swaps_for_odd_rotation")
{
    // width 4 / height 2 with an even rotation: height/2 = 1 step in direction 4, then width/2 = 2 steps in direction 2.
    TileXY expected = gm::Neighbour(128, 128, 11, 10, 4);
    for (int i = 0; i < 2; ++i) expected = gm::Neighbour(128, 128, expected.x, expected.y, 2);
    const TileXY even = gmp::FootprintOrigin(128, 128, {11, 10}, 4, 2, 0);
    OBL_CHECK_EQ(even.x, expected.x);
    OBL_CHECK_EQ(even.y, expected.y);

    // An odd rotation (bit 0, so also -1) swaps which dimension feeds which direction: two steps in 4, one in 2.
    TileXY swapped = gm::Neighbour(128, 128, 11, 10, 4);
    swapped = gm::Neighbour(128, 128, swapped.x, swapped.y, 4);
    swapped = gm::Neighbour(128, 128, swapped.x, swapped.y, 2);
    for (int direction : {1, 3, -1}) {
        const TileXY turned = gmp::FootprintOrigin(128, 128, {11, 10}, 4, 2, direction);
        OBL_CHECK_EQ(turned.x, swapped.x);
        OBL_CHECK_EQ(turned.y, swapped.y);
    }
}

OBL_TEST_CASE("gui_modes_place.footprint_origin_leaves_unmoved_tile_unwrapped")
{
    // The division truncates toward zero: 1/2 and a negative dimension move nothing, and the tile is returned as given.
    for (const auto& dimensions : {std::pair{1, 1}, std::pair{-4, -3}, std::pair{0, 0}}) {
        const TileXY same = gmp::FootprintOrigin(128, 128, {300, 500}, dimensions.first, dimensions.second, 0);
        OBL_CHECK_EQ(same.x, 300u);
        OBL_CHECK_EQ(same.y, 500u);
    }
    const TileXY moved = gmp::FootprintOrigin(128, 128, {300, 500}, 2, 0, 0); // one step in direction 2 wraps into the map
    OBL_CHECK(moved.x < 128u && moved.y < 128u);
}

namespace {
// What the walk asks of the grid, answered from a few switches. Row direction 6 and column direction 0 as for rotation 2.
struct FakeEnv {
    uint32_t flags = 0;
    bool edge = true;
    bool bin = false;
    std::vector<TileXY> visited;
    std::vector<std::pair<TileXY, uint16_t>> highlights;
    void Orient(TileXY*, uint32_t, uint32_t, int* column, int* row, int)
    {
        *column = 0;
        *row = 6;
    }
    TileXY Neighbour(TileXY tile, int direction) { return gm::Neighbour(128, 128, tile.x, tile.y, direction); }
    uint32_t CellFlags(TileXY tile)
    {
        visited.push_back(tile);
        return flags;
    }
    bool EdgeOk(TileXY) { return edge; }
    bool HasBin(TileXY) { return bin; }
    void Highlight(TileXY tile, uint16_t color) { highlights.emplace_back(tile, color); }
};

using Layer = int32_t[obl::tables::kFootprintCells][obl::tables::kFootprintCells];

gmp::BuildingPlacement Placement(const Layer& footprint, int width, int height)
{
    gmp::BuildingPlacement building;
    building.gridWidth = width;
    building.gridHeight = height;
    building.footprint = footprint;
    return building;
}
} // namespace

OBL_TEST_CASE("gui_modes_place.building_reads_the_cells_of_the_loaded_grid")
{
    // The (width + 2) x (height + 2) area of a real grid: its last cell is the layer's own [2][2].
    auto grid = std::make_unique<obl::tables::BuildingFootprint>();
    grid->m_width = grid->m_height = 1;
    grid->m_layers[0][2][2] = 1;
    gmp::BuildingPlacement building = Placement(grid->m_layers[0], 1, 1);
    FakeEnv env;
    OBL_CHECK(!gmp::CanPlaceBuilding(env, building, {5, 5})); // the cell asks for flag 1, the map has none
    grid->m_layers[0][2][2] = 0;
    OBL_CHECK(gmp::CanPlaceBuilding(env, building, {5, 5}));
}

OBL_TEST_CASE("gui_modes_place.building_walks_the_whole_area_and_paints_nonzero_entries")
{
    Layer footprint{};
    footprint[0][0] = 1;
    footprint[0][1] = 1;
    footprint[2][0] = 1;
    gmp::BuildingPlacement building = Placement(footprint, 1, 2); // area 3 columns x 4 rows
    building.validColor = 7;
    building.invalidColor = 9;
    building.preview = true;
    FakeEnv env;
    env.flags = 1;
    OBL_CHECK(gmp::CanPlaceBuilding(env, building, {10, 10}));
    OBL_CHECK_EQ(env.visited.size(), size_t{12});
    OBL_CHECK_EQ(env.highlights.size(), size_t{3});
    for (const auto& highlight : env.highlights) OBL_CHECK_EQ(highlight.second, uint16_t{7});

    // A blocked cell fails the call, yet the walk and the painting continue to the last cell.
    FakeEnv blocked;
    blocked.flags = 0x50;
    OBL_CHECK(!gmp::CanPlaceBuilding(blocked, building, {10, 10}));
    OBL_CHECK_EQ(blocked.visited.size(), size_t{12});
    OBL_CHECK_EQ(blocked.highlights.size(), size_t{3});
    for (const auto& highlight : blocked.highlights) OBL_CHECK_EQ(highlight.second, uint16_t{9});

    building.preview = false;
    FakeEnv quiet;
    quiet.flags = 1;
    OBL_CHECK(gmp::CanPlaceBuilding(quiet, building, {10, 10}));
    OBL_CHECK(quiet.highlights.empty());
}

OBL_TEST_CASE("gui_modes_place.building_cell_rules")
{
    Layer footprint{};
    const gmp::BuildingPlacement building = Placement(footprint, 1, 1);
    const auto fits = [&](uint32_t flags, bool edge = true, bool bin = false) {
        FakeEnv env;
        env.flags = flags;
        env.edge = edge;
        env.bin = bin;
        return gmp::CanPlaceBuilding(env, building, {10, 10});
    };
    footprint[0][0] = 0x2;
    OBL_CHECK(fits(0x2));
    OBL_CHECK(!fits(0x1)); // no common bit
    OBL_CHECK(!fits(0x8002)); // 0x8000 blocks
    OBL_CHECK(!fits(0x12)); // 0x10 belongs to the 0x50 mask
    OBL_CHECK(!fits(0x40 | 2)); // 0x40 as well
    OBL_CHECK(!fits(0x100)); // a 0x100 cell needs bit 4 in the entry
    footprint[0][0] = 0x4;
    OBL_CHECK(fits(0x100));
    OBL_CHECK(!fits(0x100, true, true)); // a Bin on the cell blocks every non-zero entry
    footprint[0][0] = 0x200; // the edge entry skips the flag tests
    OBL_CHECK(fits(0x50, true));
    OBL_CHECK(!fits(0x50, false));
    OBL_CHECK(!fits(0, true, true));
}

OBL_TEST_CASE("gui_modes_place.building_odd_rotation_reads_the_transposed_entry")
{
    Layer footprint{};
    footprint[0][1] = 1; // row 0, column 1 of an unturned walk; row 1, column 0 of a turned one
    gmp::BuildingPlacement building = Placement(footprint, 1, 3); // area 3 x 5; turned: 5 columns, 3 rows
    building.preview = true;

    FakeEnv even;
    OBL_CHECK(!gmp::CanPlaceBuilding(even, building, {10, 10})); // flags 0: the entry cannot fit
    OBL_REQUIRE(even.highlights.size() == 1);
    OBL_CHECK_EQ(even.highlights[0].first.x, even.visited[1].x);
    OBL_CHECK_EQ(even.highlights[0].first.y, even.visited[1].y);

    building.rotation = 3;
    FakeEnv turned;
    OBL_CHECK(!gmp::CanPlaceBuilding(turned, building, {10, 10}));
    OBL_CHECK_EQ(turned.visited.size(), size_t{15});
    OBL_REQUIRE(turned.highlights.size() == 1);
    OBL_CHECK_EQ(turned.highlights[0].first.x, turned.visited[5].x); // row 1 * 5 columns + column 0
    OBL_CHECK_EQ(turned.highlights[0].first.y, turned.visited[5].y);
}

namespace {
struct SceneryFake {
    bool crosses = false;
    std::vector<TileXY> blocked;
    std::vector<TileXY> occupied;
    std::vector<TileXY> visited;
    bool CrossesBoundary(TileXY, int, int) { return crosses; }
    TileXY Neighbour(TileXY tile, int direction) { return gm::Neighbour(128, 128, tile.x, tile.y, direction); }
    static bool Has(const std::vector<TileXY>& list, TileXY tile)
    {
        for (const auto& item : list)
            if (item.x == tile.x && item.y == tile.y) return true;
        return false;
    }
    bool SceneryTileOk(TileXY tile)
    {
        visited.push_back(tile);
        return !Has(blocked, tile);
    }
    bool OnlyLitter(TileXY tile) { return !Has(occupied, tile); }
};
} // namespace

OBL_TEST_CASE("gui_modes_place.scenery_walk_rotation_and_early_exit")
{
    SceneryFake env;
    OBL_CHECK(gmp::CanPlaceScenery(env, 3, 2, 0, {10, 10}));
    OBL_CHECK_EQ(env.visited.size(), size_t{6});

    // Even rotation: gridHeight cells per row (direction 0), gridWidth rows (direction 6); odd rotation: the other way round.
    SceneryFake wide;
    OBL_CHECK(gmp::CanPlaceScenery(wide, 3, 2, 0, {10, 10}));
    TileXY second = gm::Neighbour(128, 128, wide.visited[0].x, wide.visited[0].y, 0);
    OBL_CHECK_EQ(wide.visited[1].x, second.x);
    OBL_CHECK_EQ(wide.visited[1].y, second.y);
    const TileXY nextRow = gm::Neighbour(128, 128, 10, 10, 6);
    OBL_CHECK_EQ(wide.visited[2].x, nextRow.x); // the third tile starts the second row: two cells per row
    OBL_CHECK_EQ(wide.visited[2].y, nextRow.y);
    SceneryFake turned;
    OBL_CHECK(gmp::CanPlaceScenery(turned, 3, 2, 1, {10, 10}));
    OBL_CHECK_EQ(turned.visited.size(), size_t{6});
    OBL_CHECK_EQ(turned.visited[3].x, nextRow.x); // three cells per row
    OBL_CHECK_EQ(turned.visited[3].y, nextRow.y);

    env = {};
    env.crosses = true;
    OBL_CHECK(!gmp::CanPlaceScenery(env, 3, 2, 0, {10, 10}));
    OBL_CHECK(env.visited.empty());

    env = {};
    env.blocked.push_back({10, 10});
    OBL_CHECK(!gmp::CanPlaceScenery(env, 3, 2, 0, {10, 10}));
    OBL_CHECK_EQ(env.visited.size(), size_t{1}); // stops at the first failing tile

    env = {};
    env.occupied.push_back({10, 10});
    OBL_CHECK(!gmp::CanPlaceScenery(env, 1, 1, 0, {10, 10}));
}

OBL_TEST_CASE("gui_modes_place.place_tile_money_signed_cash_unsigned_expenses")
{
    OBL_CHECK_EQ(gmp::CashAfterPlacing(100.0f, 25), 75.0f);
    OBL_CHECK_EQ(gmp::ExpensesAfterPlacing(10.0f, 25), 35.0f);
    // A negative price adds to the cash but is a huge unsigned number for the expenses.
    OBL_CHECK_EQ(gmp::CashAfterPlacing(100.0f, -5), 105.0f);
    OBL_CHECK_EQ(gmp::ExpensesAfterPlacing(0.0f, -5), 4294967291.0f);
    // The exact double sum is rounded once, to float.
    OBL_CHECK_EQ(gmp::CashAfterPlacing(33554432.0f, 1), 33554432.0f); // 2^25 - 1 is not a float: ties to even
}

OBL_TEST_CASE("gui_modes_place.cursor_is_divided_by_the_zoom_scale_and_truncated")
{
    OBL_CHECK_EQ(gmp::ScaledCursor(400, 1.0f), 400);
    OBL_CHECK_EQ(gmp::ScaledCursor(400, 2.0f), 200);
    OBL_CHECK_EQ(gmp::ScaledCursor(401, 2.0f), 200); // truncated toward zero
    OBL_CHECK_EQ(gmp::ScaledCursor(-401, 2.0f), -200);
    OBL_CHECK_EQ(gmp::ScaledCursor(300, 0.5f), 600);
    OBL_CHECK_EQ(gmp::ScaledCursor(300, 0.0f),
            0); // the float-to-int conversion of an infinity is the integer indefinite, whose low 32 bits are 0
}

OBL_TEST_CASE("gui_modes_place.grass_and_pavement_tile_rules")
{
    constexpr uint32_t land = 1, bit31 = 0x80000000u, bit6 = 0x40, grass = 0x7000, paving = 0x10000000u;
    OBL_CHECK(gmp::GrassTileUsable(land | bit31));
    OBL_CHECK(gmp::GrassTileUsable(bit6)); // 0x40 alone is enough
    OBL_CHECK(gmp::GrassTileUsable(land | bit6));
    OBL_CHECK(!gmp::GrassTileUsable(land)); // land without 0x80000000 or 0x40
    OBL_CHECK(!gmp::GrassTileUsable(bit31));
    OBL_CHECK(!gmp::GrassTileUsable(land | bit31 | grass));
    OBL_CHECK(!gmp::GrassTileUsable(land | bit31 | 0x1000)); // any one of the grass bits
    OBL_CHECK(!gmp::GrassTileUsable(land | bit31 | paving));
    OBL_CHECK(!gmp::GrassTileUsable(bit6 | paving));
    OBL_CHECK(gmp::PavementTileUsable(land | bit31));
    OBL_CHECK(!gmp::PavementTileUsable(bit6)); // 0x40 does not help a path
    OBL_CHECK(!gmp::PavementTileUsable(land | bit31 | paving));
    OBL_CHECK(gmp::PavementTileUsable(land | bit31 | grass)); // grass does not block paving
    OBL_CHECK(!gmp::PavementTileUsable(bit31));
}

OBL_TEST_CASE("gui_modes_place.cash_rules_tiles_may_spend_everything_buildings_may_not")
{
    OBL_CHECK(gmp::CashCoversTile(100.0f, 100));
    OBL_CHECK(!gmp::CashCoversBuilding(100.0f, 100));
    OBL_CHECK(gmp::CashCoversBuilding(100.0f, 99));
    OBL_CHECK(!gmp::CashCoversTile(99.0f, 100));
    OBL_CHECK(gmp::CashCoversTile(0.0f, 0));
    OBL_CHECK(gmp::CashCoversTile(5.0f, -5)); // a negative price is paid out of the sign
    OBL_CHECK(!gmp::CashCoversTile(-0.5f, 0));
    OBL_CHECK(!gmp::CashCoversTile(std::nanf(""), 0));
    OBL_CHECK(!gmp::CashCoversBuilding(std::nanf(""), 0));
    // The comparison is made before the float rounding of the difference: 2^25 - 1 is not a float, but it is positive.
    OBL_CHECK(gmp::CashCoversBuilding(33554432.0f, 1));
}

OBL_TEST_CASE("gui_modes_place.cursor_shape_checks_the_panel_last")
{
    int queries = 0;
    const auto over = [&](bool answer) {
        return [&queries, answer] {
            ++queries;
            return answer;
        };
    };
    OBL_CHECK_EQ(gmp::PlaceCursorShape(true, false, over(false)), gmp::kCursorNormal);
    OBL_CHECK_EQ(gmp::PlaceCursorShape(false, true, over(false)), gmp::kCursorNormal);
    OBL_CHECK_EQ(queries, 0);
    OBL_CHECK_EQ(gmp::PlaceCursorShape(false, false, over(true)), gmp::kCursorNormal);
    OBL_CHECK_EQ(queries, 1);
    OBL_CHECK_EQ(gmp::PlaceCursorShape(false, false, over(false)), gmp::kCursorRefused);
    OBL_CHECK_EQ(queries, 2);
}

OBL_TEST_CASE("gui_modes_place.building_command_is_a_world_create_message")
{
    const auto message = gmp::BuildingCommandMessage();
    OBL_CHECK_EQ(message.sender, -2);
    OBL_CHECK_EQ(message.receiver, -2);
    OBL_CHECK_EQ(message.type, 0);
    OBL_CHECK_EQ(message.data, 2);
    const auto placement = gmp::BuildingCommandPlacement("SHOP", {12, 34}, 3);
    OBL_CHECK(std::strcmp(placement.m_name, "SHOP") == 0);
    OBL_CHECK_EQ(placement.m_x, 12);
    OBL_CHECK_EQ(placement.m_y, 34);
    OBL_CHECK_EQ(placement.m_layer, 3);
    for (float price : placement.m_prices) OBL_CHECK_EQ(price, -1.0f);
    OBL_CHECK_EQ(placement.m_beer, -1);
    OBL_CHECK_EQ(placement.m_beerPrice, 0.5f);
    OBL_CHECK_EQ(placement.m_age, 0);
    OBL_CHECK_EQ(placement.m_broken, 0);
    const std::string longKey(40, 'k');
    const auto longPlacement = gmp::BuildingCommandPlacement(longKey.c_str(), {0, 0}, 0);
    OBL_CHECK_EQ(longPlacement.m_sprite[7], 'k');
    OBL_CHECK_EQ(longPlacement.m_sprite[8], 0);
}

OBL_TEST_CASE("gui_modes_place.scenery_command_is_a_world_create_message_of_class_one")
{
    const auto message = gmp::SceneryCommandMessage();
    OBL_CHECK_EQ(message.sender, -2);
    OBL_CHECK_EQ(message.receiver, -2);
    OBL_CHECK_EQ(message.type, 0);
    OBL_CHECK_EQ(message.data, 1);
    const auto placement = gmp::SceneryCommandPlacement("BENCH", "Park bench", {12, 34}, 3);
    OBL_CHECK(std::strcmp(placement.m_name, "BENCH") == 0);
    OBL_CHECK(std::strcmp(placement.m_sprite, "Park bench") == 0);
    OBL_CHECK_EQ(placement.m_x, 12);
    OBL_CHECK_EQ(placement.m_y, 34);
    OBL_CHECK_EQ(placement.m_layer, 1 | (3 << 8));
    int32_t marker;
    std::memcpy(&marker, &placement.m_prices[0], sizeof(marker));
    OBL_CHECK_EQ(marker, -2);
    OBL_CHECK_EQ(placement.m_beer, 0);
    // The direction is a byte: a negative one keeps only its low byte, the upper two bytes of the layer stay zero.
    OBL_CHECK_EQ(gmp::SceneryCommandPlacement("A", "B", {0, 0}, -1).m_layer, 1 | (0xff << 8));
    // A name or key over 31 characters is truncated and stays terminated; the neighbouring fields are untouched.
    const std::string longName(40, 'n');
    const auto clipped = gmp::SceneryCommandPlacement("A", longName.c_str(), {12, 34}, 0);
    OBL_CHECK_EQ(clipped.m_sprite[30], 'n');
    OBL_CHECK_EQ(clipped.m_sprite[31], 0);
    OBL_CHECK_EQ(clipped.m_x, 12);
    OBL_CHECK_EQ(clipped.m_y, 34);
    OBL_CHECK_EQ(clipped.m_layer, 1);
    const std::string longKey(40, 'k');
    const auto keyed = gmp::SceneryCommandPlacement(longKey.c_str(), "B", {12, 34}, 0);
    OBL_CHECK_EQ(keyed.m_name[31], 0);
    OBL_CHECK(std::strcmp(keyed.m_sprite, "B") == 0);
    OBL_CHECK_EQ(keyed.m_x, 12);
}

namespace {
bool SameTile(TileXY a, TileXY b)
{
    return a.x == b.x && a.y == b.y;
}
// Records what OutlineScenery asks for; the neighbour of a tile is a plain step on an unbounded grid (direction 0: +x, 6: +y).
struct OutlineRecorder {
    mutable std::vector<std::pair<TileXY, uint16_t>> outlined;
    TileXY Neighbour(TileXY tile, int direction) const
    {
        OBL_CHECK(direction == 0 || direction == 6);
        return direction == 0 ? TileXY{tile.x + 1, tile.y} : TileXY{tile.x, tile.y + 1};
    }
    void Outline(TileXY tile, uint16_t color) const { outlined.push_back({tile, color}); }
};
} // namespace

OBL_TEST_CASE("gui_modes_place.building_preview_tiles_swap_for_odd_rotation")
{
    for (int rotation : {-3, -1, 1, 3, 5}) {
        const auto tiles = gmp::BuildingPreviewTiles(3, 5, rotation);
        OBL_CHECK_EQ(tiles.first, 5);
        OBL_CHECK_EQ(tiles.second, 3);
    }
    for (int rotation : {-2, 0, 2, 4}) {
        const auto tiles = gmp::BuildingPreviewTiles(3, 5, rotation);
        OBL_CHECK_EQ(tiles.first, 3);
        OBL_CHECK_EQ(tiles.second, 5);
    }
}

OBL_TEST_CASE("gui_modes_place.scenery_outline_walks_width_rows_of_height_cells_when_even")
{
    OutlineRecorder env;
    gmp::OutlineScenery(env, 2, 3, 0, {10, 20}, 0x07e0);
    // gridWidth = 2 rows in direction 6, each with gridHeight = 3 cells in direction 0.
    const std::vector<TileXY> expected{{10, 20}, {11, 20}, {12, 20}, {10, 21}, {11, 21}, {12, 21}};
    OBL_CHECK_EQ(env.outlined.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < env.outlined.size(); ++i) {
        OBL_CHECK(SameTile(env.outlined[i].first, expected[i]));
        OBL_CHECK_EQ(env.outlined[i].second, 0x07e0);
    }
    OutlineRecorder turned;
    gmp::OutlineScenery(turned, 2, 3, 1, {10, 20}, 0xf800);
    // An odd rotation swaps them: 3 rows of 2 cells.
    OBL_CHECK_EQ(turned.outlined.size(), 6u);
    OBL_CHECK(SameTile(turned.outlined[2].first, {10, 21}));
    OBL_CHECK(SameTile(turned.outlined[5].first, {11, 22}));
}

OBL_TEST_CASE("gui_modes_place.scenery_outline_with_a_non_positive_dimension_draws_nothing")
{
    for (auto [width, height] : {std::pair{0, 3}, std::pair{3, 0}, std::pair{-1, 3}, std::pair{3, -2}}) {
        OutlineRecorder env;
        gmp::OutlineScenery(env, width, height, 0, {1, 1}, 1);
        OBL_CHECK(env.outlined.empty());
    }
}

OBL_TEST_CASE("gui_modes_place.building_rejects_unsupported_rotations_before_traversal")
{
    const Layer footprint{};
    auto building = Placement(footprint, 1, 2);
    for (int rotation : {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8}) {
        FakeEnv env;
        building.rotation = rotation;
        const bool supported = rotation >= 0 && rotation <= 3;
        OBL_CHECK_EQ(gmp::CanPlaceBuilding(env, building, {10, 10}), supported);
        OBL_CHECK_EQ(env.visited.empty(), !supported);
    }
}
