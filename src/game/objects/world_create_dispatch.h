#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_create_params.h"
#include "game/tables/world_params_tables.h"
#include "game/objects/world_create_staff.h"

#include <cstdint>

// The world's CreateObjectFromMessage: `bool (World*, Message* request, const void* record)`.
// `request->data` is the object class (1..20, the `ObjectType` number): the dispatcher calls the world's matching Create factory with the
// record, except for class 19 (SnootyStaff), which has no factory and is built inline. A world that is paused (m_paused) is unpaused for the
// call, so the new object's first script step runs, and re-paused afterwards. If the object exists, the world answers the requester with
// MT_OBJECTCREATED (data = the new id) through the *global* message handler.
//   Entity* CreateObject(ObjectType, const void* record)  the factory of that class
//   Entity* CreateObjectByKey(const char* key, ObjectType) the manager's CreateObject, SetType, SetState, Teleport, SetHome: as in world_create_staff.h
//   void PostGlobal(messages::Message&)                           Post on g_MessageHandler
//   uint8_t& Paused()                                           World::m_paused
namespace obl::world_create {

inline messages::Message CreatedMessage(int requester, uint32_t objectId)
{
    messages::Message message{};
    message.sender = messages::kMessageReceiverWorld;
    message.receiver = requester;
    message.type = messages::MT_OBJECTCREATED;
    message.flags = 0;
    message.data = static_cast<int>(objectId);
    return message;
}

// SnootyStaff has the staff record but no lookup, no first message and (unlike the template) no state change: the dispatcher's reply is all that follows.
template<class Env> object::Base* CreateSnootyStaff(Env& env, const object::StaffCreateParams& record)
{
    object::Base* object = env.CreateObjectByKey(record.m_key, object::ObjectType::SnootyStaff);
    env.SetType(object, object::ObjectType::SnootyStaff);
    env.SetState(object, 0);
    env.Teleport(object, record.m_pos[0], record.m_pos[1], record.m_pos[2]);
    env.SetHome(object, record.m_homeId);
    return object;
}

template<class Env> bool CreateObjectFromMessage(Env& env, const messages::Message& request, const void* record)
{
    const bool wasPaused = env.Paused() != 0;
    if (wasPaused) env.Paused() = 0;
    const uint32_t kind = static_cast<uint32_t>(request.data) - 1;
    // An unknown class returns false straight away, without putting the pause flag back (this exit skips the restore; tests pin it).
    if (kind > 19) return false;
    const auto type = static_cast<object::ObjectType>(kind + 1);
    object::Base* object = type == object::ObjectType::SnootyStaff
            ? CreateSnootyStaff(env, *static_cast<const object::StaffCreateParams*>(record))
            : env.CreateObject(type, record);
    if (wasPaused) env.Paused() = 1;
    if (object == nullptr) return false;
    messages::Message reply = CreatedMessage(request.sender, object->m_id);
    env.PostGlobal(reply);
    return true;
}

} // namespace obl::world_create
