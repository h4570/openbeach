#pragma once

#include "game/messages/message_state.h"
#include "game/world/world_state.h"

namespace obl::world {

// The message fan-out of the World class: world::PostMessageToHumans,
// world::PostMessageToWorldClass, world::PostMessageToBuildings and world::PostMessageToClass,
// `src/game/world/world_post_message.h`. The small control methods live in world_control.cpp. No indirection is left; the
// registration only sets the `call-drive` drivers.
bool RegisterWorldMessageDrivers();

// The entry points our callers use; each counts
// `<name>`.
void PostMessageToHumans(world_data::World& world, messages::Message& message);
void PostMessageToWorldClass(world_data::World& world, messages::Message& message, int cls);
void PostMessageToBuildings(world_data::World& world, messages::Message& message);
void PostMessageToClass(messages::Handler* handler, int cls, messages::Message& message);

} // namespace obl::world
