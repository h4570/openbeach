#pragma once

#include "core/det_math.h"
#include "core/float_to_int.h"
#include "game/map/grid_state.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

// A moving object owns its cursor and shares the route through it. Serialization keeps its own stable labels.
namespace obl::path {

struct Route {
    float m_timeStamp = 0;
    // Preserve the saved reference count independently of shared_ptr ownership so a reload keeps the record unchanged.
    int32_t m_referenceCount = 0;
    std::vector<map::TilePoint> m_steps; // "m_pDirections": the tiles, (x, y) pairs
    int32_t m_start[2] = {-1, -1};
    int32_t m_stop[2] = {-1, -1};
    int32_t m_pathType = 0; // land 1, through scenery 4, sea 3, pavement 0 (path_tiles.h)
    uint8_t m_valid = 1;
};

// Owned by Mover::m_pathCursor.
struct RouteCursor {
    std::shared_ptr<const Route> m_route;
    uint8_t m_reverse = 0;
    int32_t m_currentStep = 0;
};

inline int StepCount(const RouteCursor& cursor)
{
    return static_cast<int>(cursor.m_route->m_steps.size());
}

inline RouteCursor MakeCursor(std::shared_ptr<const Route> route, bool reverse)
{
    RouteCursor cursor;
    cursor.m_reverse = reverse;
    cursor.m_currentStep = reverse ? static_cast<int>(route->m_steps.size()) - 1 : 0;
    cursor.m_route = std::move(route);
    return cursor;
}

inline const uint32_t* StepTile(const RouteCursor& cursor)
{
    const auto* tiles = reinterpret_cast<const uint8_t*>(cursor.m_route->m_steps.data());
    return reinterpret_cast<const uint32_t*>(tiles + static_cast<uintptr_t>(static_cast<uint32_t>(cursor.m_currentStep)) * 8u);
}

inline int Advance(RouteCursor& cursor)
{
    cursor.m_currentStep = std::bit_cast<int>(static_cast<uint32_t>(cursor.m_currentStep) + (cursor.m_reverse ? 0xffffffffu : 1u));
    const int index = cursor.m_currentStep;
    const int count = StepCount(cursor);
    if (index == count || index < 0) return 2;
    return cursor.m_reverse ? index == 0 : index == count - 2;
}

inline float ApproxSqrt(float squared)
{
    const uint32_t bits = std::bit_cast<uint32_t>(squared);
    if (!bits) return 0;
    const uint32_t index = (bits >> 8) & 0xffff;
    constexpr uint32_t kExponentTwo = 1u << 22; // exponent bits of 2.0f, shifted right by 8
    constexpr uint32_t kMantissaMask = (1u << 23) - 1u; // the 23 mantissa bits
    const uint32_t input = ((index & 0x7fff) | (index & 0x8000 ? 0x3f8000 : kExponentTwo)) << 8;
    const uint32_t mantissa =
            std::bit_cast<uint32_t>(static_cast<float>(std::sqrt(static_cast<double>(std::bit_cast<float>(input))))) & kMantissaMask;
    return std::bit_cast<float>(mantissa | ((((bits + 0xc0800000u) >> 1) + 0x3f800000u) & 0x7f800000u));
}

// The direction-from-vector function (Vec2 by value, count): the sector (0.. count-1) of a unit vector, angle = acos(-y)
// mirrored for x < 0, rounded to the nearest of `count` sectors. -y is stored as a float; above 1 it becomes 1, below -1 (or NaN) -1;
// x < 0 is also taken for NaN (an unordered compare counts as below). The quotient is truncated.
inline int DirectionIndex(float x, float y, int count)
{
    constexpr double kTwoPi = 6.283185482025146484375;
    float cosine = static_cast<float>(static_cast<double>(y) * -1.0);
    if (static_cast<double>(cosine) > 1.0)
        cosine = 1.0f;
    else if (!(cosine >= -1.0f))
        cosine = -1.0f;
    double angle = det_math::Acos(static_cast<double>(cosine));
    if (!(x >= 0.0f)) angle = kTwoPi - angle;
    int bucket = FloatToIntWrap(angle / (kTwoPi / static_cast<double>(2 * count)));
    if (bucket % 2 != 0) ++bucket;
    return bucket / 2 % count;
}

inline int Direction(int tileSize, const map::TilePoint& from, const map::TilePoint& to)
{
    auto point = [tileSize](const map::TilePoint& tile) {
        const uint32_t half = static_cast<uint32_t>(tileSize) >> 1;
        return std::pair<float, float>{
                static_cast<float>(half * tile.x), static_cast<float>(static_cast<uint32_t>(tileSize) * tile.y + (tile.x & 1) * half)};
    };
    const auto a = point(from), b = point(to);
    float x = b.first - a.first, y = b.second - a.second;
    const float length = ApproxSqrt(static_cast<float>(static_cast<double>(x) * x + static_cast<double>(y) * y));
    const float estimate = std::bit_cast<float>(0x7f000000u - std::bit_cast<uint32_t>(length));
    const float inverse = static_cast<float>((2.0 - static_cast<double>(length) * estimate) * estimate);
    x *= inverse;
    y *= inverse;
    return DirectionIndex(x, y, 8);
}

inline bool IsStraight(const RouteCursor& cursor, int tileSize)
{
    const auto& steps = cursor.m_route->m_steps;
    if (steps.size() < 2) return false;
    const int direction = Direction(tileSize, steps[0], steps[1]);
    for (size_t i = 2; i < steps.size(); ++i)
        if (Direction(tileSize, steps[i - 1], steps[i]) != direction) return false;
    return true;
}

} // namespace obl::path
