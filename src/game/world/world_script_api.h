#pragma once

// The script API of the world scripts: the built-in functions (`Function <name>;`) and the values of conditions the engine
// implements, each with the number the parser stores and the world script machine dispatches on (world_control_logic.h, world_script.cpp).
// The names are the tokens of the scripts; the parser compares them ignoring ASCII case. Messages come from
// scripts/world_vocabulary.json of the pack.

#include "game/world/world_script_parse.h"

#include <vector>

namespace obl::world_script_parse {

// Sorted by number.
const std::vector<NamedId>& EngineFunctions();
const std::vector<NamedId>& EngineValues();

} // namespace obl::world_script_parse
