#include "core/char16.h"
#include "game/world/world_script_host.h"
// A check of the world scripts: scripts-dump reads the live game only through
// the native script records (src/game/objects/script_types.h, src/game/world/world_script_types.h) and writes JSON that tools/check_script_dump.py compares with the decoded .ble sources,
// so a wrong record fails a test.

#include "world_dump.h"

#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/log.h"
#include "test_util.h"
#include "core/wide_path.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace obl::test {
namespace {

std::filesystem::path g_scriptsFile;

size_t CommandCount(const script::ScriptCommand* list)
{
    size_t count = 0;
    for (const script::ScriptCommand* node = list; node && count < 100000; node = node->m_next) ++count;
    return count;
}

// A state as JSON: its number, every message handler {id, OnReceipt length, OnSend length} and the three list lengths.
template<int N> void ScriptStateJson(std::ostringstream& json, const script::ScriptState<N>& state)
{
    json << "{\"id\":" << state.m_stateId << ",\"numHandlers\":" << state.m_NumHandlers << ",\"handlers\":[";
    for (uint32_t i = 0; i < state.m_NumHandlers && i < static_cast<uint32_t>(N); ++i) {
        const script::ScriptHandler& handler = state.m_handlers[i];
        json << (i ? "," : "") << "{\"id\":" << handler.m_messageId << ",\"receipt\":" << CommandCount(handler.m_onReceipt)
             << ",\"send\":" << CommandCount(handler.m_onSend) << "}";
    }
    json << "],\"entry\":" << CommandCount(state.m_onEntry) << ",\"exit\":" << CommandCount(state.m_onExit)
         << ",\"update\":" << CommandCount(state.m_onUpdate) << "}";
}

// The scenario script and every object script the world loaded, read through the native script records. The oracle parses the
// same .ble texts (tools/check_script_dump.py), so state tables, handler tables and list lengths are compared with the source.
bool DumpScripts(std::string& out)
{
    const world_data::World* world = World();
    const world_script_host::WorldScriptHost* host = world ? world->m_worldScriptHost.get() : nullptr;
    const world_script::WorldScript* script = world ? world->m_worldScript : nullptr;
    if (!host || !script || script != host->WorldScript() || script->m_numStates > world_script::kMaxWorldScriptStates) return false;
    std::ostringstream json;
    json << "{\"mapName\":\"" << world->m_mapId << "\",\"world\":{\"current\":" << script->m_currentState << ",\"states\":[";
    for (uint32_t i = 0; i < script->m_numStates; ++i) {
        json << (i ? "," : "") << "{\"name\":\"" << script->m_stateNames[i] << "\",\"state\":";
        ScriptStateJson(json, script->m_states[i]);
        json << "}";
    }
    json << "]},\"objects\":[";
    for (size_t i = 0; i < host->ScriptProgramCount(); ++i) {
        const script::ScriptProgram* object = &host->ScriptProgramAt(i);
        if (object->m_NumStates > script::kMaxProgramStates) return false;
        json << (i ? "," : "") << "{\"name\":\"" << host->ScriptProgramName(i) << "\",\"states\":[";
        for (uint32_t state = 0; state < object->m_NumStates; ++state) {
            json << (state ? "," : "");
            ScriptStateJson(json, object->m_states[state]);
        }
        json << "]}";
    }
    json << "]}\n";
    out = json.str();
    return true;
}

} // namespace

void InitWorldDump(const Text16& gameDirectory)
{
    g_scriptsFile = WidePath(gameDirectory) / "scripts-dump.json";
}

bool ExecuteWorldDumpCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "scripts-dump") {
        std::string text;
        if (!args.empty() || !World() || GameStateIndex() != 0) {
            OBL_LOG("autotest: FAILED scripts-dump needs settled gameplay and no arguments");
        } else if (!DumpScripts(text)) {
            OBL_LOG("autotest: FAILED scripts-dump: the world has no (consistent) scripts");
        } else {
            std::ofstream file(g_scriptsFile, std::ios::binary | std::ios::trunc);
            file << text;
            if (file)
                OBL_LOG("autotest: scripts-dump objects=%zu bytes=%zu", World()->m_worldScriptHost->ScriptProgramCount(), text.size());
            else
                OBL_LOG("autotest: FAILED scripts-dump cannot write %s", g_scriptsFile.string().c_str());
        }
        return true;
    }
    return false;
}

} // namespace obl::test
