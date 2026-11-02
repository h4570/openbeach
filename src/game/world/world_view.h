#pragma once

#include "game/world/world_state.h"

namespace obl::world {

// World operations: world::ApplyZoomRequest, world::UpdateCameraInput,
// world::UpdateHotkeys, world::Draw and world::DrawObjectOverlays, `src/game/world/world_view_logic.h`. All five are
// plain functions of World*. The calls into the input, GUI, map view, renderer and weather objects are plain calls.

// The entry points our callers use; each counts
// `<name>` like the other entry points.
void ApplyZoomRequest(world_data::World& world);
void UpdateCameraInput(world_data::World& world);
void UpdateHotkeys(world_data::World& world);
void Draw(world_data::World& world);
void DrawObjectOverlays(world_data::World& world);

} // namespace obl::world
