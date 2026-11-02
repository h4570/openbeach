#pragma once

// The script API of the object scripts: the built-in functions (`Function <name>;`) and the values (variables and
// constants) the engine implements, each with the number the parser stores and the object code dispatches on
// (guest_script.cpp, entity_lifecycle.h, *_script_values.h). The names are the tokens of the scripts; the parser
// compares them ignoring ASCII case. States and messages come from scripts/object_vocabulary.json of the pack.

#include "game/objects/script_program.h"

#include <vector>

namespace obl::script_program {

// Sorted by number; a function may have two names (139: Move2SeaEntryPoint, FnMove2SeaEntry).
const std::vector<NamedId>& EngineFunctions();
const std::vector<NamedId>& EngineValues();

} // namespace obl::script_program
