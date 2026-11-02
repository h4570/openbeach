#pragma once

#include "game/game/game_accessors.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/map/path_state.h"

#include <cstdint>

// Helpers shared by the route searches (path_route.cpp): the tile predicate the search holds.
namespace obl::map::pathsearch {
using map::Grid;
using path::PathSearch;

inline auto Allowed(PathSearch& search)
{
    return [&search](uint32_t x, uint32_t y) { return search.m_isTileAllowed(search, x, y); };
}

} // namespace obl::map::pathsearch
