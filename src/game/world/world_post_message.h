#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/objects/staff_classes.h"

#include <cstdint>

// world::PostMessageToClass, world::PostMessageToWorldClass, world::PostMessageToBuildings and
// world::PostMessageToHumans. All four are one loop: take the
// object list of a class once (object_manager::GetListByClass: pointer and length are read once, so objects the delivered scripts add
// later are not reached), store the receiver id (the object's id) in the caller's message record before every messages::Post. The
// record is shared, so it keeps the last receiver afterwards. The list is live memory, read again at every step: for
// classes 2 and 3 it is the object manager's own array, for the others the object manager's scratch buffer (filled by object_manager::FilterObjects),
// which every later GetListByClass call rewrites - a delivered script that asks for another class changes what the walk reads next, and
// the walk reads the same buffer.
//
// The object lists and the Post stay behind an `Env`:
//   List GetList(int cls)        object_manager::GetListByClass: objects and count of the class
//   void Post(messages::Message&)  messages::Post on the World's handler (delivered synchronously: scripts run inside the call)
namespace obl::world_post {

constexpr int kBuildingClass = 2;

struct List {
    object::Base* const* objects = nullptr;
    uint32_t count = 0;
};

template<class Env> void PostToClass(Env& env, int cls, messages::Message& message)
{
    const List list = env.GetList(cls);
    for (uint32_t i = 0; i < list.count; ++i) {
        message.receiver = static_cast<int>(list.objects[i]->m_id);
        env.Post(message);
    }
}

template<class Env> void PostToBuildings(Env& env, messages::Message& message)
{
    PostToClass(env, kBuildingClass, message);
}

template<class Env> void PostToHumans(Env& env, messages::Message& message)
{
    for (int cls : object::kHumanClasses) PostToClass(env, cls, message); // personnel, then guests
}

} // namespace obl::world_post
