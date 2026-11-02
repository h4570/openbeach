#pragma once

#include "game/ui/sprite.h"
#include "game/objects/object_create_params.h"
#include "game/tables/world_params_tables.h"
#include "script_message.h"
#include "game/objects/world_create_staff.h"

#include <cstdint>

// CreateGuest (World, GuestCreateParams*, returns the object). Env: `const tables::GuestDef& GetGuest(key)`
// (GetGuestId + GetGuestDefinition), `CreateObjectByKey`, `SetType`, `Teleport`, `SetSprite(object, name)` (stores `m_sprite`), `uint8_t GlobalKeyline()`,
// `Post` (the world's handler), `PostDelayed(message, delay)` (the global handler), `int Rand()`, `int& GuestCount()`. Allocation, constructor, sprite
// bank and the messages stay game functions.
namespace obl::world_create {

// The guest's own start: STATECHANGE with state 0 (the staff and props templates send state 1; there is no SetState call).
constexpr int kGuestStartState = 0;

// Both messages are from the world (-2), `flags` 0; the words after data
// are zero.
inline messages::Message GuestMessage(uint32_t objectId, int type, int data)
{
    messages::Message message{};
    message.sender = messages::kMessageReceiverWorld;
    message.receiver = static_cast<int>(objectId);
    message.type = type;
    message.data = data;
    return message;
}

// The second message waits `2 * rand() / 32767` seconds (computed with one 53-bit
// rounding per operation, one float store).
inline float OpinionDelay(int draw)
{
    const double step = static_cast<double>(draw) * static_cast<double>(script::kRandUnit);
    return static_cast<float>(step + step);
}

template<class Env> object::Base* CreateGuest(Env& env, const object::GuestCreateParams& record)
{
    const tables::GuestDef& def = env.GetGuest(record.m_key);
    auto* object = env.CreateObjectByKey(record.m_key, object::ObjectType::Guest);
    env.SetType(object, object::ObjectType::Guest);
    env.Teleport(object, record.m_pos[0], record.m_pos[1], record.m_pos[2]);
    env.SetSprite(object, def.m_Sprite.c_str());
    auto* sprite = static_cast<ui::SpriteInstance*>(object->m_sprite);
    if (sprite->m_animIndex != def.m_DefSprite) {
        sprite->m_animIndex = def.m_DefSprite;
        sprite->m_frameIndex = 0;
    }
    sprite->m_keyline = env.GlobalKeyline();
    messages::Message start = GuestMessage(object->m_id, messages::MT_STATECHANGE, kGuestStartState);
    env.Post(start);
    // The id is read again after the delivery, the draw comes after it (the guest script may draw too), then the delayed message and the counter.
    messages::Message opinions = GuestMessage(object->m_id, messages::MT_UPDATEOPINIONS, 0);
    const float delay = OpinionDelay(env.Rand());
    env.PostDelayed(opinions, delay);
    ++env.GuestCount();
    return object;
}

} // namespace obl::world_create
