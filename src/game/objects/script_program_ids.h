#pragma once

// Identifier tables of the object-script parser: STATE_* and MT_* from scripts/object_vocabulary.json of the pack
// (importer/src/readers/script_vocabulary.cpp), the functions and values from the engine's script API (script_program_api.h).
// Pure parsing only (no game memory, no logging), so the unit tests can run it without the game.
// Format: the identifier tables of the object script.

#include "game/objects/script_program.h"

#include <string>
#include <string_view>

namespace obl::object {

// Returns false and sets `error` (with `out` cleared) on any format or validation error.
bool ParseScriptIds(std::string_view json, script_program::Vocabulary& out, std::string& error);

} // namespace obl::object
