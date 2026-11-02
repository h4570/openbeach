#pragma once

#include "game/objects/object_staff.h"

namespace obl::staff::call {

template<class Env> void SecurityCallFunction(object::SecurityGuard& employee, int32_t id, Env& env)
{
    switch (id) {
    case 0xb: employee.m_visible = 0; return;
    case 0xc: employee.m_visible = 1; return;
    case 0x7b: env.PushThought(0x20000000); return;
    case 0x7e: employee.m_misdemeanourFound = env.LookForOffender(employee.m_MisdemeanourPos); return;
    case 0x7f:
        ++employee.m_numSeriousFinesToday;
        employee.m_fineTotalToday += employee.m_seriousFineAmount;
        env.CollectFine("SERIOUS");
        return;
    case 0x80:
        ++employee.m_numMinorFinesToday;
        employee.m_fineTotalToday += employee.m_minorFineAmount;
        env.CollectFine("MINOR");
        return;
    case 0x81: ++employee.m_numJailed; return;
    case 0x82: ++employee.m_numCautioned; return;
    case 0x83: {
        float point[3];
        const float* serve = env.ServePosition(employee.m_HomeID, point);
        employee.m_FineOffsetX = static_cast<float>(static_cast<double>(serve[0]) + 22.0);
        employee.m_FineOffsetY = serve[1];
        return;
    }
    case 0x85: env.FaceObject(employee.m_offenderId); return;
    case 0x86: env.ChooseAction(); return;
    case 0x87: env.AddFeedback("SECURITY_GUARD_HAS_DETAINED", employee.m_id, 8); return;
    case 0xae: employee.m_waitingForDeath = 1; return;
    case 0xaf: env.Blink("SecurityIcon", true); return;
    case 0xb0:
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        env.Select();
        env.SelectTab("SecurityDialog", 2);
        break;
    case 0xb1: {
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        const char* key = env.FeedbackKey(employee.m_CurrentFeedback);
        if (key) env.AddFeedback(key, employee.m_id, 13);
        break;
    }
    default: env.BaseCall(id); return;
    }
    env.PushThought(employee.m_CurrentFeedback);
    // Enabling and disabling the blink use different icon names.
    env.Blink("SecurityGuardIcon", false);
}

} // namespace obl::staff::call
