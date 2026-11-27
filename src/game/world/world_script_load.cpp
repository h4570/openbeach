#include "core/char16.h"
#include "core/pack_path.h"
#include "game/world/world_script_types.h"
#include "game/world/scenario_load.h"
#include "game/world/world_script_load.h"
#include "game/game/diagnostics.h"

#include "game/game/game_accessors.h"
#include "game/world/world_params.h"
#include "game/tables/world_params_tables.h"
#include "core/file_io.h"
#include "core/log.h"
#include "formats/data_patch.h"
#include "formats/mod_pack.h"
#include "formats/script_pack.h"
#include "core/call_counter.h"
#include "core/wide_path.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace obl::world {
namespace {
namespace ws = world_script_parse;
using script::ScriptCommand;
using world_script::WorldScript;
// The calls of the parser and the lifecycle.
test::CallCounter g_loadCalls("world.script-load");
test::CallCounter g_constructCalls("world.script-construct");
test::CallCounter g_destructCalls("world.script-destruct");
ws::Vocabulary g_vocabulary;
std::string g_error;
std::filesystem::path g_packDirectory; // <game dir>/packs/base, where the converted scripts are
bool g_vocabularyOk = false;
const ws::Memory kMemory{[](size_t bytes) { return ::operator new(bytes); }, [](void* block) { ::operator delete(block); }};

int BeerId(const char* name)
{
    return world_params::GetBeerId(game::GetWorldParams(), name);
}
// The loading bar starts from the length of the script text, as it did from the size of the `.ble` file.
bool TryParse(WorldScript& script, const char* logicalPath, const ws::Memory& memory, uint32_t& size, std::string& error)
{
    std::string text;
    if (!scenario_load::ReadWorldScript(g_packDirectory, logicalPath, text, error)) return false;
    size = static_cast<uint32_t>(text.size());
    text.push_back(static_cast<char>(file_io::kTerminator)); // the scanners read one byte past the text
    try {
        auto vocabulary = g_vocabulary;
        for (const auto& beer : game::GetWorldParams()->m_beers) vocabulary.beers.push_back({beer.m_id, beer.m_key});
        AddCatalogModifiers(*game::GetWorldParams(), vocabulary);
        ws::Parse(script, text.data(), size, vocabulary, memory, &BeerId);
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
    return true;
}
std::atomic<bool> g_loadSeen{false};
std::atomic<uint32_t> g_lastLoadSize{0};
uint32_t RecordLoadSize(uint32_t size)
{
    g_lastLoadSize = size;
    g_loadSeen = true;
    return size;
}
uint32_t LoadScript(WorldScript* script, const char* path, const char* logicalPath)
{
    if (!g_vocabularyOk) {
        game::FatalError("Cannot load scripts/world_vocabulary.json (%s). Import the game data again", g_error.c_str());
        return 0;
    }
    uint32_t size = 0;
    std::string error;
    g_loadCalls.Count();
    if (!TryParse(*script, logicalPath, kMemory, size, error)) game::FatalError("%s (%s)", error.c_str(), path);
    return RecordLoadSize(size);
}

// ---- Lifecycle: construct / destruct ---------------------------------------------------------------------
// The If stack of the parser is a game global the constructor clears (a quirk kept on purpose; only the parser reads it).
using IfStack = std::array<void*, 10>;
IfStack& ParserStack()
{
    return game::GetContext().worldScriptIfStack;
}

WorldScript* ConstructScript(WorldScript* script)
{
    g_constructCalls.Count();
    ws::Construct(*script);
    ParserStack().fill(nullptr);
    return script;
}
void DestructScript(WorldScript* script)
{
    g_destructCalls.Count();
    ws::Destruct(*script, kMemory);
}

} // namespace

uint32_t WorldScriptLoad(world_script::WorldScript& script, const char* path, const char* logicalPath)
{
    return LoadScript(&script, path, logicalPath);
}

world_script::WorldScript* WorldScriptConstruct(world_script::WorldScript& script)
{
    return ConstructScript(&script);
}

void WorldScriptDestruct(world_script::WorldScript& script)
{
    DestructScript(&script);
}
bool GetLastWorldScriptLoadSize(uint32_t& size)
{
    size = g_lastLoadSize;
    return g_loadSeen;
}
bool LoadWorldScriptVocabulary(const Text16& gameDirectory)
{
    g_packDirectory = WidePath(gameDirectory) / pack_path::Selected();
    data_patch::Input file(pack_path::Resolve(g_packDirectory, "scripts/world_vocabulary.json"));
    std::string json{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    g_vocabularyOk = ParseWorldScriptIds(json, g_vocabulary, g_error);
    OBL_LOG("world script parser: vocabulary %s (%s)", g_vocabularyOk ? "loaded" : "unavailable", g_error.c_str());
    return true;
}

std::vector<ModWorldScriptFile> DiscoverModWorldScripts()
{
    std::vector<ModWorldScriptFile> result;
    for (const mod_pack::ActiveMod& mod : mod_pack::Active()) {
        if (mod.manifest.scope != mod_pack::Scope::World) continue;
        const std::filesystem::path dir = mod.directory / "scripts" / "world";
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        std::vector<std::pair<std::string, std::filesystem::path>> files;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".wsc") continue;
            files.emplace_back(it->path().filename().string(), it->path());
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [name, path] : files) result.push_back({mod.manifest.id, std::move(name), std::move(path)});
    }
    return result;
}

