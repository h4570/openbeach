#pragma once

#include "game/objects/object_base.h"

namespace obl::object {

// The guest's constructor and destructor (guest life cycle, docs/architecture/objects.md) live in src/game/objects/guest_lifecycle_driver.cpp.
void RegisterGuestLifecycleDriver();
} // namespace obl::object
