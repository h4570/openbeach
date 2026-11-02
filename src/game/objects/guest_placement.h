#pragma once

#include "game/map/grid_state.h"
#include "game/objects/guest_beach_points.h"
#include "game/objects/guest_update_opinions.h"
#include "script_message.h"

#include <cstdint>

// The pure rules of the small guest functions that put a guest somewhere (guest placement, docs/architecture/objects.md): SetupSnogOffset,
// SetSurfacePoint, IsEntryPointClear and the hotel choice of FindLodging. The game side
// (object lists, grid lookups, virtual calls) is in src/game/objects/guest_placement.cpp.
//
// 53-bit precision: every operation is one double rounding, every float store one float rounding.
namespace obl::guest_placement {

// ---- SetupSnogOffset ----------------------------------------------------------------------------------------------------------------

// Where the snog partner is put relative to the guest, by the guest's sprite direction (0..7): a fixed step in world units. A direction past 7
// has no step, so the caller handles that case itself.
struct SnogStep {
    int32_t x, y;
};

constexpr bool SnogStepFor(uint32_t direction, SnogStep& step)
{
    constexpr SnogStep kSteps[8] = {{0, -14}, {11, -12}, {15, 0}, {11, 10}, {0, 16}, {-10, 10}, {-15, 0}, {-10, -10}};
    if (direction > 7) return false;
    step = kSteps[direction];
    return true;
}

// Step plus float position, stored as float.
inline float SnogCoordinate(int32_t step, float position)
{
    return static_cast<float>(static_cast<double>(step) + static_cast<double>(position));
}

// ---- SetSurfacePoint ----------------------------------------------------------------------------------------------------------------

constexpr float kSurfaceMin = 10.0f;
constexpr float kSurfaceMaxX = 4000.0f;
constexpr float kSurfaceMaxY = 8000.0f;
constexpr uint32_t kTileWater = 4; // the cell flag the surface point must stand on

struct SurfaceCandidate {
    float x, y;
};

// One candidate: the guest's position moved by a random offset of up to its swim distance `radius` either way (rand * 3.0518509e-05 * (r - -r) - r,
// the x offset stored as a float, the y one added unrounded), x first, then both clamped to the map (a NaN or too small value to 10).
template<class Draw> SurfaceCandidate SurfaceCandidateAround(float guestX, float guestY, float radius, Draw&& draw)
{
    const double r = static_cast<double>(radius);
    const double unit = static_cast<double>(script::kRandUnit);
    const float offsetX = static_cast<float>((static_cast<double>(draw()) * unit) * (r - -r) - r);
    const double offsetY = (static_cast<double>(draw()) * unit) * (r - -r) - r;
    float x = static_cast<float>(static_cast<double>(guestX) + static_cast<double>(offsetX));
    float y = static_cast<float>(static_cast<double>(guestY) + offsetY);
    if (guest_opinions::Below(x, kSurfaceMin))
        x = kSurfaceMin;
    else if (x > kSurfaceMaxX)
        x = kSurfaceMaxX;
    if (guest_opinions::Below(y, kSurfaceMin))
        y = kSurfaceMin;
    else if (y > kSurfaceMaxY)
        y = kSurfaceMaxY;
    return {x, y};
}

// ---- IsEntryPointClear --------------------------------------------------------------------------------------------------------------

// The tile of the entry point and its neighbours 0, 2, 4 and 6 carry no building (flag 0x10): the same test as the shore vertex of FindSwimPoint.
inline bool EntryPointClear(const map::Grid& grid, uint32_t tileX, uint32_t tileY)
{
    return guest_beach::detail::ShoreFree(grid, tileX, tileY);
}

// ---- FindLodging ------------------------------------------------------------------------------------------------------------

constexpr float kNoRoomLeaveTime = 21.0f; // the hour at which a guest without a room goes home

// A building has a room for the guest when its guest capacity differs from the guests it already holds (the capacity check tests for non-zero, so a
// full house is the only refusal; an over-full one counts as free).
constexpr bool HasRoom(int32_t capacity, int32_t guests)
{
    return capacity - guests != 0;
}

} // namespace obl::guest_placement
