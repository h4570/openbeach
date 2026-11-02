#pragma once

#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"

namespace obl::staff::call {

template<class Env> void MechanicCallFunction(object::Mechanic& employee, int32_t id, Env& env)
{
    switch (id) {
    case 0xb: employee.m_visible = 0; return;
    case 0xc: employee.m_visible = 1; return;
    case 0x74: employee.m_lastTaskTime = env.DayTime(); return;
    case 0x7b: env.PushThought(0x20000000); return;
    case 0x99: {
        employee.m_repairType = 2;
        employee.m_DestObjectID = -1;
        if ((employee.m_JobMask & 4) != 0 && env.Rand() % 100 > 80) employee.m_DestObjectID = env.FindRepairJob(3, employee.m_repairType);
        if (employee.m_repairType == 2) {
            if ((employee.m_JobMask & 3) == 3) {
                const int32_t first = env.Rand() % 100 < 51 ? 1 : 2;
                employee.m_DestObjectID = env.FindRepairJob(first, employee.m_repairType);
                if (employee.m_DestObjectID == -1) employee.m_DestObjectID = env.FindRepairJob(first == 1 ? 2 : 1, employee.m_repairType);
            } else if ((employee.m_JobMask & 1) != 0)
                employee.m_DestObjectID = env.FindRepairJob(2, employee.m_repairType);
            else if ((employee.m_JobMask & 2) != 0)
                employee.m_DestObjectID = env.FindRepairJob(1, employee.m_repairType);
            if ((employee.m_JobMask & 1) != 0 && employee.m_DestObjectID == -1)
                employee.m_DestObjectID = env.FindRepairJob(0, employee.m_repairType);
        }
        if (employee.m_DestObjectID == -1) {
            employee.m_foundRepair = 0;
            return;
        }
        employee.m_foundRepair = 1;
        employee.m_lastTaskTime = env.DayTime();
        return;
    }
    case 0x9a: {
        const int32_t job = env.BetterJob(employee.m_DestObjectID, employee.m_repairType);
        if (job == -1) {
            employee.m_foundBetterJob = 0;
            return;
        }
        employee.m_foundBetterJob = 1;
        employee.m_DestObjectID = job;
        return;
    }
    case 0x9b:
        if ((employee.m_JobMask & 4) != 0) {
            employee.m_DestObjectID = env.WateringJob();
            if (employee.m_DestObjectID != -1) {
                employee.m_foundRepair = 1;
                employee.m_lastTaskTime = env.DayTime();
                employee.m_repairType = 3;
                return;
            }
        }
        employee.m_foundRepair = 0;
        return;
    case 0x9d: {
        const auto* object = env.Object(employee.m_DestObjectID);
        env.ReturnJob(employee.m_DestObjectID, object->m_type == object::ObjectType::Building ? 2 : 1);
        return;
    }
    case 0x9e:
        switch (employee.m_repairType) {
        case 0: env.PushThought(4); break;
        case 1: env.PushThought(0x10); break;
        case 2: env.PushThought(8); break;
        case 3: env.PushThought(0x20); break;
        }
        env.ReleaseJob(employee.m_DestObjectID);
        employee.m_DestObjectID = -1;
        return;
    case 0x9f:
        env.StartedJob(employee.m_DestObjectID);
        switch (employee.m_repairType) {
        case 0: ++employee.m_inspectionsToday; return;
        case 1: ++employee.m_vandalsToday; return;
        case 2: ++employee.m_repairsToday; return;
        case 3: ++employee.m_wateredToday; return;
        default: return;
        }
    case 0xa0: {
        const auto* object = env.Object(employee.m_DestObjectID);
        employee.m_WateringPos[1] = static_cast<float>(static_cast<double>(object->m_WorldXYZ[1]) - 22);
        employee.m_WateringPos[0] = static_cast<float>(static_cast<double>(object->m_WorldXYZ[0]) + 34);
        return;
    }
    case 0xa1: {
        env.Object(employee.m_DestObjectID);
        auto& sprite = *static_cast<ui::SpriteInstance*>(employee.m_sprite);
        const int32_t directions = sprite.m_sprite->m_anims[sprite.m_animIndex]->m_directions;
        sprite.m_direction = directions < 6 ? directions - 1 : 5;
        return;
    }
    case 0xad:
        employee.m_RequiredFeedback = 0;
        employee.m_FeedbackStatus &= ~2;
        env.BaseCall(id);
        return;
    case 0xae: employee.m_waitingForDeath = 1; return;
    case 0xaf: env.Blink("MechanicIcon", true); return;
    case 0xb0:
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        env.Select();
        env.SelectTab("MechanicDialog", 2);
        break;
    case 0xb1: {
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        const char* key = env.FeedbackKey(employee.m_CurrentFeedback);
        if (key) env.AddFeedback(key, employee.m_id, 9);
        break;
    }
    default: env.BaseCall(id); return;
    }
    env.PushThought(employee.m_CurrentFeedback);
    env.Blink("MechanicIcon", false);
}

} // namespace obl::staff::call
