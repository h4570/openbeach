#pragma once

#include <cstdint>

namespace obl::object {

struct Staff;
}

namespace obl::object::staff_facade {

// Staff queries: FindWalkablePointNear, FindWanderPos,
// SetHome and ReduceSelectionToHover, in `src/game/objects/staff_lookup.h`. The six profession HasState methods
// functions are reached through the member overrides of the staff classes only. Each facade takes the member pointer first, as the
// member call did, and keeps its call counter.
bool FindWalkablePointNear(object::Staff* employee, float* pos, int dx, int dy);
void FindWanderPos(object::Staff* employee);
void SetHome(object::Staff* employee, uint32_t homeId);
void ReduceSelectionToHover(object::Staff* employee);

} // namespace obl::object::staff_facade
