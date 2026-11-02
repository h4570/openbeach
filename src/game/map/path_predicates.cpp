#include "core/call_counter.h"
#include "game/map/grid_state.h"
#include "game/map/path_queries.h"
#include "game/map/path_state.h"
#include "game/map/path_tiles.h"

#include <cstdint>

namespace obl::path {
namespace {

// Predicate answers (`call-report <name>`): the route search calls them through PathSearch::m_isTileAllowed, the movement
// messages call Path::IsTileAllowed.
test::CallCounter g_seaCalls("map.sea-tile");
test::CallCounter g_pavementCalls("map.pavement-tile");
test::CallCounter g_landCalls("map.land-tile");
test::CallCounter g_byModeCalls("map.tile-by-mode");
test::CallCounter g_allowedCalls("map.tile-allowed");

uint32_t FlagsAt(const PathSearch& search, uint32_t x, uint32_t y)
{
    return search.m_grid->Cell(x, y).m_flags;
}

} // namespace

bool IsSeaTileAt(const PathSearch& search, uint32_t x, uint32_t y)
{
    g_seaCalls.Count();
    return IsSeaTile(FlagsAt(search, x, y));
}

bool IsShallowOrLandTileAt(const PathSearch& search, uint32_t x, uint32_t y)
{
    return IsShallowOrLandTile(FlagsAt(search, x, y));
}

bool IsPavementTileAt(const PathSearch& search, uint32_t x, uint32_t y)
{
    g_pavementCalls.Count();
    return IsPavementTile(FlagsAt(search, x, y));
}

bool IsLandTileAt(const PathSearch& search, uint32_t x, uint32_t y)
{
    g_landCalls.Count();
    return IsLandTile(FlagsAt(search, x, y));
}

bool IsLandOrBlockedTileAt(const PathSearch& search, uint32_t x, uint32_t y)
{
    return IsLandOrBlockedTile(FlagsAt(search, x, y));
}

bool IsTileAllowedByMode(const PathSearch& search, uint32_t x, uint32_t y)
{
    g_byModeCalls.Count();
    return IsTileByMode(search.m_shallowMode != 0, search.m_seaMode != 0, search.m_pavementMode != 0, FlagsAt(search, x, y));
}

bool Path::IsTileAllowed(uint32_t x, uint32_t y, int pathType) const
{
    g_allowedCalls.Count();
    return path::IsTileAllowed(pathType, FlagsAt(m_search, x, y));
}

} // namespace obl::path
