#include "game/map/path_route.h"

#include "core/call_counter.h"
#include "game/game/game_accessors.h"
#include "game/map/grid_state.h"
#include "game/map/path_data.h"
#include "game/map/path_queries.h"
#include "game/map/path_search_support.h"
#include "game/map/path_state.h"

#include <cstdint>

namespace obl::path {
namespace {

using map::pathsearch::Allowed;

// Route searches run (`call-report map.path-route`).
test::CallCounter g_routeCalls("map.path-route");

TilePredicate Predicate(RouteKind kind)
{
    switch (kind) {
    case RouteKind::Land: break;
    case RouteKind::LandOrBlocked: return &IsLandOrBlockedTileAt;
    case RouteKind::Sea: return &IsSeaTileAt;
    case RouteKind::Pavement: return &IsPavementTileAt;
    }
    return &IsLandTileAt;
}

float GameTime()
{
    const auto* world = game::GetWorld();
    return world ? world->m_gameTime : 0.0f;
}

RouteCursor* Create(RouteKind kind, PathSearch& search, uint32_t option, uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found)
{
    g_routeCalls.Count();
    return CreateRoute(kind, sx, sy, tx, ty, found, GameTime(), [&](const auto& allocate, uint32_t** tiles) {
        return FindRoute(
                kind, search, Predicate(kind), static_cast<uint8_t>(option), sx, sy, tx, ty, tiles, Allowed(search),
                [&search] { ClearOpenList(search); }, allocate);
    }).release();
}

} // namespace

RouteCursor* Path::CreateLandRoute(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found, uint32_t option)
{
    return Create(RouteKind::Land, m_search, option, sx, sy, tx, ty, found);
}

RouteCursor* Path::CreateLandOrBlockedRoute(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found, uint32_t option)
{
    return Create(RouteKind::LandOrBlocked, m_search, option, sx, sy, tx, ty, found);
}

RouteCursor* Path::CreateSeaRoute(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found)
{
    return Create(RouteKind::Sea, m_search, 0, sx, sy, tx, ty, found);
}

RouteCursor* Path::CreatePavementRoute(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found)
{
    return Create(RouteKind::Pavement, m_search, 0, sx, sy, tx, ty, found);
}

// The search starts with its member defaults (no search run, no mode, an empty open list) and the default predicate that
// looks at the mode bytes; the node array is sized by the first search.
Path* CreatePath(map::Grid* grid)
{
    auto* path = new Path;
    path->m_search.m_grid = grid;
    path->m_search.m_isTileAllowed = &IsTileAllowedByMode;
    game::SetPath(path);
    return path;
}

void DestroyPath(Path* path)
{
    delete path;
    game::SetPath(nullptr);
}

bool IsCursorStraight(const RouteCursor& cursor)
{
    return IsStraight(cursor, game::GetGrid()->m_tileSize);
}

void ReleaseCursor(RouteCursor* cursor)
{
    delete cursor;
}

} // namespace obl::path
