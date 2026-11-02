#pragma once

#include "core/float_to_int.h"
#include "game/map/grid_math.h"
#include "game/map/grid_state.h"
#include "game/objects/guest_walk_targets.h"
#include "game/map/grid_cell.h"
#include "script_message.h"

#include <cstdint>
#include <optional>

// The pure rules of the four guest functions that pick a walking goal by looking along the tile grid (guest walking, docs/architecture/objects.md):
// StartPathWalk, SurfToShore, WindSurfStraight and GotoDancePoint. The game
// side (the guest, the messages, the sprite) is in src/game/objects/guest_walk_routes.cpp. Tiles are read from the game's own grid (map::Grid) and stepped with
// grid_math::Neighbour, like src/game/objects/guest_beach_points.h; the draws come from a callable that is the game's rand().
//
// 53-bit precision: a draw scaled by float constants is one double operation per step, a float store one float
// rounding, and the truncation (src/core/float_to_int.h) applies to what is left.
namespace obl::guest_walk {

using grid_math::TileXY;

// ---- StartPathWalk -------------------------------------------------------------------------------------------------------------------

constexpr uint32_t kGetonTileMask = 0x10000080; // pavement around a building, or the cell under or next to one
constexpr uint32_t kGetonLandBit = 0x2; // ... or this bit when neither of the two below is set
constexpr uint32_t kGetonLandBlockers = 0x8010; // blocked, or taken by a building body
constexpr int kGetonMaxRounds = 512; // a ray of the four wraps around the grid in at most 256 steps; further rounds repeat tiles

// The tile the guest can walk off the beach to: (flags & 0x10000080) or (flags & 2 and not flags & 0x8010). Unlike IsPavementTile the blocked bit does
// not exclude the first alternative.
inline bool IsGetonTile(uint32_t flags)
{
    return (flags & kGetonTileMask) != 0 || ((flags & kGetonLandBit) != 0 && (flags & kGetonLandBlockers) == 0);
}

// Four rays from the guest's tile, in directions 0, 2, 4 and 6; every round steps each ray once (the neighbouring tile, ray i in direction 2 * i) and the
// first tile of the round that passes IsGetonTile wins. Walking never ends if no ray meets such a tile; `maxRounds` makes that a null
// result for the caller (an unbounded walk would never end).
inline std::optional<TileXY> FindGetonTile(const map::Grid& grid, TileXY start, int maxRounds = kGetonMaxRounds)
{
    TileXY ray[4] = {start, start, start, start};
    for (int round = 0; round < maxRounds; ++round)
        for (int i = 0; i < 4; ++i) {
            ray[i] = grid_math::Neighbour(grid.m_width, grid.m_height, ray[i].x, ray[i].y, 2 * i);
            if (IsGetonTile(grid.Cell(ray[i].x, ray[i].y).m_flags)) return ray[i];
        }
    return std::nullopt;
}

// ---- SurfToShore -----------------------------------------------------------------------------------------------------------------

constexpr uint32_t kSurfLastColumn =
        0x7f; // a ray standing in column 0 or 127, or on row 0, is finished (the grid is 128 wide: a constant of the code)
constexpr uint32_t kSurfStopFlags = 0x8030; // blocked or taken: the ray stops after this tile
constexpr uint32_t kSurfBeachEdge = 0x100; // the beach edge: a message goes out for the tile the ray stood on before this one

// Eight rays from the guest's tile, ray i in direction i. Each round steps every unfinished ray that is not on the map edge; a ray whose new tile is blocked
// or taken is finished, and one whose new tile is a beach edge calls `emit(tile it stood on)`. It then sets `i = 4`, which does not leave the
// loop (a bug kept on purpose): the loop runs to 8, so it carries on with ray 5 (even when it was ray 5, 6 or 7 that found the edge: that one steps again). The search ends after the
// first round that sent a message, or when no ray was left to step; true tells that messages were sent, false that none was (then the guest swims to the
// nearest sea point).
template<class Emit> bool SurfToBeachRays(const map::Grid& grid, TileXY start, Emit&& emit)
{
    TileXY ray[8];
    bool finished[8] = {};
    for (TileXY& tile : ray) tile = start;
    bool sent = false;
    for (;;) {
        bool stepped = false;
        for (int i = 0; i < 8; ++i) {
            if (finished[i]) continue;
            const TileXY at = ray[i];
            if (at.x == 0 || at.x == kSurfLastColumn || at.y == 0) {
                finished[i] = true;
                continue;
            }
            stepped = true;
            ray[i] = grid_math::Neighbour(grid.m_width, grid.m_height, at.x, at.y, i);
            const uint32_t flags = grid.Cell(ray[i].x, ray[i].y).m_flags;
            if ((flags & kSurfStopFlags) != 0) finished[i] = true;
            if ((flags & kSurfBeachEdge) != 0) {
                sent = true;
                emit(at);
                i = 4;
            }
        }
        if (!stepped) return false;
        if (sent) return true;
    }
}

// ---- WindSurfStraight ------------------------------------------------------------------------------------------------------------

constexpr uint32_t kWindSurfWater = 0x4; // the tile is water ...
constexpr uint32_t kWindSurfBlocked = 0x10; // ... and not taken by a building
constexpr int kWindSurfReach = 5; // at most five tiles ahead are looked at
constexpr int32_t kWindSurfBlockedState = 5; // Guest::m_ActionState when there is no water ahead (the guest turns round)
constexpr int32_t kWindSurfTurnMessage = 0x43; // MT_CHAT with flags 1 and the previous action state in data
constexpr int32_t kWindSurfNextActionMessage = 0x39; // MT_DO_NEXT_ACTION, delayed by the length of the turn animation
constexpr int32_t kWindSurfNextActionState = 0x3d; // its extra
constexpr int32_t kWindSurfAnimPlayOnce = 2; // SpriteInstance::m_animType of the board sprite while it turns
constexpr const char* kWindSurfTurnGuestAnim = "D_WINDSURF_TURNL"; // the guest's own animation (its length is the delay)
constexpr const char* kWindSurfTurnBoardAnim = "WINDSURF_TURNL"; // the board sprite's

// How many free water tiles lie straight ahead of `start` in the sprite's direction: 0..5, counting from the first tile ahead and stopping at the first one
// that is not water or is taken.
inline int WaterAhead(const map::Grid& grid, TileXY start, int direction)
{
    TileXY tile = grid_math::Neighbour(grid.m_width, grid.m_height, start.x, start.y, direction);
    int count = 0;
    do {
        const uint32_t flags = grid.Cell(tile.x, tile.y).m_flags;
        if ((flags & kWindSurfWater) == 0 || (flags & kWindSurfBlocked) != 0) break;
        ++count;
        tile = grid_math::Neighbour(grid.m_width, grid.m_height, tile.x, tile.y, direction);
    } while (count < kWindSurfReach);
    return count;
}

// The tile `steps` tiles ahead of `start`.
inline TileXY TileAhead(const map::Grid& grid, TileXY start, int direction, int steps)
{
    TileXY tile = start;
    for (int i = 0; i < steps; ++i) tile = grid_math::Neighbour(grid.m_width, grid.m_height, tile.x, tile.y, direction);
    return tile;
}

// The number of tiles the guest swims on: a draw modulo the free tiles ahead, plus one (the draw is divided signed: rand() is never negative).
inline int WindSurfSteps(int32_t draw, int freeTiles)
{
    return draw % freeTiles + 1;
}

// ---- GotoDancePoint --------------------------------------------------------------------------------------------------------------

constexpr uint32_t kDanceFloorFlag = 0x800; // the cell flag the building's footprint carries ("built", set by FinishBuild)
constexpr int32_t kDanceSpread = 8; // a candidate lies at most 8 tiles from the guest on either axis
constexpr float kDanceTileDraw = -16.0f; // the draw is scaled to -16..0 and subtracted from -8
constexpr float kDanceJitter = 20.0f; // the point inside the tile, +-10 units around the tile's bottom point
constexpr float kDanceJitterCentre = 10.0f;
constexpr uint32_t kDanceMaxColumn = 0x80; // the candidate's x must be below this (signed); y has only a lower bound
constexpr int kDanceTilePoint = 4; // Tile-to-world conversion kind: the bottom point
constexpr int kDanceMaxRetries = 1000000; // a dance floor out of reach would never end the search; the cap stops it

// One axis of a candidate: -8 - trunc(draw * kRandUnit * -16). 32-bit wrapping subtraction.
inline int32_t DanceOffset(int32_t draw)
{
    const int32_t scaled =
            FloatToIntWrap(static_cast<double>(draw) * static_cast<double>(script::kRandUnit) * static_cast<double>(kDanceTileDraw));
    return static_cast<int32_t>(0xfffffff8u - static_cast<uint32_t>(scaled));
}

// A tile of the game's grid that may lie outside it: the candidate is a plain 32-bit pair, the grid wraps it when it reads a cell.
struct RawTile {
    int32_t x, y;
};

// The footprint of the dance floor: `outer` rows (steps in direction 6 from the anchor tile) of `inner` tiles (steps in direction 0). The even and odd
// sprite directions of the building swap which of its grid width and height is which.
struct DanceFloor {
    TileXY anchor;
    int32_t outer, inner;
};

inline DanceFloor DanceFloorOf(TileXY anchor, int32_t spriteDirection, int32_t gridWidth, int32_t gridHeight)
{
    const bool even = spriteDirection % 2 == 0; // signed remainder
    return even ? DanceFloor{anchor, gridWidth, gridHeight} : DanceFloor{anchor, gridHeight, gridWidth};
}

// Calls visit(tile) for every tile of the footprint in a fixed order (rows of `inner` tiles, `outer` rows).
template<class Visit> void ForEachFloorTile(const map::Grid& grid, const DanceFloor& floor, Visit&& visit)
{
    TileXY row = floor.anchor, tile = floor.anchor;
    for (int32_t outer = floor.outer; outer > 0; --outer) {
        for (int32_t inner = floor.inner; inner > 0; --inner) {
            visit(tile);
            tile = grid_math::Neighbour(grid.m_width, grid.m_height, tile.x, tile.y, 0);
        }
        row = grid_math::Neighbour(grid.m_width, grid.m_height, row.x, row.y, 6);
        tile = row;
    }
}

// Whether the (raw) candidate is one of the footprint's tiles: the candidate's two 32-bit words are compared with each wrapped tile.
inline bool OnDanceFloor(const map::Grid& grid, const DanceFloor& floor, RawTile candidate)
{
    bool inside = false;
    ForEachFloorTile(grid, floor, [&](TileXY tile) {
        if (static_cast<uint32_t>(candidate.x) == tile.x && static_cast<uint32_t>(candidate.y) == tile.y) inside = true;
    });
    return inside;
}

// The tile the guest dances on: a random candidate within 8 tiles of the guest that stands on the footprint, carries the built flag, is not the guest's
// own tile and lies at x 0..127 and y >= 0. The first candidate is drawn and discarded (the "off the floor" flag starts set), then candidates are drawn
// until one fits; each draws x and then y. Null after `maxRetries` candidates.
template<class Rand>
std::optional<RawTile> FindDanceTile(
        const map::Grid& grid, TileXY guest, const DanceFloor& floor, Rand&& rand, int maxRetries = kDanceMaxRetries)
{
    const auto draw = [&] {
        const int32_t dx = DanceOffset(rand());
        const int32_t dy = DanceOffset(rand());
        return RawTile{static_cast<int32_t>(guest.x) + dx, static_cast<int32_t>(guest.y) + dy};
    };
    RawTile candidate = draw();
    bool offFloor = true;
    for (int retry = 0; retry < maxRetries; ++retry) {
        if ((grid.Cell(static_cast<uint32_t>(candidate.x), static_cast<uint32_t>(candidate.y)).m_flags & kDanceFloorFlag) != 0) {
            const bool here = static_cast<uint32_t>(candidate.x) == guest.x && static_cast<uint32_t>(candidate.y) == guest.y;
            if (!here && !offFloor && candidate.x < static_cast<int32_t>(kDanceMaxColumn) && candidate.x >= 0 && candidate.y >= 0)
                return candidate;
        }
        candidate = draw();
        offFloor = !OnDanceFloor(grid, floor, candidate);
    }
    return std::nullopt;
}

// The spot inside the chosen tile, from its bottom point (tile-to-world conversion kind 4) and two more draws, x first: draw * kRandUnit * 20 + point - 10,
// every step one 53-bit operation. x is rounded to float and then truncated, y stays a double until its truncation.
inline Goal DanceGoal(float pointX, float pointY, int32_t drawX, int32_t drawY)
{
    const double unit = static_cast<double>(script::kRandUnit);
    const double jitter = static_cast<double>(kDanceJitter);
    const double centre = static_cast<double>(kDanceJitterCentre);
    const float x = static_cast<float>((static_cast<double>(drawX) * unit * jitter + static_cast<double>(pointX)) - centre);
    const double y = (static_cast<double>(drawY) * unit * jitter + static_cast<double>(pointY)) - centre;
    return {FloatToIntWrap(static_cast<double>(x)), FloatToIntWrap(y)};
}

} // namespace obl::guest_walk
