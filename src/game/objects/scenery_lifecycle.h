#pragma once

#include "game/objects/object_base.h"

namespace obl::object {

struct Scenery;
} // namespace obl::object

namespace obl::object {

// Scenery initialization, scenery teardown and scenery deletion
// (docs/architecture/objects.md); the logic is `src/game/objects/scenery_construction.h`.
// Restoring saved scenery's steps around the base, for the obl.save reader (scenery_serial.cpp, save-switch.md): the draw routine picked
// from the constructed sprite and name, and the footprint cells joined after the position is loaded.
void ScenerySetDraw(object::Scenery& scenery);
void SceneryJoinFootprint(object::Scenery& scenery);

} // namespace obl::object
