#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/world/world_state.h"

namespace obl::world {

// World operations: the factories the world script and the event manager reach through
// world::CreateObjectFromMessage. The bodies are the templates in `src/game/objects/world_create_*.h`; allocation
// (the object manager's create), the object's own methods and the delivery of the first message stay behind their Envs.
// Each entry point counts `<name>`, and the dispatcher
// calls the factories directly, so the per-factory counters still count.
bool CreateObjectFromMessage(layout::World& world, const messages::Message& request, const void* record);
// The factory of one class (the record type the class's world::Create* took); nullptr for a class without a factory.
object::Base* CreateObject(layout::World& world, object::ObjectType type, const void* record);

} // namespace obl::world
