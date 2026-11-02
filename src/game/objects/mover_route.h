#pragma once

#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/map/grid_cell.h"

#include <cstdint>

// The moving object's BuildRoute (docs/architecture/objects.md): picks the pavement or land route between two tiles for the
// object and stores the new cursor in m_pathCursor. The caller's start/stop arrays (x, y) may be rewritten by the endpoint helper.
// Everything the route reaches outside the object goes through an `Env` (the game binds it in src/game/objects/mover_receive.cpp, the unit
// tests fake it):
//   uint32_t TileFlags(uint32_t x, uint32_t y)                         grid cell flags of the (wrapped) tile
//   bool Prepare(uint32_t* start, uint32_t* stop, uint32_t mask)       preparing route endpoint tiles
//   void Clear(uint32_t* start, uint32_t* stop, uint32_t mask)         clearing temporary route tiles
//   PathCursor* Pavement(sx, sy, tx, ty, bool* found)                  pavement route creation on g_Path
//   PathCursor* Land(sx, sy, tx, ty, bool* found, uint32_t option)     land route creation on g_Path (the option byte is always 0 here)
namespace obl::moving {

inline constexpr uint32_t kPavementMask = 0x10000082u; // route-type mask of a pavement walk (MT_PAVEMENT2*)
inline constexpr uint32_t kLandMask = 0x80000000u; // route-type mask of a land walk (MT_MOVE2*)
inline constexpr uint32_t kSceneryTile = 0x8000; // tile flag that makes a pavement endpoint unusable as it is

// Both end tiles must match the mask and differ; then a pavement route additionally needs both tiles free of scenery. Such a pair is routed
// straight away (pavement or land by the mask, a mask of any other kind builds nothing). Every other pair, unless the ends are the same tile,
// first goes through the endpoint helper (which may move the ends and tag the beach), is routed with the corrected ends when that worked, and
// then the temporary tile bits are removed again - also when the helper failed and when the mask is of no known kind. The direct route does
// not remove anything. A new cursor replaces m_pathCursor without releasing the old one (the callers release before); a route that was not
// built leaves it alone. `*found` is written by the route builders only.
template<class Env> void BuildRoute(object::Mover& object, uint32_t* start, uint32_t* stop, bool* found, uint32_t mask, Env& env)
{
    const uint32_t startFlags = env.TileFlags(start[0], start[1]);
    const uint32_t stopFlags = env.TileFlags(stop[0], stop[1]);
    const bool sameTile = start[0] == stop[0] && start[1] == stop[1];
    const auto route = [&] {
        if (mask == kPavementMask)
            object.m_pathCursor = env.Pavement(start[0], start[1], stop[0], stop[1], found);
        else if (mask == kLandMask)
            object.m_pathCursor = env.Land(start[0], start[1], stop[0], stop[1], found, 0);
    };

    if ((mask & startFlags) && (mask & stopFlags) && !sameTile) {
        if (mask != kPavementMask) {
            if (mask == kLandMask) route();
            return;
        }
        if (!((startFlags | stopFlags) & kSceneryTile)) {
            route();
            return;
        }
    }
    if (sameTile) return;
    if (env.Prepare(start, stop, mask)) route();
    env.Clear(start, stop, mask);
}

} // namespace obl::moving
