#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_building.h"

#include <algorithm>

namespace obl::staff::call {

template<class Env> void CleanerCallFunction(object::Cleaner& employee, int32_t id, Env& env)
{
    switch (id) {
    case 0xb: employee.m_visible = 0; return;
    case 0xc: employee.m_visible = 1; return;
    case 0x6e: {
        int32_t type = 0;
        employee.m_DestObjectID = env.FindJob(true, type);
        if (employee.m_DestObjectID != -1) {
            employee.m_urgentClean = 1;
            employee.m_UrgentType = type;
        }
        return;
    }
    case 0x6f: {
        env.CancelJob(employee.m_DestObjectID);
        auto* object = env.Object(employee.m_DestObjectID);
        if (object) {
            messages::Message message{};
            message.sender = object->m_id;
            message.receiver = object->m_id;
            message.type = 3;
            message.data = 3;
            env.Post(message);
            env.SetMoving(object, false);
            env.SetCalledCleaning(object, false);
            object->m_waitingForDeath = 1;
        }
        ++employee.m_LitterToday;
        env.PushThought(0x40);
        return;
    }
    case 0x70: env.MoveToBin(employee.m_DestObjectID, employee.m_BinPos); return;
    case 0x71: env.FaceSameDirection(employee.m_DestObjectID); return;
    case 0x72: {
        auto* object = env.Object(employee.m_DestObjectID);
        env.SetCleanerClaimed(object, false);
        env.SetMoving(object, false);
        env.SetCalledCleaning(object, false);
        env.SetBinFill(object, 0);
        env.PushThought(8);
        env.CancelJob(employee.m_DestObjectID);
        ++employee.m_BinsToday;
        return;
    }
    case 0x73: {
        auto* object = env.Object(employee.m_DestObjectID);
        auto& building = *static_cast<object::Building*>(object);
        const int32_t count = building.m_NumTimesCleanedToday + 1;
        building.m_VisitsSinceCleaned = 0;
        building.m_NumTimesCleanedToday = count;
        if (count > 2) {
            employee.m_BusyToilet = employee.m_DestObjectID;
            employee.m_RequiredFeedback = 0;
            employee.m_FeedbackStatus &= ~2;
        }
        env.SetCleanerClaimed(object, false);
        env.SetMoving(object, false);
        env.SetCalledCleaning(object, false);
        env.PushThought(0x10);
        env.CancelJob(employee.m_DestObjectID);
        ++employee.m_ToiletsToday;
        return;
    }
    case 0x74: employee.m_lastTaskTime = env.DayTime(); return;
    case 0x75: env.FaceObjectIfPresent(employee.m_DestObjectID); return;
    case 0x77: env.DidNotClean(employee.m_DestObjectID); return;
    case 0x78: env.StartedCleaning(employee.m_DestObjectID); return;
    case 0x7b: env.PushThought(0x20000000); return;
    case 0x7c: {
        const auto* building = static_cast<object::Building*>(env.Object(employee.m_DestObjectID));
        const int32_t delay = std::clamp(building->m_VisitsSinceCleaned / 2, 2, 15);
        messages::Message message{};
        message.sender = employee.m_id;
        message.receiver = employee.m_id;
        message.type = 0x39;
        message.extra = 0x73;
        env.PostDelayed(message, static_cast<float>(delay));
        return;
    }
    case 0x7d: {
        employee.m_litterFound = 0;
        employee.m_binFound = 0;
        employee.m_toiletFound = 0;
        int32_t type = 0;
        employee.m_DestObjectID = env.FindJob(false, type);
        if (employee.m_DestObjectID == -1) return;
        employee.m_lastTaskTime = env.DayTime();
        if (type == 0)
            employee.m_litterFound = 1;
        else if (type == 1)
            employee.m_binFound = 1;
        else if (type == 2)
            employee.m_toiletFound = 1;
        return;
    }
    case 0xad:
        employee.m_RequiredFeedback = 0;
        employee.m_FeedbackStatus &= ~4;
        env.BaseCall(id);
        return;
    case 0xae: employee.m_waitingForDeath = 1; return;
    case 0xaf: env.Blink("CleanerIcon", true); return;
    case 0xb0:
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        env.Select();
        env.SelectTab("CleanerDialog", 2);
        break;
    case 0xb1: {
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        const char* key = env.FeedbackKey(employee.m_CurrentFeedback);
        if (key) env.AddFeedback(key, employee.m_id, 12);
        break;
    }
    default: env.BaseCall(id); return;
    }
    env.PushThought(employee.m_CurrentFeedback);
    env.Blink("CleanerIcon", false);
}

} // namespace obl::staff::call
