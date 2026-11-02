#pragma once

#include "game/world/world_state.h"

namespace obl::world {

// World operations: world::UpdateObjects, world::ProcessDeletedObjects
// and world::OnObjectDeleting, `src/game/world/world_objects_logic.h`. The calls into the objects (vtable `Update` and
// `OnExitState`) and the managers (object_manager::DeleteObject, the GUI and ferry managers, the buildings' thought lists)
// and feedback::Feedback::InvalidateObject (the facade) are called, not reimplemented here; the loops are verified by unit tests of the order and by the world-hash goldens.

// The entry points our callers use; each counts
// `<name>` like the other entry points.
void UpdateObjects(world_data::World& world);
void ProcessDeletedObjects(world_data::World& world);

} // namespace obl::world
