#pragma once

#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"

namespace obl::staff::call {

inline void RotateDirection(ui::SpriteInstance& sprite, uint32_t delta)
{
    int32_t direction = static_cast<int32_t>(static_cast<uint32_t>(sprite.m_direction) + delta) % 8;
    const int32_t count = sprite.m_sprite->m_anims[sprite.m_animIndex]->m_directions;
    if (count <= direction) direction = count - 1;
    sprite.m_direction = direction;
}

template<class Env> void LifeguardCallFunction(object::Lifeguard& employee, int32_t id, Env& env)
{
    switch (id) {
    case 0xb: employee.m_visible = 0; return;
    case 0xc: employee.m_visible = 1; return;
    case 0x2f: env.HeadForShore(); return;
    case 0x7b: env.PushThought(0x20000000); return;
    case 0x88: {
        const auto* hotel = env.Hotel();
        messages::Message message{};
        message.sender = employee.m_id;
        message.receiver = employee.m_id;
        message.type = 9;
        message.data = hotel->m_id;
        env.Post(message);
        return;
    }
    case 0x89: env.MoveToTower(); return;
    case 0x8a: env.FindBeachPoint(); return;
    case 0x8b: env.Move2SeaEntry(); return;
    case 0x8c: return;
    case 0x8d: {
        auto* guest = env.Object(employee.m_saving_id);
        env.Teleport(guest, employee.m_WorldXYZ[0], employee.m_WorldXYZ[1], employee.m_WorldXYZ[2]);
        env.SetDirection(
                *static_cast<ui::SpriteInstance*>(guest->m_sprite), static_cast<const ui::SpriteInstance*>(employee.m_sprite)->m_direction);
        return;
    }
    case 0x8e: {
        const int32_t direction = static_cast<const ui::SpriteInstance*>(env.Object(employee.m_HomeID)->m_sprite)->m_direction;
        env.SetDirection(
                *static_cast<ui::SpriteInstance*>(employee.m_sprite), static_cast<int32_t>(7u - static_cast<uint32_t>(direction) * 2u));
        return;
    }
    case 0x8f: env.SetDrawPosBuild(); return;
    case 0x90: env.SetDrawPosServe(); return;
    case 0x91: env.FindBeach(); return;
    case 0x92:
        employee.m_GuestDrowned = 1;
        ++employee.m_NumDrowned;
        ++employee.m_timesCalledOut;
        employee.m_RequiredFeedback = 0;
        employee.m_FeedbackStatus &= ~2;
        return;
    case 0x93:
        ++employee.m_timesCalledOut;
        ++employee.m_NumSaves;
        env.PushThought(0x80);
        return;
    case 0x94: {
        const auto* guest = env.Object(employee.m_saving_id);
        employee.m_inMyArea = env.SeaArea(guest->m_WorldXYZ) == employee.m_InfluenceArea;
        return;
    }
    case 0x95: {
        const float x = employee.m_WorldXYZ[0], y = employee.m_WorldXYZ[1], z = employee.m_WorldXYZ[2];
        float moveX = x, moveY = y, guestX = x, guestY = y;
        auto& sprite = *static_cast<ui::SpriteInstance*>(employee.m_sprite);
        constexpr float rescuerOffset[8][2] = {{-9, -80}, {49, -68}, {77, -12}, {61, 48}, {9, 80}, {-49, 68}, {-77, 12}, {-61, -48}};
        constexpr float guestOffset[8][2] = {{6, -66}, {52, -44}, {67, 4}, {43, 52}, {-6, 66}, {-52, 44}, {-67, -4}, {-43, -52}};
        if (static_cast<uint32_t>(sprite.m_direction) < 8) {
            const int32_t direction = sprite.m_direction;
            moveX = static_cast<float>(static_cast<double>(x) + rescuerOffset[direction][0]);
            moveY = static_cast<float>(static_cast<double>(y) + rescuerOffset[direction][1]);
            guestX = static_cast<float>(static_cast<double>(x) + guestOffset[direction][0]);
            guestY = static_cast<float>(static_cast<double>(y) + guestOffset[direction][1]);
        }
        RotateDirection(sprite, 2);
        env.Teleport(&employee, moveX, moveY, z);
        auto* guest = env.Object(employee.m_saving_id);
        // The second pair of offsets is lost by the analysis; both positions derive from the rescuer's pre-move position.
        env.Teleport(guest, guestX, guestY, z);
        RotateDirection(*static_cast<ui::SpriteInstance*>(guest->m_sprite), 6);
        return;
    }
    case 0x96: env.Object(employee.m_saving_id)->m_visible = 0; return;
    case 0x97: env.Object(employee.m_saving_id)->m_visible = 1; return;
    case 0x98: env.Object(employee.m_saving_id); return;
    case 0xad:
        employee.m_RequiredFeedback = 0;
        employee.m_FeedbackStatus &= ~4;
        env.BaseCall(id);
        return;
    case 0xae: employee.m_waitingForDeath = 1; return;
    case 0xaf: env.Blink("LifeguardIcon", true); return;
    case 0xb0:
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        env.Select();
        env.SelectTab("LifeguardDialog", 2);
        break;
    case 0xb1: {
        employee.m_CurrentFeedback = employee.m_RequiredFeedback;
        employee.m_RequiredFeedback = 0;
        const char* key = env.FeedbackKey(employee.m_CurrentFeedback);
        if (key) env.AddFeedback(key, employee.m_id, 8);
        break;
    }
    default: env.BaseCall(id); return;
    }
    env.PushThought(employee.m_CurrentFeedback);
    env.Blink("LifeguardIcon", false);
}

} // namespace obl::staff::call
