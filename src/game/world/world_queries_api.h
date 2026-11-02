#pragma once

#include "game/world/world_state.h"

namespace obl::world {

// Registers the test drivers of the `world.*` query counters (the queries themselves are `World` members, world_queries.cpp).
bool ConfigureWorldQueryDrivers();

} // namespace obl::world
