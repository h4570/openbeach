
#include "unit_test.h"

#include "game/map/grid_state.h"
#include "game/map/path_state.h"
#include "game/map/path_tiles.h"

#include <cstdint>
#include <vector>

using namespace obl::path;

namespace {

// Whether any bit of the mask is set.
bool Any(uint32_t flags, uint32_t mask)
{
    return (flags & mask) != 0;
}

// Sea: one of the 0x60000004 bits is set and bit 0x10 is not.
bool ExpectedSea(uint32_t flags)
{
    if (!Any(flags, 0x60000004)) return false;
    if (Any(flags, 0x10)) return false;
    return true;
}

// Shallow water or land: any of the 0xc0000000 bits.
bool ExpectedShallowOrLand(uint32_t flags)
{
    return Any(flags, 0xc0000000);
}

// Pavement: 0x10000000 without 0x8000, 0x2 without 0x8010, or 0x80 without 0x8000.
bool ExpectedPavement(uint32_t flags)
{
    if (Any(flags, 0x10000000) && !Any(flags, 0x8000)) return true;
    if (Any(flags, 0x2) && !Any(flags, 0x8010)) return true;
    if (!Any(flags, 0x80)) return false;
    return !Any(flags, 0x8000);
}

// Land: 0x80000000 or 0x80, without 0x8000.
bool ExpectedLand(uint32_t flags)
{
    if (!Any(flags, 0x80000000) && !Any(flags, 0x80)) return false;
    return !Any(flags, 0x8000);
}

bool ExpectedLandOrBlocked(uint32_t flags)
{
    if (Any(flags, 0x80008000)) return true;
    return Any(flags, 0x80);
}

// The first mode that is set picks the rule; without one it is free land.
bool ExpectedByMode(bool shallowMode, bool seaMode, bool pavementMode, uint32_t flags)
{
    if (shallowMode) return Any(flags, 0xc0000000);
    if (seaMode) {
        if (!Any(flags, 0x60000000)) return false;
        return !Any(flags, 0x10);
    }
    if (pavementMode) return ExpectedPavement(flags);
    if (!Any(flags, 0x80000000)) return false;
    return !Any(flags, 0x8000);
}

constexpr uint32_t kBits[] = {
        kTileLand, kTileShallow, kTileDeep, kTilePavement, kTileBlocked, kTileNearBuilding, kTileOccupied, kTileBit4, kTileBit2};
constexpr uint32_t kRelevant = 0xf000'8000u | 0x80 | 0x10 | 0x4 | 0x2;

// Calls callable(flags) for each of the 512 combinations, each also with the noise masks below added to the unread bits.
template<class Callable> void ForEachFlags(Callable&& callable)
{
    const uint32_t noise[] = {0, ~kRelevant, 0x0aaa5500u & ~kRelevant, 0x00ff0f00u & ~kRelevant, 0x1 | 0x8 | 0x100 | 0x800};
    for (uint32_t i = 0; i < 512; ++i) {
        uint32_t flags = 0;
        for (int bit = 0; bit < 9; ++bit)
            if (i & (1u << bit)) flags |= kBits[bit];
        for (uint32_t n : noise) callable(flags | (n & ~kRelevant));
    }
}

} // namespace

OBL_TEST_CASE("path_tiles.predicates_match_the_expected_values")
{
    int checked = 0;
    ForEachFlags([&](uint32_t f) {
        OBL_CHECK_EQ(IsSeaTile(f), ExpectedSea(f));
        OBL_CHECK_EQ(IsShallowOrLandTile(f), ExpectedShallowOrLand(f));
        OBL_CHECK_EQ(IsPavementTile(f), ExpectedPavement(f));
        OBL_CHECK_EQ(IsLandTile(f), ExpectedLand(f));
        OBL_CHECK_EQ(IsLandOrBlockedTile(f), ExpectedLandOrBlocked(f));
        ++checked;
    });
    OBL_CHECK_EQ(checked, 512 * 5);
}

