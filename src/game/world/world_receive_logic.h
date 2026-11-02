#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/world/world_state.h"

#include <cstdint>

// The world as the receiver of a message:
// world::ReceiveMessage(message, name) the message handler's target for `receiver == -2`
// world::PostPlaceCommand(message, name) a world-bound command stamped with the world's time
// The calls out of the function (the object factory, the object manager, the world script, the message handler) stay behind an `Env`, so the order of
// events is unit-tested without the DLL. Each function lists the members of its Env.
namespace obl::world_receive {

constexpr int kKillObject = 0; // MT_KILLOBJECT
constexpr int kObjectCreated = 1; // MT_OBJECTCREATED: acknowledged, nothing else
constexpr int kStateChange = 3; // MT_STATECHANGE

// ---- world::ReceiveMessage ----------------------------------------------------------------------------------------------------
//   bool CreateObject(const Message&, const void* name)   world::CreateObjectFromMessage (its result is the function's)
//   Entity* GetObject(uint32_t id)                   the object manager's lookup by id
//   void RunOnExitList(WorldScript&)                     runs the exit handlers of the current state
//   void EnterState(WorldScript&, uint32_t state)        enters a state of the script
//   void DispatchMessage(WorldScript&, const Message&)   hands the message to the script's handlers (the record goes by value; its result is not looked at)
// A non-null `name` is a creation request. Otherwise by `type`: 0 marks the object `data` for removal (world::ProcessDeletedObjects does the rest;
// the write through GetObject's result is unchecked, so an unknown id would crash; here nothing happens), 1 is acknowledged, 3 leaves the
// current script state, remembers the new one in the world's script state field (also without a script) and enters it, anything else goes to the script's handlers
// of the current state. Every path except the creation request answers true.
template<class Env> bool ReceiveMessage(world::World& world, const messages::Message& message, const void* name, Env& env)
{
    if (name != nullptr) return env.CreateObject(message, name);
    switch (message.type) {
    case kKillObject:
        if (object::Base* object = env.GetObject(static_cast<uint32_t>(message.data))) object->m_waitingForDeath = 1;
        break;
    case kObjectCreated: break;
    case kStateChange:
        if (world.m_worldScript) env.RunOnExitList(*world.m_worldScript);
        world.m_scriptState = message.data;
        if (world.m_worldScript) env.EnterState(*world.m_worldScript, static_cast<uint32_t>(message.data));
        break;
    default:
        if (world.m_worldScript) env.DispatchMessage(*world.m_worldScript, message);
        break;
    }
    return true;
}

// ---- world::PostPlaceCommand --------------------------------------------------------------------------------------------------
//   void Dispatch(messages::Message&, const void* name)   the message handler's dispatch (null receiver, message, name)
// `time` is the time field of the global world, read before anything else. The receiver argument is null, so a command whose `receiver` is not the
// world is not delivered to any object (the callers always address the world).
template<class Env> void PostPlaceCommand(float time, messages::Message& command, const void* name, Env& env)
{
    command.time = time;
    env.Dispatch(command, name);
}

} // namespace obl::world_receive
