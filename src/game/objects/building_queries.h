#pragma once

#include "core/ascii_case.h"
#include <cstddef>
#include <cstdint>

// The pure parts of the small Building queries and setters (docs/architecture/objects.md): what
// HasState, SetQueueDirectionFromRotation, GetAverageMale/FemaleIntensity,
// GetOverlayYOffset, HasBuildJob/HasDemolishJob and GetThoughtObjectId decide. No game
// memory: the glue in src/game/objects/building_api.cpp reads the fields and calls these.
namespace obl {

// HasState: `this` is never read; `state` is compared as an unsigned number, so the answer
// is "state <= 4" for the five building states and false for everything above, negative values included.
inline bool BuildingStateIsKnown(uint32_t state)
{
    return state <= 4;
}

// SetQueueDirectionFromRotation: the direction of the queue (m_queue_direction x, y) per map rotation 0..3 in tiles of
// 16; the table covers 0..3 and any other (unsigned) rotation leaves the field alone.
struct QueueDirection {
    float x = 0.0f;
    float y = 0.0f;
};

inline bool QueueDirectionFromRotation(uint32_t rotation, QueueDirection& out)
{
    switch (rotation) {
    case 0: out = {16.0f, 16.0f}; return true;
    case 1: out = {16.0f, -16.0f}; return true;
    case 2: out = {-16.0f, -16.0f}; return true;
    case 3: out = {-16.0f, 16.0f}; return true;
    default: return false;
    }
}

// GetAverageMale/FemaleIntensity: the sum of the first `count` entries of one column of the interleaved intensity table
// (`stride` 2 floats per slot) divided by `count`. The sum is a running double over single floats and the result is
// a double; count 0 gives 0.0 (the constant), a negative count skips the loop and divides 0.0 by it.
inline double AverageIntensity(const float* column, int32_t count)
{
    if (count == 0) return 0.0;
    double sum = 0.0;
    for (int32_t i = 0; i < count; ++i) sum += static_cast<double>(column[i * 2]);
    return sum / static_cast<double>(count);
}

// GetOverlayYOffset: half the anim's hotspot Y, rounded towards zero.
inline int32_t OverlayYOffset(int16_t hotspotY)
{
    return static_cast<int32_t>(hotspotY) / 2;
}

// HasBuildJob/HasDemolishJob: the construction manager answers a float (progress); the code compares it with 0.0
// (equal and unordered both count as zero) and answers true only for an ordered, non-zero value.
inline bool ProgressIsNonZero(float progress)
{
    return progress < 0.0f || progress > 0.0f;
}

// SetBeerType / SetBeerStrength: the slot that sells beer is the first of the definition's
// commodities whose display text (`CommodityDef::m_name`) equals "Beer" (`_stricmp`, so ASCII case is ignored). Every other
// commodity text is a different word, a miss answers -1.
constexpr int32_t kNoBeerSlot = -1;

// `nameAt(i)` is the display text of the i-th commodity of the definition; `count` is compared unsigned.
template<class NameAt> int32_t FindBeerSlot(uint32_t count, NameAt&& nameAt)
{
    for (uint32_t i = 0; i < count; ++i)
        if (ascii::EqualsNoCase("Beer", nameAt(i))) return static_cast<int32_t>(i);
    return kNoBeerSlot;
}

// The cost per serving of a beer slot: BeerDef::m_BaseCost + m_StrCost * strength. The strength is multiplied
// by m_StrCost and added to the base cost (a double accumulates the same single-float product exactly).
inline float BeerSlotCost(float baseCost, float strengthCost, float strength)
{
    return static_cast<float>(static_cast<double>(strengthCost) * static_cast<double>(strength) + static_cast<double>(baseCost));
}

} // namespace obl
