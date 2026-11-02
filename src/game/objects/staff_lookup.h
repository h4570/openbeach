#pragma once

#include "game/objects/object_staff.h"
#include "game/map/path_tiles.h"

#include <cstddef>
#include <cstdint>

// Staff queries: where a staff member can stand or wander to, which script states a profession has, the
// home building and the selection level. The functions that look at the terrain take the game through an environment:
//   env.CellFlags(x, y, z)  - the flags (GridCell::m_flags) of the tile under a world point (the cell at the world position)
//   env.Rand()              - the game's rand()
//   env.TileFlags(x, y, z)  - the same through the map view world-to-tile conversion, the way the wander search reads the grid
namespace obl::staff {

// The walkable test of the neighbour search is the pavement predicate of the route search (the pavement predicate, same truth table).
// FindWalkablePointNear (no object needed): tries the four diagonal
// neighbours of `pos` (+dx -dy, -dx +dy, -dx -dy, +dx +dy, in that order) and moves `pos` to the first one whose tile
// is a pavement tile. The sums are one rounding to float of a double sum (53-bit precision); z stays.
template<class Env> bool FindWalkablePointNear(float* pos, int dx, int dy, Env& env)
{
    for (int attempt = 0; attempt < 4; ++attempt) {
        float x = pos[0];
        float y = pos[1];
        const float z = pos[2];
        const double mx = static_cast<double>(dx);
        const double my = static_cast<double>(dy);
        switch (attempt) {
        case 0:
            x = static_cast<float>(static_cast<double>(x) + mx);
            y = static_cast<float>(static_cast<double>(y) - my);
            break;
        case 1:
            x = static_cast<float>(static_cast<double>(x) - mx);
            y = static_cast<float>(my + static_cast<double>(y));
            break;
        case 2:
            x = static_cast<float>(static_cast<double>(x) - mx);
            y = static_cast<float>(static_cast<double>(y) - my);
            break;
        default:
            x = static_cast<float>(static_cast<double>(x) + mx);
            y = static_cast<float>(my + static_cast<double>(y));
            break;
        }
        if (path::IsPavementTile(env.CellFlags(x, y, z))) {
            pos[0] = x;
            pos[1] = y;
            pos[2] = z;
            return true;
        }
    }
    return false;
}

// FindWanderPos: picks the point of `m_WanderPos` (x, y; z stays) the staff member strolls to. Each try draws
// two offsets in [-300, 300] around the member (offset = -300 - trunc(rand * (1/32767) * -600), the product in double), resets a coordinate that
// left the map (x outside [0, 4096), y outside [0, 8192)) to the member's own, and asks the tile: a pavement or `0x2` tile that no building body
// occupies (0x10) is taken; so is whatever the 1001st try gives (the loop stops when the try counter reaches 1000).
constexpr float kRandScale = 3.0518509447574615e-05f; // 1/32767 as float
constexpr float kRandSpan = -600.0f;
constexpr float kMapWidth = 4096.0f;
constexpr float kMapHeight = 8192.0f;

template<class Env> void FindWanderPos(object::Staff& staff, Env& env)
{
    const float homeX = staff.m_WorldXYZ[0];
    const float homeY = staff.m_WorldXYZ[1];
    const float z = staff.m_WorldXYZ[2];
    float x = homeX;
    float y = homeY;
    auto offset = [&env]() {
        const int draw = env.Rand();
        const double scaled = static_cast<double>(draw) * static_cast<double>(kRandScale) * static_cast<double>(kRandSpan);
        return -300 - static_cast<int>(scaled); // FTOL truncates toward zero
    };
    auto sample = [&] {
        x = homeX;
        y = homeY;
        x = static_cast<float>(static_cast<double>(offset()) + static_cast<double>(x));
        y = static_cast<float>(static_cast<double>(offset()) + static_cast<double>(y));
        // "Below or unordered": a NaN takes the same reset as a coordinate below zero.
        if (!(x >= 0.0f) || x >= kMapWidth) x = homeX;
        if (!(y >= 0.0f) || y >= kMapHeight) y = homeY;
    };
    sample();
    for (uint32_t tries = 0;; ++tries) {
        const uint32_t flags = env.TileFlags(x, y, z);
        if (((flags & (path::kTilePavement | path::kTileBit2)) != 0 && (flags & path::kTileOccupied) == 0) || tries >= 1000) break;
        sample();
    }
    staff.m_WanderPos[0] = x;
    staff.m_WanderPos[1] = y;
}

// Profession HasState queries: the script states (docs/architecture/objects.md) the profession's logic has; asked by the script engine.
enum class StateKind { Cleaner, Lifeguard, Mechanic, Rep, Security, Builder };

namespace query_detail {

struct StateRange {
    uint32_t first;
    uint32_t last;
};

// Every table lists a profession's states as inclusive ranges.
inline constexpr StateRange kCleaner[] = {{0, 1}, {6, 6}, {0x4c, 0x4d}, {0x68, 0x75}, {0x83, 0x83}};
inline constexpr StateRange kLifeguard[] = {{1, 1}, {6, 6}, {0x4c, 0x57}, {0x83, 0x83}};
inline constexpr StateRange kMechanic[] = {{0, 1}, {6, 6}, {0x4c, 0x4d}, {0x58, 0x5e}, {0x83, 0x83}};
inline constexpr StateRange kRep[] = {{0, 1}, {6, 0xb}, {0x4c, 0x4d}, {0x83, 0x83}};
inline constexpr StateRange kSecurity[] = {{0, 1}, {6, 6}, {0x4c, 0x4d}, {0x76, 0x83}};
inline constexpr StateRange kBuilder[] = {{0, 1}, {6, 6}, {0x4c, 0x4c}, {0x5f, 0x67}, {0x83, 0x84}};

template<size_t N> bool In(const StateRange (&ranges)[N], uint32_t state)
{
    for (const StateRange& range : ranges)
        if (state >= range.first && state <= range.last) return true;
    return false;
}

} // namespace query_detail

inline bool HasState(StateKind kind, uint32_t state)
{
    using namespace query_detail;
    switch (kind) {
    case StateKind::Cleaner: return In(kCleaner, state);
    case StateKind::Lifeguard: return In(kLifeguard, state);
    case StateKind::Mechanic: return In(kMechanic, state);
    case StateKind::Rep: return In(kRep, state);
    case StateKind::Security: return In(kSecurity, state);
    case StateKind::Builder: return In(kBuilder, state);
    }
    return false;
}

// SetHome in the base and the professions ( the service staff and the snooty one override it).
inline void SetHome(object::Staff& staff, uint32_t homeId)
{
    staff.m_HomeID = static_cast<int32_t>(homeId);
}

// ReduceSelectionToHover: a selected (2) staff member is only hovered (1) afterwards.
inline void ReduceSelectionToHover(object::Staff& staff)
{
    if (staff.m_SelectLevel == 2) staff.m_SelectLevel = 1;
}

} // namespace obl::staff
