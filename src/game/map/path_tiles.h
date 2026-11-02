#pragma once

#include <cstdint>

// Tile predicates of the route search (docs/architecture/map.md). Each one is a pure function of the cell flags; the
// tile coordinates only select the cell, wrapped modulo the grid dimensions like Grid::TileIndex.
namespace obl::path {

// Bits of GridCell::m_flags that the predicates look at (the other bits never change an answer). Meanings from
// docs/specs/construction.md; bits whose role is not known carry the number only.
constexpr uint32_t kTileLand = 0x80000000; // free land
constexpr uint32_t kTileShallow = 0x40000000; // shallow water
constexpr uint32_t kTileDeep = 0x20000000; // deep water
constexpr uint32_t kTilePavement = 0x10000000; // full path tile around a building
constexpr uint32_t kTileBlocked = 0x8000; // blocks placing, walking on land and on the pavement
constexpr uint32_t kTileNearBuilding = 0x80; // cell under or next to a building
constexpr uint32_t kTileOccupied = 0x10; // cell taken by a building body
constexpr uint32_t kTileBit4 = 0x4;
constexpr uint32_t kTileBit2 = 0x2;

// Route::m_pathType / the `pathType` argument of Path::IsTileAllowed.
enum class RouteType : int {
    Pavement = 0,
    Land = 1,
    ShallowOrLand = 2,
    Sea = 3,
    LandOrBlocked = 4,
};

// any of 0x60000004, and not occupied by a building.
constexpr bool IsSeaTile(uint32_t flags)
{
    return (flags & (kTileDeep | kTileShallow | kTileBit4)) != 0 && (flags & kTileOccupied) == 0;
}

constexpr bool IsShallowOrLandTile(uint32_t flags)
{
    return (flags & (kTileLand | kTileShallow)) != 0;
}

// three alternatives, evaluated in this order; the first one that matches answers true.
constexpr bool IsPavementTile(uint32_t flags)
{
    if ((flags & kTilePavement) != 0 && (flags & kTileBlocked) == 0) return true;
    if ((flags & kTileBit2) != 0 && (flags & (kTileBlocked | kTileOccupied)) == 0) return true;
    return (flags & kTileNearBuilding) != 0 && (flags & kTileBlocked) == 0;
}

constexpr bool IsLandTile(uint32_t flags)
{
    return ((flags & kTileLand) != 0 || (flags & kTileNearBuilding) != 0) && (flags & kTileBlocked) == 0;
}

// unlike the land predicate the blocked bit does not exclude, it admits (the "path through scenery" route).
constexpr bool IsLandOrBlockedTile(uint32_t flags)
{
    return (flags & (kTileLand | kTileBlocked)) != 0 || (flags & kTileNearBuilding) != 0;
}

// unknown types answer false.
constexpr bool IsTileAllowed(int pathType, uint32_t flags)
{
    switch (static_cast<RouteType>(pathType)) {
    case RouteType::Pavement: return IsPavementTile(flags);
    case RouteType::Land: return IsLandTile(flags);
    case RouteType::ShallowOrLand: return IsShallowOrLandTile(flags);
    case RouteType::Sea: return IsSeaTile(flags);
    case RouteType::LandOrBlocked: return IsLandOrBlockedTile(flags);
    }
    return false;
}

// the predicate of a new search. Every route search replaces it, so it only runs when something calls it between
// construction and the first search. The mode bytes in priority order: shallow, sea, pavement, otherwise land.
constexpr bool IsTileByMode(bool shallowMode, bool seaMode, bool pavementMode, uint32_t flags)
{
    if (shallowMode) return (flags & (kTileLand | kTileShallow)) != 0;
    if (seaMode) return (flags & (kTileDeep | kTileShallow)) != 0 && (flags & kTileOccupied) == 0;
    if (pavementMode) return IsPavementTile(flags);
    return (flags & kTileLand) != 0 && (flags & kTileBlocked) == 0;
}

} // namespace obl::path
