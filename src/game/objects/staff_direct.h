#pragma once

#include <cstdint>

namespace obl::object {

struct Staff;
} // namespace obl::object

namespace obl::object {

// The player's orders to staff (src/game/objects/staff_direct_logic.h, docs/architecture/objects.md): the staff DirectToPoint/DirectToTarget and the six
// professions' DirectToObject. An order that cannot be handled (the representative given an object that does not exist) ends the game with
// fatal::UnhandledInput.
// The six DirectToObject are the professions' virtuals; DirectToPoint/DirectToTarget are plain entries counted like the handlers
// (`objects.staff-direct`), called by the command executor and the world-state test driver without an installed address.
void StaffDirectToPoint(object::Staff& member, float* point);
void StaffDirectToTarget(object::Staff& member, int32_t target);

} // namespace obl::object
