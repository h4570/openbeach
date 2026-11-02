#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_base.h"

#include <cstdint>

// ReceiveMessage of the base object: what every object class falls back to for a message it has no branch for
// (the moving and static objects' ReceiveMessage fall back to it as well). Only two types mean anything here:
//   MT_STATECHANGE (3): HasState(new), then OnExitState *before* changing the state, m_state = new, then
//       OnEnterState(new). A failing step ends the game (FatalError) and carries on if the call ever returned. Answers true.
//   MT_SETOWNER (4): m_owner = data, with a FatalError for data < 6 (the test reads that way round; tests pin it). Answers true.
// Every other type answers false and touches nothing. The Env supplies the three virtual calls and the four FatalErrors:
//   bool HasState(Entity&, uint32_t)     bool ExitState(Entity&)     bool EnterState(Entity&, uint32_t)
//   void Fatal(entity::Fatal, const Entity&, const Message&)
namespace obl::entity {

enum class Fatal { UnsupportedState, ExitState, EnterState, InvalidOwner };

inline constexpr uint32_t kFirstValidOwner = 6;

template<class Env> bool ReceiveMessage(object::Base& object, const messages::Message& message, Env& env)
{
    switch (message.type) {
    case messages::MT_STATECHANGE: {
        const uint32_t state = static_cast<uint32_t>(message.data);
        if (!env.HasState(object, state)) env.Fatal(Fatal::UnsupportedState, object, message);
        if (!env.ExitState(object)) env.Fatal(Fatal::ExitState, object, message);
        object.m_state = message.data;
        if (!env.EnterState(object, state)) env.Fatal(Fatal::EnterState, object, message);
        return true;
    }
    case messages::MT_SETOWNER:
        if (static_cast<uint32_t>(message.data) < kFirstValidOwner) env.Fatal(Fatal::InvalidOwner, object, message);
        object.m_owner = static_cast<uint8_t>(message.data);
        return true;
    default: return false;
    }
}

} // namespace obl::entity
