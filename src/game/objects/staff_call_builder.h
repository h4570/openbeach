#pragma once

#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_staff.h"

#include <algorithm>

namespace obl::staff::call {

template<class Env> void BuilderCallFunction(object::Builder& employee, int32_t id, Env& env)
{
    switch (id) {
    case 0xb: employee.m_visible = 0; return;
    case 0xc: employee.m_visible = 1; return;
    case 0x5c: employee.m_lastTaskTime = env.DayTime(); return;
    case 0x5d: env.FindNearestValidTile(); return;
    case 0x5e:
    case 0x5f: return;
    case 0x60: env.CheckForFemaleGuests(); return;
    case 0x61: env.Whistling() = 0; return;
    case 0x62: env.PushThought(0x20); return;
    case 0x64: env.RemoveCostPoint(employee.m_DestObjectID); return;
    case 0x65: env.FreeJob(employee.m_id); return;
    case 0x66: env.FreeJobNoBuildCheck(employee.m_id); return;
    case 0x67:
    case 0x6a: env.ReallocateBuilders(); return;
    case 0x68: {
        const int32_t remaining = std::max(0, FloatToIntWrap(static_cast<float>(env.DayLength() / 24 - employee.m_IdleTimePerHour)));
        auto* object = env.Object(employee.m_DestObjectID);
        if (!object) return;
        messages::Message message{};
        message.sender = employee.m_id;
        message.receiver = employee.m_id;
        message.type = 0x39;
        message.extra = env.HasDemolishJob(object) ? 100 : 99;
        // The multiply/subtract is evaluated without an intermediate float store.
        const float delay =
                static_cast<float>(static_cast<double>(-static_cast<float>(remaining)) * env.Rand() * static_cast<double>(3.051851e-05f) +
                        static_cast<float>(remaining));
        env.PostDelayed(message, delay);
        return;
    }
    case 0x69:
        employee.m_RequiredFeedback = 0;
        employee.m_FeedbackStatus &= ~2;
        return;
    case 0xae: employee.m_waitingForDeath = 1; return;
    case 0xb2: env.ClearDelayed(employee.m_id); return;
    default: env.BaseCall(id); return;
    }
}

} // namespace obl::staff::call
