#pragma once

#include "game/map/path_data.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"

#include <cstdint>
#include <cstring>

// Mover accessors and setters (group A of docs/architecture/objects.md): Get/SetMovePosition, Get/SetVelocity, SetMoveTarget,
// SetMoving and the route cursor tile lookup preserve the raw 32-bit float representations
// (a signalling NaN must not be quieted by a float copy).
namespace obl::moving {

inline void CopyBits(float* to, const float* from)
{
    std::memcpy(to, from, sizeof(float));
}

inline float* GetMovePosition(const object::Mover& object, float* out)
{
    CopyBits(&out[0], &object.m_vPosition[0]);
    CopyBits(&out[1], &object.m_vPosition[1]);
    return out;
}

inline float* GetVelocity(const object::Mover& object, float* out)
{
    CopyBits(&out[0], &object.m_vVelocity[0]);
    CopyBits(&out[1], &object.m_vVelocity[1]);
    return out;
}

inline void SetWords(float (&field)[2], uint32_t x, uint32_t y)
{
    std::memcpy(&field[0], &x, sizeof(x));
    std::memcpy(&field[1], &y, sizeof(y));
}

inline void SetMovePosition(object::Mover& object, uint32_t x, uint32_t y)
{
    SetWords(object.m_vPosition, x, y);
}
inline void SetVelocity(object::Mover& object, uint32_t x, uint32_t y)
{
    SetWords(object.m_vVelocity, x, y);
}
inline void SetMoveTarget(object::Mover& object, uint32_t x, uint32_t y)
{
    SetWords(object.m_vTarget, x, y);
}

// The flag is the low byte of the stack argument; callers normally pass 0 or 1, but nothing normalises it.
inline void SetMoving(object::Mover& object, uint32_t moving)
{
    object.m_moving = static_cast<uint8_t>(moving);
}

// The (x, y) pair of the cursor's current step. No bounds check: an index outside the array reads whatever is there (callers stay in range);
// the address arithmetic is 32-bit in the game process.
inline uint32_t* CursorTile(const path::RouteCursor& cursor, uint32_t* out)
{
    const uint32_t* pair = path::StepTile(cursor);
    out[0] = pair[0];
    out[1] = pair[1];
    return out;
}

} // namespace obl::moving
