#pragma once

#include "game/map/path_state.h"
#include "game/map/path_tiles_state.h"
#include "game/ui/sprite.h"

#include <cstdint>

namespace obl::map {
struct Grid;
}

// The paths of the running game: the world's route builder and path tiles, made with `new` and kept in game::Context, the
// tile predicates of the route search and the cursor a moving object walks its route with. The pure logic is in
// path_route.h, path_search.h and path_tiles*.h.
namespace obl::path {

struct RouteCursor;

// The world's path tiles. Create sets them in game::Context; Destroy releases their sprites, deletes them and clears the
// pointer. The handles hold the game's SpriteInstance* (SpriteOf/HandleOf convert).
PathTiles* CreatePathTiles(const char* pavement, const char* beachEdge);
void DestroyPathTiles(PathTiles* tiles);
inline ui::SpriteInstance* SpriteOf(SpriteHandle sprite)
{
    return reinterpret_cast<ui::SpriteInstance*>(static_cast<uintptr_t>(sprite));
}
inline SpriteHandle HandleOf(ui::SpriteInstance* sprite)
{
    return static_cast<SpriteHandle>(reinterpret_cast<uintptr_t>(sprite));
}

// The world's route builder on `grid`. Create sets it in game::Context; Destroy deletes it and clears the pointer.
Path* CreatePath(map::Grid* grid);
void DestroyPath(Path* path);

// The tile predicates of the route search (path_predicates.cpp), with the signature of TilePredicate; they answer from the
// cell flags of the tile (x, y).
bool IsSeaTileAt(const PathSearch& search, uint32_t x, uint32_t y);
bool IsShallowOrLandTileAt(const PathSearch& search, uint32_t x, uint32_t y);
bool IsPavementTileAt(const PathSearch& search, uint32_t x, uint32_t y);
bool IsLandTileAt(const PathSearch& search, uint32_t x, uint32_t y);
bool IsLandOrBlockedTileAt(const PathSearch& search, uint32_t x, uint32_t y);
// The default predicate of a new search; the mode bytes pick the answer.
bool IsTileAllowedByMode(const PathSearch& search, uint32_t x, uint32_t y);

// The cursor of a moving object: the straight-line test on the world's tile size, and the release (the moving object owns
// the cursor; the route goes with its last cursor).
bool IsCursorStraight(const RouteCursor& cursor);
void ReleaseCursor(RouteCursor* cursor);

} // namespace obl::path
