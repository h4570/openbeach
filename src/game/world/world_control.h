#pragma once

#include "game/world/world_state.h"

namespace obl::world {

// World operations: world::SetGameSpeedLevel, world::CallScriptFunction
// and world::CheckMissingStaffFeedback, `src/game/world/world_control_logic.h`. The calls into other objects (MapView::LockCamera/
// UnlockCamera, FatalError, object_manager::CountByClass, game_parameters::GetInt) are plain calls; the won scenario
// goes through game_api::CompleteCurrentScenario.
//
// world::UpdateBuildingTotals forwards.
// It is watched by a call counter (`call-report <name>`, expected 0), so a path that reaches it fails the test.
bool RegisterWorldControlDrivers();

// The entry points our callers use.
// Each counts `<name>` like the other entry points.
// Set a temporary power-grid snapshot; the next totals update recomputes it.
void SetPowerTotals(world_data::World& world, float demand, float installed, float output);
// Enable or disable the world building wear model.
void SetBreakdownsAllowed(world_data::World& world, bool allowed);
// Set a free simulation multiplier without changing the selected speed level.
void SetGameSpeedFactor(world_data::World& world, float factor);
void SetGameSpeedLevel(world_data::World& world, int level);
void CallScriptFunction(world_data::World& world, int function);
void CheckMissingStaffFeedback(world_data::World& world);

} // namespace obl::world
