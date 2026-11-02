#pragma once

#include "core/float_to_int.h"
#include "game/objects/guest_update_opinions.h"
#include "script_message.h"

#include <cmath>
#include <cstdint>

// The pure rules of DecideSwimAction, of the lifeguard search of UpdateDrowning and of the notice of
// SendOutDeathMessage, docs/architecture/objects.md.
// The game side (grid lookups, messages, StartStomachIllness) is in src/game/objects/guest_actions.cpp.
//
// 53-bit precision: every operation is one double rounding, every float store one float rounding. "Below" means below or unordered
// (guest_opinions::Below); "strictly above" is false for an unordered pair.
namespace obl::guest_actions {

using guest_opinions::Below;

constexpr uint32_t kSeaTile = 0x4; // grid cell flag: sea
constexpr uint32_t kBlockedTile = 0x10; // grid cell flag: blocked (building, scenery)
constexpr int32_t kStateBadGuts = 0x38; // STATE_GUEST_BAD_GUTS (StartStomachIllness)
constexpr float kSwimMin = 10.0f;
constexpr float kSwimMaxX = 4000.0f;
constexpr float kSwimMaxY = 8000.0f;

// One coordinate of a swim target: position + (draw * * (r - -r) - r), the offset stored as float before the addition.
inline float SwimCoordinate(float position, float radius, int32_t draw)
{
    const double r = radius;
    const float offset = static_cast<float>(static_cast<double>(draw) * static_cast<double>(script::kRandUnit) * (r - -r) - r);
    return static_cast<float>(static_cast<double>(position) + static_cast<double>(offset));
}

// Below 10 (NaN included) becomes 10, strictly above `max` becomes `max`.
inline float ClampSwim(float value, float max)
{
    if (Below(value, kSwimMin)) return kSwimMin;
    return value > max ? max : value;
}

struct SwimTarget {
    float x, y;
};

// Draws x then y around (x, y) within +-radius, clamps to 10..4000 x 10..8000 and redraws until the tile is free sea. It
// never gives up (a radius that reaches no free sea tile hangs). `rand()` is the game's rand, `tileFlags(x, y)` the grid flags of the tile
// of a world point (world-to-tile conversion at the guest's height).
template<class Rand, class TileFlags> SwimTarget DecideSwimTarget(float x, float y, float radius, Rand&& rand, TileFlags&& tileFlags)
{
    for (;;) {
        const int32_t drawX = rand();
        const int32_t drawY = rand();
        const SwimTarget target{
                ClampSwim(SwimCoordinate(x, radius, drawX), kSwimMaxX), ClampSwim(SwimCoordinate(y, radius, drawY), kSwimMaxY)};
        const uint32_t flags = tileFlags(target.x, target.y);
        if ((flags & kSeaTile) && !(flags & kBlockedTile)) return target;
    }
}

// The 3-D distance of a lifeguard: float differences in double, summed (dz^2 + dy^2) + dx^2, square root; not rounded to float before the compare.
inline double LifeguardDistance(const float (&guard)[3], const float (&guest)[3])
{
    const double dx = static_cast<double>(guard[0]) - guest[0];
    const double dy = static_cast<double>(guard[1]) - guest[1];
    const double dz = static_cast<double>(guard[2]) - guest[2];
    return std::sqrt((dz * dz + dy * dy) + dx * dx);
}

// The nearest lifeguard so far: starts at 1e8 with no id (-2). A guard wins when its distance is below the best float (below or unordered), so
// a tie keeps the first, and a NaN distance wins and then lets every later guard win too (nothing compares as not-below NaN).
struct NearestLifeguard {
    static constexpr int32_t kNone = -2;
    int32_t id = kNone;
    float best = 1e8f;
    void Offer(int32_t guard, double distance)
    {
        if (!Below(distance, best)) return;
        best = static_cast<float>(distance);
        id = guard;
    }
};

// ---- SendOutDeathMessage -------------------------------------------------------------------------------------------

constexpr int32_t kStateAttackByShark = 0x36; // STATE_GUEST_ATTACK_BY_SHARK: the guest was eaten, any other state drowned
constexpr int kDeathRadius = 5; // every guest within 5 tiles is told (the dead one too)
constexpr int32_t kSpotEaten = 8; // MT_SPOT_OPINION kinds (ReactToSpottedEvent)
constexpr int32_t kSpotDrowned = 10;
constexpr uint32_t kDeathTickerType = 5; // feedback::Feedback::AddText type of the notice

// What a death is called: the text lookup key of the ticker line, the spot opinion kind and whether the world eaten or drowned counter increases.
struct DeathNotice {
    const char* key;
    int32_t spotKind;
    bool eaten;
};

inline DeathNotice DeathNoticeFor(int32_t state)
{
    if (state == kStateAttackByShark) return {"GUEST_EATEN", kSpotEaten, true};
    return {"GUEST_DROWNED", kSpotDrowned, false};
}

} // namespace obl::guest_actions
