#pragma once
#include "core/char16.h"
#include "game/world/world_script_parse.h"
#include "game/objects/script_types.h"
#include "game/tables/world_params_tables.h"
#include "game/world/world_script_types.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace obl::world {
bool ParseWorldScriptIds(std::string_view json, world_script_parse::Vocabulary& out, std::string& error);
// The catalog's `Weather` modifier words, as Parse reads them.
void AddCatalogModifiers(const tables::WorldParams& params, world_script_parse::Vocabulary& out);
bool LoadWorldScriptVocabulary(const Text16& gameDirectory);
// What the last WorldScriptLoad returned (= the size in bytes of the script's .ble file; WorldScriptHost::LoadScript passes it on, ConstructWorld
// turns it into the loading-bar fraction) and whether a call happened at all.
bool GetLastWorldScriptLoadSize(uint32_t& size);

// Load (returns the .ble size), construct and destruct; the entry points forward to them.
uint32_t WorldScriptLoad(world_script::WorldScript& script, const char* path);
world_script::WorldScript* WorldScriptConstruct(world_script::WorldScript& script);
void WorldScriptDestruct(world_script::WorldScript& script);
} // namespace obl::world
