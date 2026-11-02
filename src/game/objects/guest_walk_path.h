#pragma once

#include "game/map/grid_math.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// The pure rules of WalkAlongPath (guest walk along the path, docs/architecture/objects.md): the guest looks at the eight tiles around it, picks one of those that
// lead somewhere (a draw modulo their number), follows that direction while the tiles stay such tiles and walks to the middle of the second-to-last one (see WalkTiles); with none it
// stands, thinks "no path" and tries again in two seconds. The game side (objects, thoughts, messages) is in src/game/objects/guest_walk_routes.cpp.
namespace obl::guest_path {

constexpr int32_t kStateDesperate = 0x3a; // Guest::m_state of a guest that walks as a last resort
constexpr const char* kDesperateWalk =
        "D_WALK_DESPERATE"; // the m_WalkAnimation of such a guest that makes the walk a "desperate" one (m_ActionState 1)
constexpr int32_t kActionStateDesperate = 1;
constexpr const char* kStandAnimation = "StandAnimation"; // played, not looped, when there is no path
constexpr int32_t kRetryMessage = 0x16; // the delayed message to the guest itself after a failed walk (extra = its state)
constexpr float kRetryDelay = 2.0f;

// A tile the guest can walk on to: a tile marked 0x10000080 and not 0x8000, or a land tile (2) with neither 0x8000 nor the building bit (0x10).
constexpr uint32_t kMarkedTile = 0x10000080;
constexpr uint32_t kPavementBit = 0x8000;
constexpr uint32_t kLandBit = 2;
constexpr uint32_t kPavementOrBuilding = 0x8010;

inline bool WalkableTile(uint32_t flags)
{
    return ((flags & kMarkedTile) != 0 && (flags & kPavementBit) == 0) || ((flags & kLandBit) != 0 && (flags & kPavementOrBuilding) == 0);
}

// The inline byte-by-byte comparison of the walk animation with "D_WALK_DESPERATE" (a plain strcmp, bounded by the field).
inline bool IsDesperateWalk(const char* walkAnimation, size_t size)
{
    return std::strncmp(walkAnimation, kDesperateWalk, size) == 0;
}

inline int32_t ActionStateOf(int32_t state, const char* walkAnimation, size_t size)
{
    return state == kStateDesperate && IsDesperateWalk(walkAnimation, size) ? kActionStateDesperate : 0;
}

// The number of walkable tiles around (tileX, tileY); no draw is made for none.
inline uint32_t WalkableNeighbours(const map::Grid& grid, uint32_t tileX, uint32_t tileY)
{
    uint32_t count = 0;
    for (int d = 0; d < 8; ++d) {
        const grid_math::TileXY n = grid_math::Neighbour(grid.m_width, grid.m_height, tileX, tileY, d);
        count += WalkableTile(grid.Cell(n.x, n.y).m_flags);
    }
    return count;
}

// With at least one walkable neighbour: one draw (unsigned, modulo the count, from 1) picks the k-th walkable neighbour, then the walk goes on in that direction
// for as long as the tile is a walkable one. `visit` gets the tile whose middle becomes the goal, once per step. The coordinates kept for the next middle
// are those of the tile just left, so after the first step the tile visited is the one before the one being tested: the goal is the second-to-last tile
// of the row (the only one, when the row is a single tile). False, with no draw, for none.
template<class Rand, class Visit> bool WalkTiles(const map::Grid& grid, uint32_t tileX, uint32_t tileY, Rand&& rand, Visit&& visit)
{
    grid_math::TileXY neighbours[8];
    bool walkable[8];
    uint32_t count = 0;
    for (int d = 0; d < 8; ++d) {
        neighbours[d] = grid_math::Neighbour(grid.m_width, grid.m_height, tileX, tileY, d);
        walkable[d] = WalkableTile(grid.Cell(neighbours[d].x, neighbours[d].y).m_flags);
        count += walkable[d];
    }
    if (count == 0) return false;
    uint32_t remaining = static_cast<uint32_t>(rand()) % count + 1;
    int chosen = 0;
    for (int d = 0; remaining != 0; ++d) {
        if (!walkable[d]) continue;
        --remaining;
        chosen = d;
    }
    grid_math::TileXY tile = neighbours[chosen];
    grid_math::TileXY middle = tile;
    while (WalkableTile(grid.Cell(tile.x, tile.y).m_flags)) {
        visit(middle);
        middle = tile;
        tile = grid_math::Neighbour(grid.m_width, grid.m_height, tile.x, tile.y, chosen);
    }
    return true;
}

// The thoughts: `<prefix><digit>` with the digit `rand() % 3 + 1`. A guest on foot (activity 0) names the building it was heading for, one on an activity the sea
// (desired commodity 2) or the beach; with no walkable tile at all it is always "ATALL".
constexpr const char* kThoughtBuilding = "GUEST_THOUGHT_NO_PATH_"; // + the building's key
constexpr const char* kThoughtSea = "GUEST_THOUGHT_NO_PATH_SEA";
constexpr const char* kThoughtBeach = "GUEST_THOUGHT_NO_PATH_BEACH";
constexpr const char* kThoughtAtAll = "GUEST_THOUGHT_NO_PATH_ATALL";
constexpr int32_t kSeaCommodity = 2;

} // namespace obl::guest_path
