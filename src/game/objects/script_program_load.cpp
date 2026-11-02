#include "core/char16.h"
#include "core/pack_path.h"
#include "game/objects/script_program_load.h"
#include "game/game/diagnostics.h"

#include "game/game/game_accessors.h"
#include "game/objects/script_program_ids.h"
#include "core/file_io.h"
#include "core/log.h"
#include "game/objects/script_program.h"
#include "formats/script_pack.h"
#include "core/call_counter.h"
#include "core/wide_path.h"

#include <cstddef>
#include <array>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace obl::object {
using game::FatalError;
using game::GetContext;

namespace {

using script::ScriptProgram;
namespace os = script_program;

// `call-report objects.script-<name>`.
test::CallCounter g_load("objects.script-load");
test::CallCounter g_construct("objects.script-construct");
test::CallCounter g_destruct("objects.script-destruct-states");

os::Vocabulary g_vocabulary;
std::string g_loadError = "not loaded";
bool g_vocabularyOk = false;
std::filesystem::path g_packDirectory; // <game dir>/packs/base, where the converted scripts are

// Command lists own operator new blocks and release them during teardown.
const os::Memory kGameMemory = {[](size_t size) { return ::operator new(size); }, [](void* block) { ::operator delete(block); }};

using IfStack = std::array<script::ScriptCommand*, 10>;
IfStack& ParserStack()
{
    return GetContext().objectScriptIfStack;
}
uint8_t& ParserDepth()
{
    return GetContext().objectScriptIfDepth;
}

ScriptProgram* Construct(ScriptProgram* objectScript)
{
    os::Construct(*objectScript);
    ParserStack().fill(nullptr);
    ParserDepth() = 0;
    return objectScript;
}

bool LoadVocabulary(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        g_loadError = "file not found";
        return false;
    }
    std::string json{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return ParseScriptIds(json, g_vocabulary, g_loadError);
}

// Parses the converted script of `path` (`data/script/object/<name>.lgc`, read from the pack, script_pack.h) into `script`; false
// and `error` when the script is unusable (a fatal error). The parser reads size + 1 bytes: the text and the `.` terminator.
bool TryParse(ScriptProgram& script, const char* path, const os::Memory& memory, std::string& error)
{
    std::string text;
    if (!script_pack::ReadPackScript(g_packDirectory, path, script_pack::Kind::Object, text, error)) return false;
    text.push_back(static_cast<char>(file_io::kTerminator));
    try {
        os::Parse(script, text.data(), text.size() - 1, g_vocabulary, memory);
    } catch (const os::ParseError& failure) {
        error = failure.what();
        return false;
    }
    return true;
}

} // namespace

ScriptProgram* ScriptProgramConstruct(ScriptProgram& script)
{
    g_construct.Count();
    return Construct(&script);
}

void ScriptProgramDestructStates(ScriptProgram& script)
{
    g_destruct.Count();
    os::DestructStates(script, kGameMemory);
}

void ScriptProgramLoad(ScriptProgram& script, const char* path)
{
    if (!g_vocabularyOk) {
        FatalError("Cannot load scripts/object_vocabulary.json (%s).\r\nImport the game data again", g_loadError.c_str());
        return;
    }
    g_load.Count();
    std::string error;
    if (!TryParse(script, path, kGameMemory, error)) FatalError("%s (%s)", error.c_str(), path);
}

bool LoadScriptProgramVocabulary(const Text16& gameDirectory)
{
    g_packDirectory = WidePath(gameDirectory) / pack_path::Selected();
    g_vocabularyOk = LoadVocabulary(pack_path::Resolve(g_packDirectory, "scripts/object_vocabulary.json"));
    if (g_vocabularyOk)
        OBL_LOG("object script: %u states, %u messages, %u functions, %u values loaded", static_cast<unsigned>(g_vocabulary.states.size()),
                static_cast<unsigned>(g_vocabulary.messages.size()), static_cast<unsigned>(g_vocabulary.functions.size()),
                static_cast<unsigned>(g_vocabulary.values.size()));
    else
        OBL_LOG("object script: scripts/object_vocabulary.json not usable (%s) - the script parser cannot run", g_loadError.c_str());
    return true;
}

} // namespace obl::object