namespace {
bool ParseAndValidateModScript(WorldScript& script, const ModWorldScriptFile& file, const ws::Vocabulary& vocabulary, ws::BeerId beerId,
        bool validateModifiers, std::string& error)
{
    std::string text;
    if (!script_pack::ReadScript(file.path, script_pack::Kind::World, text, error)) {
        error = "mod \"" + file.modId + "\" (scripts/world/" + file.scriptName + "): " + error;
        return false;
    }
    const uint32_t size = static_cast<uint32_t>(text.size());
    text.push_back(static_cast<char>(file_io::kTerminator));
    try {
        ws::Parse(script, text.data(), size, vocabulary, kMemory, beerId, nullptr, validateModifiers);
    } catch (const std::exception& failure) {
        error = "mod \"" + file.modId + "\" (scripts/world/" + file.scriptName + "): " + failure.what();
        return false;
    }
    std::string reason;
    if (!ws::ValidateModWorldScript(script, reason)) {
        error = "mod \"" + file.modId + "\" (scripts/world/" + file.scriptName + "): " + reason;
        return false;
    }
    return true;
}

int StartupBeerId(const char*)
{
    return 0;
}
} // namespace

bool CheckModWorldScripts(std::vector<std::string>* errors)
{
    const std::vector<ModWorldScriptFile> files = DiscoverModWorldScripts();
    if (files.empty()) return true;
    const auto report = [&](const std::string& error) {
        if (errors)
            errors->push_back(error);
        else
            OBL_LOG("FatalError: %s", error.c_str());
    };
    if (!g_vocabularyOk) {
        report("Cannot load scripts/world_vocabulary.json (" + g_error + ")");
        return false;
    }
    bool valid = true;
    for (const auto& file : files) {
        WorldScript script{};
        ws::Construct(script);
        std::string error;
        const bool parsed = ParseAndValidateModScript(script, file, g_vocabulary, &StartupBeerId, false, error);
        ws::Destroy(script, kMemory);
        if (parsed) continue;
        report(error);
        valid = false;
        if (!errors) break;
    }
    return valid;
}

bool LoadModWorldScript(world_script::WorldScript& script, const ModWorldScriptFile& file, std::string& error)
{
    if (!g_vocabularyOk) {
        error = "Cannot load scripts/world_vocabulary.json (" + g_error + ")";
        return false;
    }
    auto vocabulary = g_vocabulary;
    for (const auto& beer : game::GetWorldParams()->m_beers) vocabulary.beers.push_back({beer.m_id, beer.m_key});
    AddCatalogModifiers(*game::GetWorldParams(), vocabulary);
    return ParseAndValidateModScript(script, file, vocabulary, &BeerId, true, error);
}
} // namespace obl::world
