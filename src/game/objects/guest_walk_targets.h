#pragma once

#include "core/float_to_int.h"
#include "game/objects/guest_update_opinions.h"
#include "script_message.h"

#include <cmath>
#include <cstdint>

// The pure rules of the guest functions that pick a walking goal and post it (guest walking goals, docs/architecture/objects.md): HeadForEntryPoint,
// GetSeaEntry, GotoWorldUnloadPoint, WalkToWindsurf and HeadForBin. The game
// side (objects, grid, the message post) is in src/game/objects/guest_walk_targets.cpp.
//
// 53-bit precision: a float added to a float constant is one double operation, a float store one float rounding, and
// the truncation (src/core/float_to_int.h) applies to what is left.
namespace obl::guest_walk {

constexpr int32_t kVehicleWithUnloadPoint =
        5; // m_vehicleType whose unload point is walked to; any other vehicle sends the guest to MT_MOVEIMHERE
constexpr int32_t kUnloadRotationEast = 7; // sprite direction of the vehicle that moves the point (-63, +14)
constexpr int32_t kUnloadRotationWest = 5; // ... and (+22, +72)
constexpr int32_t kStateAfterMoveHere = 0x29; // Message::extra of the MT_MOVEIMHERE the other vehicles get
constexpr float kWindSurfOffsetX = -95.0f; // the footprint offset of the windsurf board spot
constexpr float kWindSurfOffsetY = -63.0f;

struct Goal {
    int32_t x, y; // flags and data of the message: the truncated target
};

// A goal posted as given: the two floats are truncated one after the other.
inline Goal TruncatedGoal(float x, float y)
{
    return {FloatToIntWrap(x), FloatToIntWrap(y)};
}

// The unload point of the vehicle at (x, y) turned `rotation`. Direction 7 subtracts 63 from x (kept as a double, not rounded to float) and adds 14 to y
// (stored as float); direction 5 adds 22 and 72 the same way; every other direction keeps the vehicle's own position.
inline Goal UnloadGoal(float x, float y, int32_t rotation)
{
    if (rotation == kUnloadRotationEast)
        return {FloatToIntWrap(static_cast<double>(x) - 63.0), FloatToIntWrap(static_cast<float>(static_cast<double>(y) + 14.0))};
    if (rotation == kUnloadRotationWest)
        return {FloatToIntWrap(static_cast<double>(x) + 22.0), FloatToIntWrap(static_cast<float>(static_cast<double>(y) + 72.0))};
    return TruncatedGoal(x, y);
}

// ---- HeadForBin --------------------------------------------------------------------------------------------------------

constexpr float kBinSearchRange = 160.0f; // bins farther than this are not looked at (the start of the best distance)
constexpr int32_t kActionStateNoBin = 1; // Guest::m_ActionState when no bin is chosen
constexpr int32_t kActionStateToBin = 2; // ... when the guest walks to one

// A bin takes more rubbish while its fill level is below the capacity; both are compared as unsigned.
inline bool BinHasRoom(int32_t items, int32_t capacity)
{
    return static_cast<uint32_t>(capacity) > static_cast<uint32_t>(items);
}

// The distance to a bin: the x difference stays a double, the y difference is stored as float before it is squared; summed
// dy^2 + dx^2 and square-rooted, not rounded to float for the comparison.
inline double BinDistance(float guestX, float guestY, float binX, float binY)
{
    const double dx = static_cast<double>(guestX) - static_cast<double>(binX);
    const float dy = static_cast<float>(static_cast<double>(guestY) - static_cast<double>(binY));
    return std::sqrt(static_cast<double>(dy) * dy + dx * dx);
}

// The nearest bin so far: `index` starts at the number of bins (none). A bin wins when its distance is below the best float (a tie
// keeps the first, a NaN distance wins and then lets every later bin win too); the best is then stored as float.
struct NearestBin {
    uint32_t index;
    float best = kBinSearchRange;
    explicit NearestBin(uint32_t count) : index(count) {}
    void Offer(uint32_t bin, double distance)
    {
        if (!guest_opinions::Below(distance, best)) return;
        best = static_cast<float>(distance);
        index = bin;
    }
};

// The guest goes to the bin when `rand * 3.0518509e-05` is below GuestDef::m_Litter (1/32767 of the draw, one 53-bit product; NaN counts as below).
inline bool UsesBin(int32_t draw, float chance)
{
    return guest_opinions::Below(static_cast<double>(draw) * static_cast<double>(script::kRandUnit), chance);
}

} // namespace obl::guest_walk
