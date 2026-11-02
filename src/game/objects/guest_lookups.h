#pragma once

#include "core/float_to_int.h"
#include "core/ascii_case.h"
#include "game/objects/guest_conflict.h"
#include "game/objects/guest_update_opinions.h"
#include "game/objects/guest_walk_targets.h"
#include "script_message.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// The pure rules of the guest functions that look for a place and post a few messages (guest place lookups, docs/architecture/objects.md): LookForIllPoint,
// LookForBench, QueueAtComplaints, QueueAtWharf and CreateLitter. The game side
// (objects, grid, the message post, the world's place command) is in src/game/objects/guest_lookups.cpp.
//
// 53-bit precision: a float added to a float constant is one double operation, a float store one float rounding.
namespace obl::guest_lookup {

// ---- LookForIllPoint -----------------------------------------------------------------------------------------------------------------------

constexpr int32_t kStateLeavingWorld = 0x29; // STATE_GUEST_LEAVING_WORLD, the guest without a hotel goes home

// ---- LookForBench --------------------------------------------------------------------------------------------------------------------------

constexpr int32_t kBenchTaken =
        4; // Entity::m_state of a bench that is taken, also the state the chosen bench is told (its free seats are asked once more)
constexpr int32_t kStateBench = 0x35; // the guest state entered after the bench message (the name is not mapped)

constexpr float kBenchRange = 5.0f; // tiles; the start of the best distance

// The tile distance to a bench: both differences and the sum of squares are 32-bit integers (converted as 64-bit), square root at 53 bits, not rounded to float for the
// comparison.
inline double BenchDistance(int32_t guestX, int32_t guestY, int32_t benchX, int32_t benchY)
{
    const uint32_t dx = static_cast<uint32_t>(guestX) - static_cast<uint32_t>(benchX);
    const uint32_t dy = static_cast<uint32_t>(guestY) - static_cast<uint32_t>(benchY);
    return std::sqrt(static_cast<double>(static_cast<int32_t>(dy * dy + dx * dx)));
}

// The nearest bench so far: `index` starts at the number of benches (none). A bench wins when its distance is below or equal to the best float
// (a tie goes to the later bench, and an unordered compare counts as equal, so a NaN distance wins and then lets every later bench win too);
// the best is then stored as float.
struct NearestBench {
    uint32_t index;
    float best = kBenchRange;
    explicit NearestBench(uint32_t count) : index(count) {}
    void Offer(uint32_t bench, double distance)
    {
        if (distance > static_cast<double>(best)) return;
        best = static_cast<float>(distance);
        index = bench;
    }
};

// A bench takes a guest while its seat counter is below the capacity of its definition; both are compared as unsigned.
inline bool BenchHasRoom(int32_t items, int32_t capacity)
{
    return guest_walk::BinHasRoom(items, capacity);
}

// ---- QueueAtComplaints / QueueAtWharf --------------------------------------------------------------------------------------------------

// The search area around the serve position of the building: a point `span` wide (centred on it), tried until its grid cell has one of the `wanted` bits
// and none of the `refused` bits, or until the thousandth miss; the point must lie on the map.
struct HuddleArea {
    double span;
    double half;
    uint32_t wanted;
    uint32_t refused;
};
constexpr HuddleArea kComplaintsArea{100.0, 50.0, 0x10000080u, 0x8010u};
constexpr HuddleArea kWharfArea{200.0, 100.0, 0x10000082u, 0x8000u};
constexpr int kHuddleAttempts = 1000; // the miss that makes any point on the map good, and the serve position itself the goal
constexpr float kMapWidth = 4096.0f;
constexpr float kMapHeight = 8192.0f;

struct HuddleSpot {
    float x, y;
};

// 0 <= x < 4096 and 0 <= y < 8192; a NaN fails (the compares send an unordered pair to a retry in the end), written out so that it does not
// depend on how the compiler orders a NaN compare.
inline bool OnMap(float x, float y)
{
    if (std::isnan(x) || std::isnan(y)) return false;
    return x < kMapWidth && 0.0f <= x && y < kMapHeight && 0.0f <= y;
}

// One axis: draw * kRandUnit * span - half, all in 53 bits.
inline double HuddleOffset(const HuddleArea& area, int32_t draw)
{
    return static_cast<double>(draw) * static_cast<double>(script::kRandUnit) * area.span - area.half;
}

// The loop of both functions. The first draw (x) is stored as a float, the second (y) stays in double precision, so y is one rounding of offset + serve y while x is
// the sum of the serve x and the rounded offset. A point is taken when its cell fits (or the miss count reached 1000) and it lies inside 0 <= x < 4096,
// 0 <= y < 8192 (NaN fails); the thousandth miss that is taken is replaced by the serve position itself. `flags(x, y)` is the cell's m_flags.
template<class Draw, class CellFlags>
HuddleSpot FindHuddleSpot(const HuddleArea& area, float serveX, float serveY, Draw&& draw, CellFlags&& flags)
{
    float offsetX = static_cast<float>(HuddleOffset(area, draw()));
    double offsetY = HuddleOffset(area, draw());
    for (int attempt = 0;; ++attempt) {
        const float x = static_cast<float>(static_cast<double>(serveX) + static_cast<double>(offsetX));
        const float y = static_cast<float>(offsetY + static_cast<double>(serveY));
        const uint32_t cell = flags(x, y);
        const bool fits = (cell & area.wanted) != 0 && (cell & area.refused) == 0;
        if ((fits || attempt >= kHuddleAttempts) && OnMap(x, y))
            return attempt == kHuddleAttempts ? HuddleSpot{serveX, serveY} : HuddleSpot{x, y};
        offsetX = static_cast<float>(HuddleOffset(area, draw()));
        offsetY = HuddleOffset(area, draw());
    }
}

// ---- GetConsumePoint -----------------------------------------------------------------------------------------------------------------------

constexpr int32_t kStateConsume =
        0x2d; // the state of a guest that looks for a place to consume, and of the partner it looks for (the name is not mapped)
constexpr int32_t kPartnerWaiting = 2; // Guest::m_ActionState of a partner that waits
constexpr int32_t kPartnerFound = 1; // the same field of the guest that found one
constexpr int32_t kBenchTrip = 0x14; // the same field of the guest that was sent to a bench
constexpr int kPartnerRadius = 5; // tiles
constexpr float kPartnerOffsetX = 20.0f; // the point is this far left of the partner (world units)
constexpr int32_t kBenchTripDraw = 10; // a bench is taken when rand() % 10 == 0
constexpr int32_t kConsumeJitterSpan = 500; // a random point is the start moved by draw % 500 - 250 on both axes
constexpr int32_t kConsumeJitterHalf = 250;

struct Point3 {
    float x, y, z;
};

// The cell the guest may be sent to: pavement (0x10000080) that is not 0x8000, or a cell with bit 2 that has neither 0x8000 nor 0x10 (one test, used
// by the partner point and the random point alike).
inline bool ConsumeCellFits(uint32_t flags)
{
    return ((flags & 0x10000080u) != 0 && (flags & 0x8000u) == 0) || ((flags & 2u) != 0 && (flags & 0x8010u) == 0);
}

// The point beside the partner: x is one rounding of x - 20, y and z are copied.
inline Point3 PartnerPoint(const float world[3])
{
    return {static_cast<float>(static_cast<double>(world[0]) - static_cast<double>(kPartnerOffsetX)), world[1], world[2]};
}

// 0 <= x <= 4096 and 0 <= y < 8192 (the x bound is inclusive here, unlike the huddle's); a NaN on either axis fails (the compares send an unordered pair
// to a retry on the x < 0 and y < 0 tests).
inline bool ConsumeOnMap(float x, float y)
{
    return x >= 0.0f && x <= kMapWidth && y >= 0.0f && y < kMapHeight;
}

// One axis of the random point: float(double(draw % 500 - 250) + double(start)); the draw is the signed remainder.
inline float ConsumeJitter(int32_t draw, float start)
{
    return static_cast<float>(static_cast<double>(draw % kConsumeJitterSpan - kConsumeJitterHalf) + static_cast<double>(start));
}

// The random point: the first try starts from `first` (the guest's position, or the partner point of the last partner that was found and did not fit), every
// retry from the guest's own position `home` again; x is drawn before y. A point is taken when its cell fits and it lies on the map. `flags(x, y, z)` is the
// cell's m_flags (z is the start's: the whole point goes to the world-to-tile conversion).
template<class Draw, class CellFlags> Point3 FindConsumeSpot(Point3 first, Point3 home, Draw&& draw, CellFlags&& flags)
{
    Point3 start = first;
    for (;;) {
        Point3 candidate = start;
        candidate.x = ConsumeJitter(draw(), start.x);
        candidate.y = ConsumeJitter(draw(), start.y);
        if (ConsumeCellFits(flags(candidate.x, candidate.y, candidate.z)) && ConsumeOnMap(candidate.x, candidate.y)) return candidate;
        start = home;
    }
}

// ---- FindFlirtTarget ------------------------------------------------------------------------------------------------------------------------------
// FindFlirtTarget (the guest looks for a partner: one random guest within two tiles when it fits, else the nearest open bar's surroundings). The
// bar search and the walkable tile are the ones of FindFightPoint (src/game/objects/guest_conflict.h: NearestBar, PickFightTile).

constexpr int kTottyRadius = 2; // tiles around the guest; one object of the list is drawn (rand() % count), not searched
constexpr int32_t kGenderMale = 0, kGenderFemale = 1; // GuestDef::m_Gender: a male guest looks for a female one
constexpr int32_t kStateTottyAlt =
        0x22; // the second state a partner may be in beside 0xd (STATE_GUEST_HEADING_DESIRE); the name is not mapped
constexpr int32_t kTottyState = 0x23; // the state the partner is changed to (MT_STATECHANGE data)
constexpr int32_t kTottyWorldKind = 4; // Tile-to-world conversion kind of the random point

// Equal, and also unordered, so a NaN preference matches anything.
inline bool TottyTasteMatches(float partnerAttractive, float myPreference)
{
    return !(partnerAttractive < myPreference) && !(partnerAttractive > myPreference);
}

struct TottyPartner {
    uint32_t id;
    uint8_t type; // Entity::m_type, 3 = guest
    int32_t state;
    int32_t gender;
    float attractive;
};
struct TottySeeker {
    uint32_t id;
    int32_t gender;
    float preference; // GuestDef::m_AttractivePref
};

inline bool IsTottyPartner(const TottySeeker& me, const TottyPartner& other)
{
    return other.id != me.id && other.type == 3 && other.gender == kGenderFemale && me.gender == kGenderMale &&
            (other.state == guest_conflict::kStateHeadingDesire || other.state == kStateTottyAlt) &&
            TottyTasteMatches(other.attractive, me.preference);
}

// ---- MoveActionPerson --------------------------------------------------------------------------------------------------------------------------
// MoveActionPerson: the guest's action person (m_ActivityPerson) is turned the way the guest looks and put beside it, hand in hand.

constexpr int32_t kStateHoldHands =
        0x27; // the state in which the partner is not moved aside (offset 0, no hand swap); the name is not mapped
constexpr const char* kHoldHandLeft = "D_WALK_HOLDHAND_LEFT";
constexpr const char* kHoldHandRight = "D_WALK_HOLDHAND_RIGHT";

// The partner copies the guest's sprite direction, clamped to the directions its own current animation has: direction >= count (signed) becomes count - 1,
// which is -1 for an animation with no directions.
inline int32_t PartnerDirection(int32_t guestDirection, uint8_t partnerDirections)
{
    return guestDirection < static_cast<int32_t>(partnerDirections) ? guestDirection : static_cast<int32_t>(partnerDirections) - 1;
}

struct HoldHandsRule {
    int32_t dx, dy; // world units added to the guest's position for the partner
    bool leftTest; // which animation the guest is tested for: LEFT (true) or RIGHT (false)
};

// The table (one entry per sprite direction 0..7) and its two animation tests. Another direction leaves the offsets at 0
// and takes the RIGHT test.
inline HoldHandsRule HoldHandsFor(uint32_t direction)
{
    switch (direction) {
    case 0: return {0x12, 0, true};
    case 1: return {0xc, 10, true};
    case 2: return {0, 0x12, false};
    case 3: return {0xc, -0xc, false};
    case 4: return {0x12, 0, false};
    case 5: return {0xc, 10, false};
    case 6: return {0, 0x12, true};
    case 7: return {0xc, -0xc, true};
    default: return {0, 0, false};
    }
}

// The animation the guest has when it is told to swap: it plays the one it is not playing, and the partner plays the one the guest left. Null when the
// guest's current animation is not the tested one (names compared as the byte loop does).
struct HandSwap {
    const char* guestPlays;
    const char* partnerPlays;
};
inline bool SwapHands(const HoldHandsRule& rule, const char* currentAnimation, HandSwap& swap)
{
    const char* tested = rule.leftTest ? kHoldHandLeft : kHoldHandRight;
    if (std::strcmp(currentAnimation, tested) != 0) return false;
    swap = rule.leftTest ? HandSwap{kHoldHandRight, kHoldHandLeft} : HandSwap{kHoldHandLeft, kHoldHandRight};
    return true;
}

// offset plus float position, stored as float: one double addition, one float rounding.
inline float PartnerCoordinate(int32_t offset, float position)
{
    return static_cast<float>(static_cast<double>(offset) + static_cast<double>(position));
}

// ---- CreateLitter ----------------------------------------------------------------------------------------------------------------------------

// The animation the guest is playing decides what it drops: a drink (standing or walking) a crushed or a standing can by one draw (odd: crushed), ice an ice
// splat, a burger (standing or walking) a burger, vomit a puddle. Any other animation drops nothing (no draw). Names are compared byte by byte,
// case-sensitive.
inline bool Is(const char* animation, const char* name)
{
    return ascii::EqualsNoCase(animation, name);
}

template<class Draw> const char* LitterType(const char* animation, Draw&& draw)
{
    if (Is(animation, "D_STAND_DROPDRINK") || Is(animation, "D_WALK_DRINK")) return (draw() & 1) != 0 ? "Littercancrush" : "Littercanstand";
    if (Is(animation, "D_STAND_DROPICE")) return "Littericesplat";
    if (Is(animation, "D_STAND_DROPBURGER") || Is(animation, "D_WALK_BURGER")) return "Litterburger";
    if (Is(animation, "D_STAND_VOMIT")) return "LitterPuke";
    return nullptr;
}

constexpr const char* kLitterSprite = "Litter"; // the scenery sprite name in the placement
constexpr int32_t kLitterOffenceState = 1; // Guest::m_OffenceState while the guest has just littered
constexpr int32_t kLitterRotations = 8; // the second byte of the layer word: draw % 8

} // namespace obl::guest_lookup
