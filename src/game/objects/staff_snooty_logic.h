#pragma once

#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"
#include "game/objects/staff_service_receive.h"

#include <cstdint>

// SnootyStaff uses the d_stand_snooty animation. Only
// the world's CreateObjectFromMessage builds one and no script of the shipped game asks for it. Its own methods: Construct (its constructor's part,
// after Staff's), ReceiveMessage, OnEnterState and SetHome; Update, Save and Load are the
// service staff's. Unlike the service staff it posts through MessageHandler directly, not through its Send and SendDelayed.
//
// Env:
//   env.CreateSprite(name)                                     sprite creation from the bank: the sprite is named like the object
//   env.Stand(sprite)                                          starts d_stand_snooty at the current game time without a position
//   env.PlayAnim(nameWord)                                     starts the named animation at the staff position and current game time; returns its duration
//   env.Post(message) / env.PostDelayed(message, s)            the message handler's Post / PostDelayed
//   env.Movement(message)                                      the human ReceiveMovementMessage, its answer
namespace obl::staff::snooty {

constexpr float kMaxForce = 20.0f, kMaxVel = 60.0f;
constexpr int32_t kPlayOnce = 2; // SpriteInstance::m_animType

template<class Env> void Construct(object::SnootyStaff& employee, const char* name, Env& env)
{
    employee.m_sprite = env.CreateSprite(name);
    env.Stand(employee.m_sprite);
    employee.m_maxVel = kMaxVel;
    employee.m_maxForce = kMaxForce;
}

inline messages::Message ToHome(const object::SnootyStaff& employee, int type)
{
    messages::Message message{};
    message.sender = static_cast<int32_t>(employee.m_id);
    message.receiver = employee.m_HomeID;
    message.type = type;
    return message;
}

// MT_MOVEIMHERE is passed on to the home building. MT_PLAYANIMATION plays the animation whose name data points to, once, and (unless flags is
// set) tells home MT_GUEST_SERVED when it ends. The answer is the byte left in the argument's slot, as in the service staff's method.
template<class Env> uint8_t ReceiveMessage(object::SnootyStaff& employee, const messages::Message& message, Env& env)
{
    switch (message.type) {
    case messages::MT_MOVEIMHERE: env.Post(ToHome(employee, messages::MT_MOVEIMHERE)); return service::LowByte(message);
    case messages::MT_PLAYANIMATION: {
        const float length = static_cast<float>(env.PlayAnim(message.data));
        static_cast<ui::SpriteInstance*>(employee.m_sprite)->m_animType = kPlayOnce;
        if (message.flags == 0) env.PostDelayed(ToHome(employee, messages::MT_GUEST_SERVED), length);
        return service::LowByte(length);
    }
    default: return env.Movement(message);
    }
}

inline bool OnEnterState(uint32_t)
{
    return true;
}

template<class Env> void SetHome(object::SnootyStaff& employee, int32_t homeId, Env& env)
{
    employee.m_HomeID = homeId;
    env.Post(ToHome(employee, messages::MT_IM_READY));
}

} // namespace obl::staff::snooty
