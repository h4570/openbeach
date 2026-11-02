#pragma once

#include "game/messages/message_state.h"
#include "game/world/world_state.h"

namespace obl::world {

// The message fan-out of the World class: world::PostMessageToHumans,
// world::PostMessageToWorldClass, world::PostMessageToBuildings and world::PostMessageToClass,
// `src/game/world/world_post_message.h`. Group 2 (the small control methods) lives in world_control.cpp. No indirection is left; the
// registration only sets the `call-drive` drivers.
bool RegisterWorldMessageDrivers();

// The entry points our callers use; each counts
// `<name>`.
void PostMessageToHumans(layout::World& world, messages::Message& message);
void PostMessageToWorldClass(layout::World& world, messages::Message& message, int cls);
void PostMessageToBuildings(layout::World& world, messages::Message& message);
void PostMessageToClass(messages::Handler* handler, int cls, messages::Message& message);

} // namespace obl::world
