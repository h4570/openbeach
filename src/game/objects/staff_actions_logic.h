#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_guest.h"

#include <cstring>
#include <cmath>

namespace obl::staff::actions {

inline messages::Message StateMessage(const object::Base& employee, uint32_t receiver, int32_t state, int32_t priority = 0)
{
    messages::Message message{};
    message.sender = employee.m_id;
    message.receiver = receiver;
    message.type = messages::MT_STATECHANGE;
    message.data = state;
    message.flags = priority;
    return message;
}

template<class T, class Env> void Leave(T& employee, Env& env)
{
    env.SetState(0x4c);
    env.EnterState(0x4c);
}

template<class Env> void ReturnJobAndLeave(object::Mechanic& employee, bool found, Env& env)
{
    const int32_t state = employee.m_state;
    if (state == 0x58 || state == 0x5d || ((state == 0x5b || (state == 0x83 && !employee.m_cantDoJob)) && found)) {
        const int32_t type = env.RepairType(employee.m_DestObjectID);
        if (type >= 0 && type <= 3) env.ReturnRepair(employee.m_DestObjectID, type);
    }
    Leave(employee, env);
}

template<class Env> void ReleaseGuestAndLeave(object::Rep& employee, const uint32_t* guestId, bool sent, Env& env)
{
    if (employee.m_state == 9 || ((employee.m_state == 8 || employee.m_state == 0x83) && sent)) {
        const auto* object = env.Object(static_cast<int32_t>(*guestId));
        if (object->m_type == object::ObjectType::Guest) env.Post(StateMessage(employee, *guestId, 0xc));
    }
    Leave(employee, env);
}

template<class Env> void LifeguardLeave(object::Lifeguard& employee, Env& env)
{
    if (!employee.m_saving) {
        Leave(employee, env);
    } else if (employee.m_ActionState != 0 && employee.m_ActionState != 1) {
        employee.m_RescueThenDeath = 1;
    } else {
        static_cast<object::Guest*>(env.Object(employee.m_saving_id))->m_ActionState = 2;
        env.Post(StateMessage(employee, employee.m_id, 0x4c));
    }
}

enum class Profession { Builder, Cleaner, Lifeguard, Rep };

// The buffers and pointers share storage (ReturnAnimation can be the input).
inline void CopyAnimation(char* target, const char* source)
{
    if (target != source) std::memmove(target, source, std::strlen(source) + 1);
}

template<Profession P, class T, class Env>
float PlayAnimation(T& employee, const char* animation, bool reverse, int32_t endState, bool loop, const char* finish, Env& env)
{
    if constexpr (P == Profession::Lifeguard || P == Profession::Rep) {
        if (std::strcmp(animation, "MessData") == 0)
            animation = env.MessageAnimation();
        else if (std::strcmp(animation, "ReturnAnimation") == 0)
            animation = employee.m_ReturnAnimation;
        env.AnimType(reverse ? 1 : (loop ? 0 : 2));
        if (!loop) {
            CopyAnimation(employee.m_ReturnAnimation, finish ? finish : animation);
            employee.m_LeaveState = endState == -1 ? employee.m_state : endState;
            if constexpr (P == Profession::Lifeguard) {
                if (std::strcmp(employee.m_ReturnAnimation, animation) != 0 && employee.m_state != 0x15)
                    env.Send(StateMessage(employee, employee.m_id, 0x55));
            } else {
                if (employee.m_state != employee.m_LeaveState || finish) env.Post(StateMessage(employee, employee.m_id, 10));
            }
        }
    }
    float duration = static_cast<float>(env.SetAnimation(animation));
    // An unordered (NaN) duration is treated like zero.
    if (duration == 0.0f || std::isnan(duration)) duration = 1.0f;
    if constexpr (P == Profession::Cleaner) env.AnimType(reverse ? 1 : (loop ? 0 : 2));
    if constexpr (P == Profession::Lifeguard || P == Profession::Rep) CopyAnimation(employee.m_CurrAnimation, animation);
    if (!loop && endState != -1) {
        int32_t next = endState;
        if constexpr (P == Profession::Lifeguard || P == Profession::Rep) next = employee.m_LeaveState;
        env.PostDelayed(StateMessage(employee, employee.m_id, next, 1), duration);
    }
    return duration;
}

template<class Env> void SendMessage(object::Staff& employee, messages::Message message, Env& env)
{
    if (auto* script = object::StaffScript(employee)) env.ScriptSend(script, message);
}

template<class Env> void SendMessageDelayed(object::Staff& employee, messages::Message message, float delay, Env& env)
{
    if (auto* script = object::StaffScript(employee)) env.ScriptSendDelayed(script, message, delay);
}

} // namespace obl::staff::actions
