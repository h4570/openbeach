#pragma once

#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_staff.h"

#include <algorithm>
#include <bit>

namespace obl::staff::call {

template<class Env> void RepCallFunction(object::Rep& employee, int32_t id, Env& env)
{
    switch (id) {
    case 0xb: employee.m_visible = 0; return;
    case 0xc: employee.m_visible = 1; return;
    case 0x7b: env.PushThought(0x20000000); return;
    case 0xa2: env.MoveToNextBuilding(); return;
    case 0xa3: env.LookForGuests(); return;
    case 0xa4: env.ArrivedAtGuest(); return;
    case 0xa5: {
        // No float store between the two multiplies; m_happinessBoostToday is a float accumulator despite its integer Save label.
        const int32_t time = std::max(1, FloatToIntWrap(static_cast<double>(employee.m_timeWithGuests) * static_cast<double>(0.8f) * 10.0));
        ++employee.m_GuestsTalkedToToday;
        employee.m_happinessBoostToday = std::bit_cast<int32_t>(
                static_cast<float>(static_cast<double>(time) + std::bit_cast<float>(employee.m_happinessBoostToday)));
        return;
    }
    case 0xa6: env.PushThought(0x20000); return;
    case 0xa7: {
        const auto& message = *static_cast<const messages::Message*>(employee.m_currentMessage);
        ++employee.m_counters[message.data][message.flags];
        if (message.flags == 2) return;
        employee.m_RequiredFeedback = 0;
        employee.m_FeedbackStatus = message.data;
        employee.m_GuestOpinionValue = message.flags;
        env.UpdateFeedback();
        return;
    }
    case 0xae: employee.m_waitingForDeath = 1; return;
    case 0xaf: env.Blink("RepIcon", true); return;
    case 0xb0:
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        env.Select();
        env.SelectTab("RepDialog", 2);
        break;
    case 0xb1: {
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        const char* key = env.FeedbackKey(employee.m_CurrentFeedback);
        if (key) env.AddFeedback(key, employee.m_id, 10);
        break;
    }
    default: env.BaseCall(id); return;
    }
    env.PushThought(employee.m_CurrentFeedback);
    env.Blink("RepIcon", false);
}

} // namespace obl::staff::call
