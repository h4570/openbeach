#pragma once

#include "game/game/game_host.h"
#include "game/ui/renderer_types.h"

namespace obl::ui {
// ConstructRenderer/DestructRenderer: game/game/game_host.h.
// Called after the game destroyed its renderer (the display-contract test checks DirectDraw use there).
void SetRendererDestroyedObserver(void (*observer)());
} // namespace obl::ui
