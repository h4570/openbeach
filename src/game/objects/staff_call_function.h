#pragma once

#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"

#include <cstdint>

// The staff CallFunction (takes the function id), the script functions (`Function <name>;`
// of a `*Logic.ble` state) every profession shares. A profession's own dispatcher handles its ids and passes the rest here; an id this one does not
// know ends the game with a FatalError (and returns, if the error handler does).
//
// Env:
//   env.FindWanderPos()        FindWanderPos
//   env.PushThought(bits)      PushThought
//   env.DirectToObject(id)     DirectToObject of the member
//   env.Fatal(text)            FatalError
namespace obl::staff::call {

enum : int32_t {
    FindWanderPos = 0x76,
    ThinkIdle = 0x79, // thought 0x20000000 (STANDIDLE)
    ThinkDirected = 0x7a, // thought 0x40000000 (DIRECTED)
    SpendIdleTime = 0xa8, // one unit of m_IdleTimePerHour
    ClearDirected = 0xa9,
    FaceFront = 0xaa, // sprite direction 4
    GoToDirected = 0xab, // DirectToObject(m_GotoObject)
    ThinkStartWork = 0xac, // thought 0x1000000
    ThinkEndWork = 0xad, // thought 0x2000000, m_endedShift
};
constexpr uint32_t kThoughtIdle = 0x20000000, kThoughtDirected = 0x40000000, kThoughtStartWork = 0x1000000, kThoughtEndWork = 0x2000000;
constexpr int32_t kFrontDirection = 4;
inline constexpr char kInvalidFunction[] = "A script called a function staff members do not have";

template<class Env> void CallFunction(object::Staff& employee, int32_t id, Env& env)
{
    switch (id) {
    case FindWanderPos: env.FindWanderPos(); return;
    case ThinkIdle: env.PushThought(kThoughtIdle); return;
    case ThinkDirected: env.PushThought(kThoughtDirected); return;
    case SpendIdleTime:
        if (employee.m_IdleTimePerHour != 0) --employee.m_IdleTimePerHour;
        return;
    case ClearDirected:
        employee.m_DirectStaff = 0;
        employee.m_GotoObject = -1;
        return;
    case FaceFront: {
        // The last direction of the current animation when it has no fifth.
        auto& sprite = *static_cast<ui::SpriteInstance*>(employee.m_sprite);
        const int32_t directions = sprite.m_sprite->m_anims[sprite.m_animIndex]->m_directions;
        sprite.m_direction = directions > kFrontDirection ? kFrontDirection : directions - 1;
        return;
    }
    case GoToDirected: env.DirectToObject(employee.m_GotoObject); return;
    case ThinkStartWork: env.PushThought(kThoughtStartWork); return;
    case ThinkEndWork:
        env.PushThought(kThoughtEndWork);
        employee.m_endedShift = 1;
        return;
    default: env.Fatal(kInvalidFunction); return;
    }
}

} // namespace obl::staff::call
