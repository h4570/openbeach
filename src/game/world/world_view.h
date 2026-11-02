#pragma once

#include "game/world/world_state.h"

namespace obl::world {

// World operations: world::ApplyZoomRequest, world::UpdateCameraInput,
// world::UpdateHotkeys, world::Draw and world::DrawObjectOverlays, `src/game/world/world_view_logic.h`. All five are
// plain functions of World*. The calls into the input, GUI, map view, renderer and weather objects are plain calls.

// The entry points our callers use; each counts
// `<name>` like the other entry points.
void ApplyZoomRequest(layout::World& world);
void UpdateCameraInput(layout::World& world);
void UpdateHotkeys(layout::World& world);
void Draw(layout::World& world);
void DrawObjectOverlays(layout::World& world);

} // namespace obl::world
