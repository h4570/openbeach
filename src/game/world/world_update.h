#pragma once

#include "game/world/world_state.h"

namespace obl::world {

// Steps of world::Update that need no game objects: world::RollDailyStats (
// `src/game/world/world_daily_stats.h`), world::UpdateBuildingTotals and world::SwitchOffConsumers (
// `src/game/world/world_power.h`; the building-level BuildingShedPower is a separate function) and world::UpdateDayCycle (
// `src/game/world/world_day_cycle.h`).
bool RegisterWorldUpdateDriver();
void UpdateBuildingTotalsDirect(void* world);

// The entry points our callers use; each counts
// `<name>` like the other entry points.
void UpdateBuildingTotals(layout::World& world);
void UpdateDayCycle(layout::World& world);

} // namespace obl::world
