#pragma once

#include "game/world/world_state.h"

namespace obl::world {

// Builds a world: `new World`, then the construction steps of world_construct*.h in their fixed order. The subsystem
// constructors, the script host and the GUI calls go through their modules. The game context's world is the new one from the
// first step on (constructors read it back); world::Destroy (world_lifecycle.h) frees it.
layout::World* Create(ui::Cursor* cursor, ui::Input* input, const char* scenarioId, bool loadingSave);

} // namespace obl::world
