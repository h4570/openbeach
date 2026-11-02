#pragma once

#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"

#include <cstdint>

namespace obl::object {

// Staff text methods: the seven GetFeedbackText methods and PushThought, in
// src/game/objects/staff_thoughts.h; the classes' GetFeedbackText virtuals call them.

// PushThought run directly, for the SetVariable methods.
void RunPushThought(object::Staff& member, uint32_t flags);

// PushThought for the other callers (counted as `objects.staff-push-thought`).
void StaffPushThought(object::Staff& member, uint32_t flags);

} // namespace obl::object
