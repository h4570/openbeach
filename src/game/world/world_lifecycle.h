#pragma once

#include "game/world/world_state.h"

namespace obl::world {

// World teardown and load; the stream save/load is in world_save.h. Each entry point
// counts its `<name>` counter.
// The world of a queued obl.save load (obl_load in save_load.h): LoadWorld's fields and every subsystem from the document,
// no stream (save-switch.md).
void LoadFromSave(layout::World& world);
// DestroyWorld and the delete of its caller: the destructor frees the subsystems, then the game context forgets the world.
void Destroy(layout::World* world);

} // namespace obl::world
