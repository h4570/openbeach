#pragma once

#include "core/float_to_int.h"
#include "game/objects/guest_draw.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"

#include <cstdint>

// The small duties of staff: the shift of a member, the builder's number, a sprite's direction clamp and the cleaner's walk to a
// bin.
//   IsOnShift: env.DayTime() (the world's time of day).
//   MoveToBin: env.Object(id) (the object manager's lookup), env.Post(message) (the message handler).
namespace obl::staff_duties {

using object::Staff;

inline void SetShift(Staff& member, int32_t start, int32_t end)
{
    member.m_StartTime = start;
    member.m_EndTime = end;
}

// The hour is truncated towards zero and compared as unsigned, so a shift that wraps past midnight (start > end) is on outside [end, start).
template<class Env> bool IsOnShift(const Staff& member, Env& env)
{
    const uint32_t hour = static_cast<uint32_t>(FloatToIntWrap(env.DayTime()));
    const uint32_t start = static_cast<uint32_t>(member.m_StartTime), end = static_cast<uint32_t>(member.m_EndTime);
    if (start > end) return hour >= start || hour < end;
    return hour >= start && hour < end;
}

inline void SetBuilderNumber(object::Builder& builder, int32_t number)
{
    builder.m_BuilderNumber = number;
}

// A direction past the animation's last one becomes the last one.
inline void SetDirection(ui::SpriteInstance& sprite, int32_t direction)
{
    const int32_t directions = static_cast<int32_t>(guest_draw::Anim(sprite).m_directions);
    if (direction >= directions) direction = directions - 1;
    sprite.m_direction = direction;
}

// The bin's world position moved to the spot of the cleaner's side of it, by the bin's direction (0..7; anything else leaves it): the point is written
// (y, x) - the two stay swapped, the bins rely on it - and a message of type 8 from the cleaner to itself carries the truncated point. The second coordinate stays
// a double until it is truncated.
template<class Env> void MoveToBin(int32_t objectId, const object::Base& cleaner, float* point, Env& env)
{
    const object::Base* bin = env.Object(objectId);
    if (!bin) return;
    const double x = bin->m_WorldXYZ[0];
    const double y = bin->m_WorldXYZ[1];
    double first = y, second = x;
    switch (static_cast<const ui::SpriteInstance*>(bin->m_sprite)->m_direction) {
    case 0: first = y + 31.0; break;
    case 1:
        second = x - 22.0;
        first = y + 22.0;
        break;
    case 2: second = x - 31.0; break;
    case 3:
    case 5:
        second = x + 22.0;
        first = y - 22.0;
        break;
    case 4: first = y - 31.0; break;
    case 6: second = x + 31.0; break;
    case 7:
        second = x + 22.0;
        first = y + 22.0;
        break;
    default: break;
    }
    point[0] = static_cast<float>(
            first); // the first coordinate went through a float store before it was written; the second one is stored once
    point[1] = static_cast<float>(second);
    messages::Message message{};
    message.sender = static_cast<int>(cleaner.m_id);
    message.receiver = static_cast<int>(cleaner.m_id);
    message.type = 8;
    message.flags = FloatToIntWrap(second);
    message.data = FloatToIntWrap(static_cast<double>(static_cast<float>(first)));
    env.Post(message);
}

} // namespace obl::staff_duties
