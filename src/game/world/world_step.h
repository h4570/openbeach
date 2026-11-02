#pragma once

#include "game/world/world_state.h"

namespace obl::world {

// World operations: world::Update (World*) from `src/game/world/world_update_logic.h`. Every step
// it calls stays the function behind its address, so the proof is the order unit tests plus the world-hash goldens.

// The entry points our callers use; each counts
// `<name>` like the other entry points.
void Update(world_data::World& world);

} // namespace obl::world
