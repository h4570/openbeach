#pragma once

// Identifier tables of the object-script parser: STATE_* and MT_* from scripts/object_vocabulary.json of the pack
// (importer/src/readers/script_vocabulary.cpp) merged with active mods' scripts/object_vocabulary.patch.json, plus
// the functions and values from the engine's script API (script_program_api.h). Pure logic (no game memory, no logging),
// so the unit tests can run it without the game.

#include "game/objects/script_program.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace obl::object {

// Returns false and sets `error` (with `out` cleared) on any format or validation error.
bool ParseScriptIds(std::string_view json, script_program::Vocabulary& out, std::string& error);

// Loads the pack's `scripts/object_vocabulary.json` merged with active mods' `scripts/object_vocabulary.patch.json`
// in load order, plus the engine's functions and values. Returns false and sets `error` on failure or name collision.
bool LoadMergedVocabulary(
        const std::filesystem::path& packDirectory, std::span<const std::string> mods, script_program::Vocabulary& out, std::string& error);

// Parses every `scripts/object/*.obs` shipped by the active mods (in load order, sorted by filename) against
// `vocabulary` in strict mode. Returns false with `error` naming the mod, file and line on any failure.
bool ValidateModObjectScripts(const std::filesystem::path& packDirectory, std::span<const std::string> mods,
        const script_program::Vocabulary& vocabulary, std::string& error);

} // namespace obl::object
