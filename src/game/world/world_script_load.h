#pragma once
#include "core/char16.h"
#include "game/world/world_script_parse.h"
#include "game/objects/script_types.h"
#include "game/tables/world_params_tables.h"
#include "game/world/world_script_types.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace obl::world {
struct ModWorldScriptFile {
    std::string modId;
    std::string scriptName; // filename inside scripts/world/, e.g. "daily_bonus.wsc"
    std::filesystem::path path;
};

bool ParseWorldScriptIds(std::string_view json, world_script_parse::Vocabulary& out, std::string& error);
// The catalog's `Weather` modifier words, as Parse reads them.
void AddCatalogModifiers(const tables::WorldParams& params, world_script_parse::Vocabulary& out);
bool LoadWorldScriptVocabulary(const Text16& gameDirectory);
// Discovers `scripts/world/*.wsc` across active world mods in load order (sorted by filename within each mod).
std::vector<ModWorldScriptFile> DiscoverModWorldScripts();
// Start-up check: parses and validates every active mod's `scripts/world/*.wsc` before any world exists. False after a
// `FatalError:` line naming the mod, the file and the reason (a refused call, a missing STATE_WORLD_STARTUP, a parse error).
bool CheckModWorldScripts(std::vector<std::string>* errors = nullptr);
// Loads and validates one mod world script into a constructed WorldScript (with the live WorldParams beers and modifiers).
bool LoadModWorldScript(world_script::WorldScript& script, const ModWorldScriptFile& file, std::string& error);
// What the last WorldScriptLoad returned (= the size in bytes of the script's .ble file; WorldScriptHost::LoadScript passes it on, ConstructWorld
// turns it into the loading-bar fraction) and whether a call happened at all.
bool GetLastWorldScriptLoadSize(uint32_t& size);

// Load (returns the .ble size), construct and destruct; the entry points forward to them.
uint32_t WorldScriptLoad(world_script::WorldScript& script, const char* path, const char* logicalPath);
world_script::WorldScript* WorldScriptConstruct(world_script::WorldScript& script);
void WorldScriptDestruct(world_script::WorldScript& script);
} // namespace obl::world
