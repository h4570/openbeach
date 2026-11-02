#pragma once

#include "game/messages/message_state.h"
#include "core/ascii_case.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// ServiceStaff (bartenders and cooks) implements `HasState`,
// `OnEnterState` (OnEnterState, answer true) and `Update` (Update, no arguments, answer true). Construct/Destruct are in staff_construction.h.
// Env of OnEnterState:
//   env.Post(message)                   post a message (Send; the words after data are
//                                       zero)
//   env.PostDelayed(message, seconds)   post a delayed message (SendDelayed)
// Env of Update:
//   env.HumanUpdate()                   the human Update of the member; false makes the game end with a FatalError
//   env.Fatal(name)                     FatalError("Update of staff member \"%s\" failed", name)
namespace obl::staff::service {

// The script states a service member has: 0 and 1 (start-up) and the DJ's booth state 0x4b.
constexpr uint32_t kIdleState = 0, kStartState = 1, kDjState = 0x4b;
constexpr float kLookAroundDelay = 20.0f;

inline bool HasState(uint32_t state)
{
    return state <= kStartState || state == kDjState;
}

inline messages::Message SelfMessage(const object::ServiceStaff& employee, int type, int data)
{
    messages::Message message{};
    message.sender = static_cast<int32_t>(employee.m_id);
    message.receiver = static_cast<int32_t>(employee.m_id);
    message.type = type;
    message.data = data;
    return message;
}

// State 0 sets state 1 on itself; state 1 sets the DJ state on itself, tells the home building (data 1 when the member's name has "DJ" in it) and lets the
// barman look around 20 seconds later. Any other state does nothing.
template<class Env> bool EnterState(object::ServiceStaff& employee, uint32_t state, Env& env)
{
    if (state == kIdleState) {
        env.Post(SelfMessage(employee, messages::MT_STATECHANGE, kStartState));
    } else if (state == kStartState) {
        env.Post(SelfMessage(employee, messages::MT_STATECHANGE, kDjState));
        messages::Message started = SelfMessage(employee, messages::MT_STAFF_CREATED, ascii::ContainsNoCase(employee.m_Name, "DJ") ? 1 : 0);
        started.receiver = employee.m_HomeID;
        env.Post(started);
        if (ascii::EqualsNoCase(employee.m_staffDefinition->m_Sprite, "BARMAN"))
            env.PostDelayed(SelfMessage(employee, messages::MT_LOOK_AROUND, 0), kLookAroundDelay);
    }
    return true;
}

template<class Env> bool Update(object::ServiceStaff& employee, Env& env)
{
    if (!env.HumanUpdate()) env.Fatal(employee.m_Name);
    return true;
}

} // namespace obl::staff::service
