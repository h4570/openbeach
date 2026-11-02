#pragma once

#include "core/float_to_int.h"
#include "game/objects/guest_update_opinions.h"

#include <cmath>
#include <cstdint>

// The pure rules of the guest fights (FindFightPoint, FindFightPerson, TellNearbyOfFight)
// and of the vandalism (LookForVandalItem, VandaliseBin), guest fights and vandalism, docs/architecture/objects.md.
// The game side (object lists, grid lookups, messages) is in src/game/objects/guest_conflict.cpp.
//
// 53-bit precision: every operation is one double rounding, every float store one float rounding. "Below" means below or unordered
// (guest_opinions::Below).
namespace obl::guest_conflict {

using guest_opinions::Below;

// ---- fights ---------------------------------------------------------------------------------------------------------------------

constexpr int32_t kStateSearchDesire = 0xc; // STATE_GUEST_SEARCH_DESIRE
constexpr int32_t kStateHeadingDesire = 0xd; // STATE_GUEST_HEADING_DESIRE
constexpr int32_t kStateLookForFight = 0x1f; // STATE_GUEST_LOOK_FOR_FIGHT
constexpr int32_t kStateFighting = 0x20; // STATE_GUEST_FIGHTING
constexpr int32_t kSpotFight = 3; // MT_SPOT_OPINION kind: a fight seen (ReactToSpottedEvent)
constexpr uint32_t kGridTiles = 0x80; // the grid is 128 x 128 tiles
constexpr int32_t kBarRadius = 7; // FindFightPoint: +-7 tiles around the nearest bar
constexpr int32_t kBarSpan = 15; // 2 * 7 + 1 values per draw
constexpr float kNoBarDistance = 50000.0f; // start of the nearest-bar search (a float)
constexpr float kOpponentOffset = 20.0f; // FindFightPerson: the guest stands this far left (-x) of the opponent
constexpr float kLookAroundDelay = 2.0f; // TellNearbyOfFight: MT_LOOK_AROUND to itself after 2 s
constexpr int kPersonRadius = 2; // FindFightPerson looks at the tiles within 2
constexpr int kWitnessRadius = 1; // TellNearbyOfFight informs the guests within 1

// ---- topless sunbathing (TellNearbyOfTopless) -----------------------------------------------------------------
constexpr int kToplessRadius = 1; // the guests within 1 tile are told
constexpr int32_t kSpotTopless = 4; // MT_SPOT_OPINION kind: a topless guest seen (ReactToSpottedEvent)
constexpr int kToplessWitnessGender = 0; // GuestDef::m_Gender 0 = male: only men get the message (the sender too, when it is a man)

// A guest class object (type 3) of the list around the sender gets the message when its definition has the witness gender.
inline bool IsToplessWitness(int gender)
{
    return gender == kToplessWitnessGender;
}

// The ground a fight may take place on: walkable pavement/beach ((f & 0x10000080) without the blocked bit 0x8000) or land/water bit 2 with neither
// 0x8000 nor 0x10 (the same test sits in both FindFightPerson and FindFightPoint).
inline bool WalkableTile(uint32_t flags)
{
    return ((flags & 0x10000080u) != 0 && (flags & 0x8000u) == 0) || ((flags & 2u) != 0 && (flags & 0x8010u) == 0);
}

// The tile distance of the bar search: sqrt of the sum dy*dy + dx*dx, which wraps in 32 bits and is then converted as a 64-bit integer, i.e. unsigned.
inline double BarDistance(int32_t dx, int32_t dy)
{
    const uint32_t sum = static_cast<uint32_t>(dy) * static_cast<uint32_t>(dy) + static_cast<uint32_t>(dx) * static_cast<uint32_t>(dx);
    return std::sqrt(static_cast<double>(sum));
}

// The nearest open bar so far (compared with the float best, unordered counts as below): a tie keeps the first building, a building with no bar at all leaves index 0.
struct NearestBar {
    float best = kNoBarDistance;
    uint32_t index = 0;
    void Offer(uint32_t building, double distance)
    {
        if (!Below(distance, best)) return;
        best = static_cast<float>(distance);
        index = building;
    }
};

// One coordinate of the walking target: base + rand() % 15 - 7 (a signed remainder).
inline int32_t FightJitter(int32_t base, int32_t draw)
{
    return draw % kBarSpan + base - kBarRadius;
}

struct FightTile {
    int32_t x, y;
};

// Draws x then y around the bar's tile until the ground is walkable and the tile lies inside 0..127. `rand()` is the game's rand, `flagsAt(x, y)`
// the grid flags of a tile (both coordinates wrap modulo the grid size, only the range test sees negative values). It never
// gives up.
template<class Rand, class FlagsAt> FightTile PickFightTile(int32_t barX, int32_t barY, Rand&& rand, FlagsAt&& flagsAt)
{
    for (;;) {
        const int32_t x = FightJitter(barX, rand());
        const int32_t y = FightJitter(barY, rand());
        const bool inside = x >= 0 && x < static_cast<int32_t>(kGridTiles) && y >= 0 && y < static_cast<int32_t>(kGridTiles);
        if (WalkableTile(flagsAt(x, y)) && inside) return {x, y};
    }
}

// The opponent has to be in one of the two states a guest takes on its way to a desire.
inline bool CanBeFought(int32_t state)
{
    return state == kStateHeadingDesire || state == kStateLookForFight;
}

// The point beside the opponent: x minus 20, stored as a float.
inline float OpponentApproachX(float x)
{
    return static_cast<float>(static_cast<double>(x) - static_cast<double>(kOpponentOffset));
}

// ---- vandalism ------------------------------------------------------------------------------------------------------------------

constexpr int32_t kStateVandalised = 4; // the state a vandal sets on its target; an object in it is skipped
constexpr float kVandalReach = 10.0f; // the search metric starts here (a float)
constexpr uint32_t kVandalDirections = 8; // sprite directions with a table entry
constexpr int32_t kKindNone = 4; // "nothing found" in the kind slot
constexpr int32_t kActionStateIdle = 1; // ActionState after a fruitless search
constexpr int32_t kActionStateFullBin = 2, kActionStateBin = 3, kActionStateBench = 4, kActionStateLamp = 5;
constexpr int32_t kOffenceVandalism = 3; // MyOffence after VandaliseBin

enum VandalKind : int32_t { Bin = 0, Bench = 1, StreetLamp = 2 };

// Absolute value (INT_MIN stays INT_MIN).
inline int32_t Abs32(int32_t value)
{
    const uint32_t sign = static_cast<uint32_t>(value >> 31);
    return static_cast<int32_t>((static_cast<uint32_t>(value) ^ sign) - sign);
}

// The search metric of LookForVandalItem. It is not a true distance but |dx| + dy^2: sqrt(dx * dx) through a 64-bit integer conversion, a square root and a truncation
// (the 32-bit product may wrap, a negative square gives NaN and its truncation 0) plus dy * dy. All int32 arithmetic wraps.
inline int32_t VandalMetric(int32_t dx, int32_t dy)
{
    const uint32_t ax = static_cast<uint32_t>(Abs32(dx));
    const double square = static_cast<double>(static_cast<int32_t>(ax * ax));
    const uint32_t root = static_cast<uint32_t>(FloatToIntWrap(std::sqrt(square)));
    const uint32_t ay = static_cast<uint32_t>(Abs32(dy));
    return static_cast<int32_t>(root + ay * ay);
}

// The best candidate so far: the metric is converted from an integer and compared unrounded (a NaN or equal skips) with the best float, so
// a candidate wins when it is not greater, and a later one wins ties; the best is then kept as the metric rounded to float.
struct VandalSearch {
    float best = kVandalReach;
    int32_t kind = kKindNone;
    bool Reaches(int32_t metric) const { return !(static_cast<double>(metric) > static_cast<double>(best)); }
    void Take(int32_t metric, int32_t k)
    {
        best = static_cast<float>(metric);
        kind = k;
    }
};

// An object is left alone while it is in the vandalised state (4) or has a vandal, a cleaner or a mechanic.
inline bool VandalTargetFree(int32_t state, uint8_t vandalised, uint8_t cleaning, uint8_t repairing)
{
    return state != kStateVandalised && vandalised == 0 && cleaning == 0 && repairing == 0;
}

// Where the guest stands to do it: the object's position + (a, b) by sprite direction k (from the table of the kind: bins have
// wider steps along x at directions 2 and 6).
struct VandalOffset {
    float a, b;
};
inline VandalOffset VandalStandingOffset(int32_t kind, uint32_t direction)
{
    static constexpr float kBinA[8] = {0.0f, -15.0f, -30.0f, -15.0f, 0.0f, 15.0f, 30.0f, 15.0f};
    static constexpr float kOtherA[8] = {0.0f, -15.0f, -20.0f, -15.0f, 0.0f, 15.0f, 20.0f, 15.0f};
    static constexpr float kB[8] = {20.0f, 15.0f, 0.0f, -15.0f, -20.0f, -15.0f, 0.0f, 15.0f};
    return {(kind == Bin ? kBinA : kOtherA)[direction & 7], kB[direction & 7]};
}

// The ActionState of the found kind: a bin is "full" (2) when its level equals the definition's capacity, another bin 3, a bench 4, a lamp 5.
inline int32_t VandalActionState(int32_t kind, bool binFull)
{
    if (kind == Bin) return binFull ? kActionStateFullBin : kActionStateBin;
    return kind == Bench ? kActionStateBench : kActionStateLamp;
}

// Nothing to vandalise: the desire turns into aggression. Fight = float(Fight + Vandalise) (one double sum), capped at 1 only when strictly above
// it (NaN is left alone).
inline float VandalisedToFight(float fight, float vandalise)
{
    const float sum = static_cast<float>(static_cast<double>(fight) + static_cast<double>(vandalise));
    return static_cast<double>(sum) > 1.0 ? 1.0f : sum;
}

} // namespace obl::guest_conflict
