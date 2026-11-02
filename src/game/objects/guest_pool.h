#pragma once

#include "core/ascii_case.h"
#include "core/float_to_int.h"
#include "game/map/grid_math.h"
#include "game/map/grid_state.h"
#include "game/objects/guest_walk_targets.h"
#include "game/map/grid_cell.h"

#include <cstdint>

// The pure rules of the two pool/slide guest functions (guest pool and slide walks, docs/architecture/objects.md): GetNextPoolPoint and
// ChooseFunSlideAction. The game side (objects, sprites, the message posts) is in src/game/objects/guest_pool.cpp.
//
// 53-bit precision: a float added to a float constant is one double operation, a float store one float rounding, and the truncation
// applies to what is left. Both functions place the guest relative to the pool building (m_DestObjectID) by the building's sprite direction 0..3.
namespace obl::guest_pool {

// Guest::m_ActionState values GetNextPoolPoint switches on; every other value picks the next tile of the pool.
constexpr int32_t kStateEnter = 0; // teleport to the pool's entry spot, face the pool
constexpr int32_t kStateSwimIn = 0x28; // MT_SWIM2SEAPOINT to the same spot
constexpr int32_t kStateFace = 0x29; // only face the pool
constexpr int32_t kStateLeave = 0x2a; // teleport to the exit spot
constexpr int32_t kStateWalkOut = 0x64; // MT_MOVE2POINT to the exit spot

constexpr uint32_t kTilePool = 0x40000000; // GridCell::m_flags bit of a pool tile
constexpr uint32_t kBuildingDirections = 4; // a direction past 3 selects no offset

struct Offset {
    float x, y;
};

// Entry and exit offsets from the building's position, by sprite direction.
constexpr Offset kEntryOffsets[kBuildingDirections] = {{-33.0f, -440.0f}, {25.0f, -420.0f}, {-96.0f, -354.0f}, {102.0f, -362.0f}};
constexpr Offset kExitOffsets[kBuildingDirections] = {{15.0f, -502.0f}, {-18.0f, -486.0f}, {-144.0f, -328.0f}, {147.0f, -332.0f}};

inline bool KnownDirection(int32_t direction)
{
    return static_cast<uint32_t>(direction) < kBuildingDirections;
}

struct Point {
    float x, y;
};

// A teleport target: both sums are stored as float.
inline Point TeleportPoint(float buildingX, float buildingY, const Offset& offset)
{
    return {static_cast<float>(static_cast<double>(buildingX) + offset.x), static_cast<float>(static_cast<double>(buildingY) + offset.y)};
}

// A posted goal: x is truncated straight from the double sum (not rounded to float), y after its float store. A direction past 3 keeps the building's position.
inline guest_walk::Goal PostedGoal(float buildingX, float buildingY, int32_t direction, const Offset (&offsets)[kBuildingDirections])
{
    if (!KnownDirection(direction)) return guest_walk::TruncatedGoal(buildingX, buildingY);
    const Offset& offset = offsets[direction];
    return {FloatToIntWrap(static_cast<double>(buildingX) + offset.x),
            FloatToIntWrap(static_cast<float>(static_cast<double>(buildingY) + offset.y))};
}

// The sprite direction of a guest facing the pool: `base - 2 * buildingDirection` in 32-bit arithmetic, then the signed remainder by 8
// (C's `%`).
inline int32_t Facing(int32_t base, int32_t buildingDirection)
{
    const uint32_t sum = static_cast<uint32_t>(base) - 2u * static_cast<uint32_t>(buildingDirection);
    return static_cast<int32_t>(sum) % 8;
}
constexpr int32_t kPoolFacingBase = 9;
// The slide facing is `mode + (4 - direction) * 2`; the same value with a different base.
inline int32_t SlideFacing(int32_t mode, int32_t buildingDirection)
{
    return Facing(mode + 8, buildingDirection);
}

// The animation has `directions` (a byte) sprite directions; a facing at or past that count (compared as signed) becomes the last one.
inline int32_t ClampFacing(int32_t facing, uint8_t directions)
{
    return facing >= static_cast<int32_t>(directions) ? static_cast<int32_t>(directions) - 1 : facing;
}

// The next tile of the pool: of the eight neighbours of the guest's tile those with the pool flag are counted (a draw modulo the count picks the k-th
// one, counted from 1), then the walk continues in that direction while the tile stays a pool tile; the middle of the last one is the goal. False when no
// neighbour is a pool tile (no draw is made then). `rand` is the C runtime's, called once.
template<class Rand> bool NextPoolTile(const map::Grid& grid, uint32_t tileX, uint32_t tileY, Rand&& rand, grid_math::PathPoint& goal)
{
    grid_math::TileXY neighbours[8];
    bool pool[8];
    uint32_t count = 0;
    for (int d = 0; d < 8; ++d) {
        neighbours[d] = grid_math::Neighbour(grid.m_width, grid.m_height, tileX, tileY, d);
        pool[d] = (grid.Cell(neighbours[d].x, neighbours[d].y).m_flags & kTilePool) != 0;
        count += pool[d];
    }
    if (count == 0) return false;
    uint32_t remaining = static_cast<uint32_t>(rand()) % count + 1;
    int chosen = 0;
    for (int d = 0; remaining != 0; ++d) {
        if (!pool[d]) continue;
        --remaining;
        chosen = d;
    }
    grid_math::TileXY tile = neighbours[chosen];
    while ((grid.Cell(tile.x, tile.y).m_flags & kTilePool) != 0) {
        goal = grid_math::TileToWorldCenter(grid.m_tileSize, tile.x, tile.y);
        tile = grid_math::Neighbour(grid.m_width, grid.m_height, tile.x, tile.y, chosen);
    }
    return true;
}

// ---- ChooseFunSlideAction ---------------------------------------------------------------------------------------------

constexpr int32_t kSlideActions = 4; // the draw is `rand() & 3`
// The spot at the top of the slide by the draw and the building's direction, and the facing base of the draw.
constexpr Offset kSlideOffsets[kSlideActions][kBuildingDirections] = {
        {{-43.0f, -526.0f}, {-104.0f, -368.0f}, {173.0f, -434.0f}, {-26.0f, -584.0f}},
        {{-25.0f, -506.0f}, {-89.0f, -388.0f}, {157.0f, -452.0f}, {-36.0f, -564.0f}},
        {{-49.0f, -484.0f}, {-53.0f, -466.0f}, {35.0f, -464.0f}, {-59.0f, -518.0f}},
        {{108.0f, -414.0f}, {18.0f, -528.0f}, {9.0f, -564.0f}, {-141.0f, -444.0f}},
};
constexpr int32_t kSlideModes[kSlideActions] = {5, 5, 5, 6};
constexpr int32_t kSlideMessageType = 0x39; // the delayed message to the guest itself (its data stays 0, extra is 0x3b)
constexpr int32_t kSlideMessageMember14 = 0x3b;

// The delay of the follow-up message: 2 s after the first two draws, 4 s after the others.
inline float SlideDelay(int32_t draw)
{
    return draw <= 1 ? 2.0f : 4.0f;
}

// The sprite that replaces the guest's by its current one: Gail, Shaz and the heavy male guest sprite (compared without case) keep to themselves, any other guest gets Chip.
constexpr const char* kSlideSprites[4] = {"Gail_funpool", "Shaz_funpool", "Bob_funpool", "Chip_funpool"};

inline int SlideSpriteIndex(const char* current)
{
    if (ascii::EqualsNoCase(current, "Gail")) return 0;
    if (ascii::EqualsNoCase(current, "Shaz")) return 1;
    return ascii::EqualsNoCase(current, "Fat_Bloke") ? 2 : 3;
}

// The palette index of the new sprite: the old one, or the last palette when the new sprite has fewer (compared as signed).
inline int32_t SlidePalette(int32_t previous, int32_t paletteCount)
{
    return previous >= paletteCount ? paletteCount - 1 : previous;
}

// ---- SetupFunPoolSwim -------------------------------------------------------------------------------------------------
// The guest's m_ActionState says what to set up: 1..4 swap the funpool sprite for a swimming one, put the guest at the foot of the slide (spot by state and
// building direction) and send it swimming to the landing spot; 5 only turns and puts it on the landing spot; 6 puts it on another spot and sends it to a
// footprint point of the building; any other state does nothing (state 0 is read as if it were one of 1..4 but with offsets the code never set, so it is
// left alone).

constexpr int32_t kSwimLanes = 4; // the states 1..kSwimLanes swap the sprite
constexpr int32_t kSwimStateLanding = 5; // turn (15 - 2 * direction) and teleport to the landing spot, no message
constexpr int32_t kSwimStateFootprint = 6; // teleport to kSwimFootprintStart, then MT_MOVE2POINT to the footprint point
constexpr int32_t kSwimFacingBase = 15;

inline bool SwimSwapsSprite(int32_t state)
{
    return state >= 1 && state <= kSwimLanes;
}

// The sprite that replaces the guest's funpool one, by its current name (compared without case): Gail, Shaz and Bob keep to themselves, any other guest gets
// The athletic male guest sprite (Bob's funpool appearance becomes the heavy male sprite).
constexpr const char* kSwimSprites[4] = {"Gail", "Shaz", "Fat_bloke", "Fit_Bloke"};

inline int SwimSpriteIndex(const char* current)
{
    if (ascii::EqualsNoCase(current, kSlideSprites[0])) return 0;
    if (ascii::EqualsNoCase(current, kSlideSprites[1])) return 1;
    return ascii::EqualsNoCase(current, kSlideSprites[2]) ? 2 : 3;
}

// The spot the guest is put on at the foot of the slide, by the state (1..4) and the building's direction, and the facing base of the state (5, 5, 5, 6).
constexpr Offset kSwimStartOffsets[kSwimLanes][kBuildingDirections] = {
        {{-63.0f, -532.0f}, {-106.0f, -360.0f}, {190.0f, -444.0f}, {-20.0f, -626.0f}},
        {{-55.0f, -516.0f}, {-90.0f, -382.0f}, {174.0f, -464.0f}, {-38.0f, -594.0f}},
        {{8.0f, -446.0f}, {-21.0f, -428.0f}, {73.0f, -504.0f}, {-100.0f, -560.0f}},
        {{52.0f, -412.0f}, {17.0f, -474.0f}, {63.0f, -568.0f}, {-141.0f, -502.0f}},
};
constexpr int32_t kSwimStartModes[kSwimLanes] = {5, 5, 5, 6};
// The landing spot: where states 1..4 send the guest and state 5 puts it.
constexpr Offset kSwimLandingOffsets[kBuildingDirections] = {{-132.0f, -476.0f}, {-73.0f, -278.0f}, {251.0f, -458.0f}, {-52.0f, -654.0f}};
// State 6: the spot the guest is put on, and the offset of the footprint point it is then sent to.
constexpr Offset kSwimFootprintStart[kBuildingDirections] = {{-171.0f, -532.0f}, {-119.0f, -258.0f}, {300.0f, -424.0f}, {-2.0f, -728.0f}};
constexpr float kSwimFootprintOffsetX = -222.0f;
constexpr float kSwimFootprintOffsetY = -576.0f;

} // namespace obl::guest_pool
