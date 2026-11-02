#include "core/char16.h"
#include "core/pack_path.h"
#include "game/world/world_script_types.h"
#include "game/world/world_script_load.h"
#include "game/game/diagnostics.h"

#include "game/game/game_accessors.h"
#include "game/world/world_params.h"
#include "game/tables/world_params_tables.h"
#include "core/file_io.h"
#include "core/log.h"
#include "formats/script_pack.h"
#include "core/call_counter.h"
#include "core/wide_path.h"

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
// Parses the converted scenario script of `path` (`Data/Scenario/<folder>/<name>.lgc`, read from the pack, script_pack.h). `size`
// is the length of the script text: the loading bar starts from it, as from the size of the `.ble` file.
bool TryParse(WorldScript& script, const char* path, const ws::Memory& memory, uint32_t& size, std::string& error)
{
    std::string text;
    if (!script_pack::ReadPackScript(g_packDirectory, path, script_pack::Kind::World, text, error)) return false;
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
uint32_t LoadScript(WorldScript* script, const char* path)
{
    if (!g_vocabularyOk) {
        game::FatalError("Cannot load scripts/world_vocabulary.json (%s). Import the game data again", g_error.c_str());
        return 0;
    }
    uint32_t size = 0;
    std::string error;
    g_loadCalls.Count();
    if (!TryParse(*script, path, kMemory, size, error)) game::FatalError("%s (%s)", error.c_str(), path);
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

uint32_t WorldScriptLoad(world_script::WorldScript& script, const char* path)
{
    return LoadScript(&script, path);
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
    std::ifstream file(pack_path::Resolve(g_packDirectory, "scripts/world_vocabulary.json"), std::ios::binary);
    std::string json{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    g_vocabularyOk = ParseWorldScriptIds(json, g_vocabulary, g_error);
    OBL_LOG("world script parser: vocabulary %s (%s)", g_vocabularyOk ? "loaded" : "unavailable", g_error.c_str());
    return true;
}
} // namespace obl::world