OBL_TEST_CASE("path_tiles.documented_cases")
{
    // Sea: water bits 0x20000000/0x40000000/0x4, never under a building body (0x10).
    OBL_CHECK(IsSeaTile(kTileDeep));
    OBL_CHECK(IsSeaTile(kTileShallow));
    OBL_CHECK(IsSeaTile(kTileBit4));
    OBL_CHECK(!IsSeaTile(kTileDeep | kTileOccupied));
    OBL_CHECK(!IsSeaTile(kTileLand));
    // Land needs 0x80000000 or 0x80, and no 0x8000; the "through scenery" predicate admits 0x8000 instead.
    OBL_CHECK(IsLandTile(kTileLand));
    OBL_CHECK(IsLandTile(kTileNearBuilding));
    OBL_CHECK(!IsLandTile(kTileLand | kTileBlocked));
    OBL_CHECK(!IsLandTile(kTileNearBuilding | kTileBlocked));
    OBL_CHECK(IsLandOrBlockedTile(kTileBlocked));
    OBL_CHECK(IsLandOrBlockedTile(kTileLand | kTileBlocked));
    OBL_CHECK(!IsLandOrBlockedTile(kTileShallow));
    // Pavement: full path tile or building edge unless blocked; or bit 2 unless blocked or occupied.
    OBL_CHECK(IsPavementTile(kTilePavement));
    OBL_CHECK(!IsPavementTile(kTilePavement | kTileBlocked));
    OBL_CHECK(IsPavementTile(kTileBit2));
    OBL_CHECK(!IsPavementTile(kTileBit2 | kTileOccupied));
    OBL_CHECK(!IsPavementTile(kTileBit2 | kTileBlocked));
    OBL_CHECK(IsPavementTile(kTileBit2 | kTileNearBuilding | kTileOccupied)); // third alternative rescues the occupied cell
    OBL_CHECK(!IsPavementTile(kTileBit2 | kTileNearBuilding | kTileBlocked));
    OBL_CHECK(IsPavementTile(kTilePavement | kTileBit2 | kTileOccupied) == true);
    OBL_CHECK(!IsPavementTile(0));
    // Shallow or land: either top water/land bit.
    OBL_CHECK(IsShallowOrLandTile(kTileLand));
    OBL_CHECK(IsShallowOrLandTile(kTileShallow));
    OBL_CHECK(!IsShallowOrLandTile(kTileDeep));
    OBL_CHECK(!IsShallowOrLandTile(kTileNearBuilding));
}

OBL_TEST_CASE("path_tiles.is_tile_allowed_dispatches_by_route_type")
{
    ForEachFlags([&](uint32_t f) {
        OBL_CHECK_EQ(IsTileAllowed(0, f), IsPavementTile(f));
        OBL_CHECK_EQ(IsTileAllowed(1, f), IsLandTile(f));
        OBL_CHECK_EQ(IsTileAllowed(2, f), IsShallowOrLandTile(f));
        OBL_CHECK_EQ(IsTileAllowed(3, f), IsSeaTile(f));
        OBL_CHECK_EQ(IsTileAllowed(4, f), IsLandOrBlockedTile(f));
        for (int type : {-1, 5, 6, 100, INT32_MIN, INT32_MAX}) OBL_CHECK(!IsTileAllowed(type, f));
    });
}

OBL_TEST_CASE("path_tiles.default_predicate_follows_the_mode_bytes")
{
    ForEachFlags([&](uint32_t f) {
        for (int mode = 0; mode < 8; ++mode) {
            bool shallowMode = mode & 1, seaMode = mode & 2, pavementMode = mode & 4;
            OBL_CHECK_EQ(IsTileByMode(shallowMode, seaMode, pavementMode, f), ExpectedByMode(shallowMode, seaMode, pavementMode, f));
        }
    });
    // Priority: shallow over sea over pavement; none = free land (without the 0x80 alternative of IsLandTile).
    OBL_CHECK(IsTileByMode(true, true, true, kTileShallow));
    OBL_CHECK(!IsTileByMode(false, true, true, kTileLand));
    OBL_CHECK(IsTileByMode(false, true, true, kTileDeep));
    OBL_CHECK(!IsTileByMode(false, true, true, kTileBit4)); // the default sea mode ignores bit 4 that IsSeaTile accepts
    OBL_CHECK(IsTileByMode(false, false, true, kTilePavement));
    OBL_CHECK(!IsTileByMode(false, false, false, kTileNearBuilding));
}

OBL_TEST_CASE("path_tiles.grid_wraps_coordinates_at_the_edges")
{
    obl::map::Grid grid{};
    grid.m_width = 6;
    grid.m_height = 4;
    grid.m_cells.resize(6 * 4);
    auto& cells = grid.m_cells;
    cells[(3 % 4) * 6 + 5 % 6].m_flags = kTileLand;
    // x=5, y=3 and the same tile shifted by whole grid widths/heights are the same cell (remainder by height, then width).
    OBL_CHECK_EQ(grid.Cell(5, 3).m_flags, kTileLand);
    OBL_CHECK_EQ(grid.Cell(5 + 6 * 7, 3 + 4 * 9).m_flags, kTileLand);
    OBL_CHECK(IsLandTile(grid.Cell(5 + 6, 3 + 4).m_flags));
    OBL_CHECK(!IsLandTile(grid.Cell(4, 3).m_flags));
}
