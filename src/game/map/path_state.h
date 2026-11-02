#pragma once

#include "game/map/path_node.h"

#include <cstdint>
#include <vector>

namespace obl::map {
struct Grid; // grid_state.h
}

// The A* route search and the world's route builder (docs/architecture/map.md). The world owns one `Path` (made by
// path::CreatePath and kept in game::Context), and the path owns its `PathSearch`.
namespace obl::path {

struct PathSearch;
struct RouteCursor;

// Tile predicate the search holds and calls for every tile it considers (each route kind sets its own; a new search starts
// with path::IsTileAllowedByMode).
using TilePredicate = bool (*)(const PathSearch& search, uint32_t x, uint32_t y);

struct PathSearch {
    map::Grid* m_grid = nullptr;
    uint8_t m_shallowMode = 0; // set by the land routes from their `option`; read only by the default predicate
    uint8_t m_seaMode = 0; // the sea route sets it for the search; the default predicate answers sea tiles
    uint8_t m_pavementMode = 0; // the pavement route sets it; every other route clears it
    uint32_t m_search = 0; // number of the current search, incremented by every route search
    uint32_t m_startX = 0; // written by the route searches
    uint32_t m_startY = 0;
    float m_startWorld[3] = {}; // world point of the start tile (Grid::TileToWorldPoint, kind 4)
    float m_stopWorld[3] = {}; // world point of the stop tile
    // The head of the open list: its m_nextOpen is the first open node.
    Node m_openHead;
    Node* m_endTile = nullptr; // node the last successful search ended on (the start tile; the route follows m_parent)
    TilePredicate m_isTileAllowed = nullptr;
    std::vector<Node> m_nodes; // one per grid cell, same index; sized on the first search (path::PrepareNodes)
};

// The route builder: its only state is the search. It is not copied or moved (the open list points into the search).
// The builders return a cursor that holds a reference to the route (path_route.cpp); the land routes take the `option`
// byte that sets the shallow mode.
struct Path {
    PathSearch m_search;

    RouteCursor* CreateLandRoute(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found, uint32_t option);
    RouteCursor* CreateLandOrBlockedRoute(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found, uint32_t option);
    RouteCursor* CreateSeaRoute(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found);
    RouteCursor* CreatePavementRoute(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found);
    // The route type is 1 for land and 4 for land or blocked (the movement messages' tests).
    bool IsTileAllowed(uint32_t x, uint32_t y, int pathType) const;
};

} // namespace obl::path
