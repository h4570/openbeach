#pragma once

#include "game/messages/message_state.h"
#include "game/world/world_state.h"

namespace obl::world {

// World operations: world::ReceiveMessage and
// world::PostPlaceCommand, `src/game/world/world_receive_logic.h`. The object factory, the object manager, the world script and the message
// handler are plain calls. Every caller goes through world::.

// world::PostPlaceCommand for our callers (counts world.post-place-command): the command stamped with the world's time goes to
// the current message handler (messages::message_facade::Current); the handler that was passed as `this` was always that one.
void PostPlaceCommand(messages::Message& command, const void* name);
// world::ReceiveMessage (counts world.receive-message).
bool ReceiveMessage(layout::World& world, messages::Message& message, const void* name);

} // namespace obl::world
