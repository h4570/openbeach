#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_create_params.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>

// The six staff factories of the World class that share one template: CreateServiceStaff,
// CreateLifeguard, CreateMechanic, CreateCleaner, CreateSecurityGuard and
// CreateRep. They differ only in the object class and the first state: look the key up, allocate the object through
// the object manager's CreateObject, set the class byte (SetType) and the state, place it, give it a home, and post
// "state change to 1" to it. The object itself and everything it runs on the way are the game's: the Env supplies the calls.
//   void LookUpStaff(const char* key)                  staff key lookup (an unknown key is a FatalError)
//   Entity* CreateObject(record, ObjectType)       the object manager's CreateObject(record, class)
//   void SetType(obj, ObjectType)                      the object's SetType
//   void SetState(obj, int)                            the object's SetState
//   void Teleport(obj, x, y, z)                        Teleport
//   void SetHome(obj, int)                             SetHome
//   void Post(messages::Message&)                        Post on the World's handler (synchronous: the script runs inside)
namespace obl::world_create {

struct StaffKind {
    object::ObjectType type;
    int firstState;
};

// Class numbers the staff factories assign (the order of CheckMissingStaffFeedback's classes is 11/9/8/12/7 plus the builder's 10).
constexpr StaffKind kServiceStaff{object::ObjectType::ServiceStaff, 0};
constexpr StaffKind kLifeguard{object::ObjectType::Lifeguard, 1}; // the only one that starts in state 1
constexpr StaffKind kMechanic{object::ObjectType::Mechanic, 0};
constexpr StaffKind kCleaner{object::ObjectType::Cleaner, 0};
constexpr StaffKind kSecurityGuard{object::ObjectType::SecurityGuard, 0};
constexpr StaffKind kRep{object::ObjectType::Rep, 0};

// The state every new staff member is sent to: the script's first working state.
constexpr int kStartedState = 1;

// The message of the factory: from the world (-2) to the new object, MT_STATECHANGE, `flags` 0, `data` = the state index. The last
// three words (extra, floatData, time) are zero (Post stamps time).
inline messages::Message StartMessage(uint32_t objectId)
{
    messages::Message message{};
    message.sender = messages::kMessageReceiverWorld;
    message.receiver = static_cast<int>(objectId);
    message.type = messages::MT_STATECHANGE;
    message.flags = 0;
    message.data = kStartedState;
    return message;
}

// The staff definition itself is not looked up: nothing here reads it.
template<class Env> object::Base* CreateStaff(Env& env, const object::StaffCreateParams& record, StaffKind kind)
{
    env.LookUpStaff(record.m_key);
    object::Base* object = env.CreateObject(record, kind.type);
    env.SetType(object, kind.type);
    env.SetState(object, kind.firstState);
    env.Teleport(object, record.m_pos[0], record.m_pos[1], record.m_pos[2]);
    env.SetHome(object, record.m_homeId);
    messages::Message message = StartMessage(object->m_id);
    env.Post(message);
    return object;
}

} // namespace obl::world_create
