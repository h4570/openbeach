#pragma once

#include "game/map/grid_math.h"
#include "game/map/grid_state.h"
#include "game/map/path_data.h"
#include "game/map/path_search.h"
#include "game/map/path_state.h"

#include <cstdint>
#include <memory>

// Routes over the A* core (docs/architecture/map.md): the route search of each kind, the route and its cursor,
// and the endpoint helpers of the movement code. Memory and the tile predicate come in through callables so the same code
// runs in the game and on a synthetic grid in the unit tests.
namespace obl::path {

enum class RouteKind { Land, LandOrBlocked, Sea, Pavement };

// Route::m_pathType of each kind: land 1, through scenery 4, sea 3, pavement 0.
inline int RoutePathType(RouteKind kind)
{
    switch (kind) {
    case RouteKind::Land: return 1;
    case RouteKind::LandOrBlocked: return 4;
    case RouteKind::Sea: return 3;
    case RouteKind::Pavement: return 0;
    }
    return 0;
}

// Number of cells from the search's end tile up the parent chain that belong to the current search.
inline int RouteChainLength(const PathSearch& search)
{
    int count = 0;
    for (const Node* cell = search.m_endTile; cell && cell->m_search[0] == search.m_search; cell = cell->m_parent) ++count;
    return count;
}

// `option` is the shallow-mode byte of the land routes (the other kinds clear it).
// Returns the number of (x, y) pairs written to *out, 0 when there is no route. The search runs from the stop tile to the start
// tile, so the chain from m_endTile (the start) leads to the stop. Land, scenery and pavement routes leave out both ends of the chain
// (a route between adjacent tiles therefore has no interior tiles and counts as "not found"); the sea route keeps all of it.
// `*out` is only written once the array is allocated; the land variants leave it alone for an empty interior.
template<class Allowed, class Clear, class Allocate>
int FindRoute(RouteKind kind, PathSearch& search, TilePredicate predicate, uint8_t option, uint32_t startX, uint32_t startY, uint32_t stopX,
        uint32_t stopY, uint32_t** out, Allowed&& allowed, Clear&& clear, Allocate&& allocate)
{
    switch (kind) {
    case RouteKind::Land:
    case RouteKind::LandOrBlocked:
        search.m_shallowMode = option;
        search.m_pavementMode = 0;
        break;
    case RouteKind::Sea:
        search.m_seaMode = 1;
        search.m_shallowMode = 0;
        search.m_pavementMode = 0;
        break;
    case RouteKind::Pavement:
        search.m_pavementMode = 1;
        search.m_shallowMode = 0;
        break;
    }
    search.m_startX = startX;
    search.m_startY = startY;
    ++search.m_search;
    search.m_isTileAllowed = predicate;
    const bool startOk = allowed(startX, startY);
    const bool stopOk = allowed(stopX, stopY);
    const auto fail = [&] {
        if (kind == RouteKind::Sea) search.m_seaMode = 0;
        return 0;
    };
    if (!startOk || !stopOk || (startX == stopX && startY == stopY)) return fail();
    if (!Search(search, startX, startY, stopX, stopY, allowed)) return fail();

    const int chain = RouteChainLength(search);
    const bool wholeChain = kind == RouteKind::Sea;
    const int count = wholeChain ? chain : chain - 2;
    const uint32_t bytes = static_cast<uint32_t>(count) * 8u;
    // Land and scenery routes skip the allocation for count == 0 (not for a negative count, which cannot happen after a successful search);
    // pavement skips it for any count <= 0; the sea route always allocates.
    const bool allocateTiles = wholeChain || (kind == RouteKind::Pavement ? count > 0 : count != 0);
    uint32_t* tiles = nullptr;
    if (allocateTiles) {
        tiles = static_cast<uint32_t*>(allocate(bytes));
        *out = tiles;
    }
    if (tiles && count > 0) {
        const Node* cell = wholeChain ? search.m_endTile : (search.m_endTile ? search.m_endTile->m_parent : nullptr);
        int written = 0;
        if (wholeChain) {
            for (; cell && cell->m_search[0] == search.m_search && written < count; cell = cell->m_parent, ++written) {
                tiles[written * 2] = cell->m_tileX;
                tiles[written * 2 + 1] = cell->m_tileY;
            }
        } else if (cell && cell->m_parent) {
            // The cell whose parent is null is the stop tile; it is not part of the route.
            while (cell->m_search[0] == search.m_search && written < count) {
                tiles[written * 2] = cell->m_tileX;
                tiles[written * 2 + 1] = cell->m_tileY;
                ++written;
                cell = cell->m_parent;
                if (!cell || !cell->m_parent) break;
            }
        }
    }
    clear();
    if (kind == RouteKind::Sea) search.m_seaMode = 0;
    return count;
}

// Builds the route around the tiles `find` produced and a cursor at its first step. `find(allocate, uint32_t** tiles)`
// returns the pair count and takes the tile array from `allocate(bytes)`, which is the route's own step vector. A missing route clears
// *routeFound (success leaves the caller's value alone) and returns null.
template<class Find>
std::unique_ptr<RouteCursor> CreateRoute(
        RouteKind kind, uint32_t startX, uint32_t startY, uint32_t stopX, uint32_t stopY, bool* routeFound, float gameTime, Find&& find)
{
    auto route = std::make_shared<Route>();
    const auto allocate = [&route](uint32_t bytes) {
        route->m_steps.assign(bytes / 8u, {});
        return reinterpret_cast<uint32_t*>(route->m_steps.data());
    };
    uint32_t* tiles = nullptr;
    const int count = find(allocate, &tiles);
    if (count == 0 || !tiles) {
        *routeFound = false;
        return nullptr;
    }
    route->m_steps.resize(static_cast<uint32_t>(count));
    route->m_start[0] = static_cast<int>(startX);
    route->m_start[1] = static_cast<int>(startY);
    route->m_stop[0] = static_cast<int>(stopX);
    route->m_stop[1] = static_cast<int>(stopY);
    route->m_timeStamp = gameTime;
    route->m_referenceCount = 1;
    route->m_pathType = RoutePathType(kind);
    return std::make_unique<RouteCursor>(MakeCursor(std::move(route), false));
}

// Cell flags the movement code reads and sets (docs/architecture/map.md).
inline constexpr uint32_t kRouteBeachTile = 0x100;
inline constexpr uint32_t kRouteBlockedTile = 0x10;
inline constexpr uint32_t kRouteSceneryTile = 0x8000;
inline constexpr uint32_t kRouteTemporaryBits = 0xc0000080; // set on beach tiles around a route endpoint
// Clearing removes more than it sets: 0x2fffff7f also drops bit 28 (0x10000000, the pavement bit) of those tiles.
inline constexpr uint32_t kRouteClearMask = 0x2fffff7f;

inline bool IsFreeBeachTile(uint32_t flags)
{
    return (flags & kRouteBeachTile) && !(flags & kRouteBlockedTile);
}

// For each endpoint that is a free beach tile, the tile and its eight neighbours lose the temporary
// bits (again only the free beach ones). The two endpoints are handled in order against the live grid.
inline void ClearTemporaryRouteTiles(const map::Grid& grid, const uint32_t* start, const uint32_t* stop)
{
    const uint32_t points[2][2] = {{start[0], start[1]}, {stop[0], stop[1]}};
    for (const auto& point : points) {
        auto& cell = grid.Cell(point[0], point[1]);
        if (!IsFreeBeachTile(cell.m_flags)) continue;
        cell.m_flags &= kRouteClearMask;
        for (int direction = 0; direction < 8; ++direction) {
            const auto n = grid_math::Neighbour(grid.m_width, grid.m_height, point[0], point[1], direction);
            auto& neighbour = grid.Cell(n.x, n.y);
            if (IsFreeBeachTile(neighbour.m_flags)) neighbour.m_flags &= kRouteClearMask;
        }
    }
}

// One endpoint of PrepareRouteEndpoints. A tile that already matches `mask` (and is not scenery) is fine. A free beach tile gets
// the temporary bits, as do the free beach tiles around it, and is then re-tested. Any other tile moves to the first of its neighbours
// 0, 2, 4, 6 that matches; none leaves it where it is and fails.
inline bool PrepareRouteEndpoint(const map::Grid& grid, uint32_t& x, uint32_t& y, uint32_t mask)
{
    auto& cell = grid.Cell(x, y);
    if ((cell.m_flags & mask) && !(cell.m_flags & kRouteSceneryTile)) return true;
    if (IsFreeBeachTile(cell.m_flags)) {
        cell.m_flags |= kRouteTemporaryBits;
        for (int direction = 0; direction < 8; ++direction) {
            const auto n = grid_math::Neighbour(grid.m_width, grid.m_height, x, y, direction);
            auto& neighbour = grid.Cell(n.x, n.y);
            if (IsFreeBeachTile(neighbour.m_flags)) neighbour.m_flags |= kRouteTemporaryBits;
        }
        return (grid.Cell(x, y).m_flags & mask) != 0;
    }
    for (int direction = 0; direction < 8; direction += 2) {
        const auto n = grid_math::Neighbour(grid.m_width, grid.m_height, x, y, direction);
        const uint32_t flags = grid.Cell(n.x, n.y).m_flags;
        if ((flags & mask) && !(flags & kRouteSceneryTile)) {
            x = n.x;
            y = n.y;
            return true;
        }
    }
    return false;
}

// Both endpoints are processed (the temporary bits stay in the grid even when the call fails); the caller's
// coordinates change only when both succeed.
inline bool PrepareRouteEndpoints(const map::Grid& grid, uint32_t* start, uint32_t* stop, uint32_t mask)
{
    uint32_t sx = start[0], sy = start[1], tx = stop[0], ty = stop[1];
    const bool startOk = PrepareRouteEndpoint(grid, sx, sy, mask);
    const bool stopOk = PrepareRouteEndpoint(grid, tx, ty, mask);
    if (!startOk || !stopOk) return false;
    start[0] = sx;
    start[1] = sy;
    stop[0] = tx;
    stop[1] = ty;
    return true;
}

} // namespace obl::path
