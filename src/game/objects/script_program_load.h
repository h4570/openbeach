#pragma once

#include "core/char16.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"

#include <string>

namespace obl::object {

// Object script loading (docs/architecture/objects.md): the vocabulary (states and messages from
// scripts/object_vocabulary.json of the pack, functions and values from script_program_api.h) is read once at startup, and each
// script is parsed from the pack by the parser of script_program.h.
bool LoadScriptProgramVocabulary(const Text16& gameDirectory);

// Construct, DestructStates and Load of one script; each counts its `objects.script-<name>` counter.
script::ScriptProgram* ScriptProgramConstruct(script::ScriptProgram& script);
void ScriptProgramDestructStates(script::ScriptProgram& script);
void ScriptProgramLoad(script::ScriptProgram& script, const char* path);

} // namespace obl::object
