#include "game/world/world_script_host_game.h"
#include "game/game/diagnostics.h"
#include "game/game/game_accessors.h"
#include "game/objects/script_program_load.h"
#include "game/world/world_script_load.h"
#include "core/call_counter.h"

namespace obl::world {
namespace {
using script::ScriptProgram;
using world_script::WorldScript;
using world_script_host::WorldScriptHost;
// Call counters (`call-report world-script-host.construct`).
test::CallCounter g_constructCalls("world-script-host.construct"), g_destructCalls("world-script-host.destruct"),
        g_loadCalls("world-script-host.load-script"), g_findCalls("world-script-host.find-object-script"),
        g_addCalls("world-script-host.add-object-script");

// The scripts' contents go through the parsers' entry points (they count their calls).
struct GameEnv {
    void SetHost(WorldScriptHost* p) { game::SetWorldScriptHost(p); }
    void ReleaseObjectScript(ScriptProgram& p) { object::ReleaseScriptProgramStates(p); }
    void ResetObjectScript(ScriptProgram& p) { object::ResetScriptProgram(p); }
    void LoadObject(ScriptProgram& p, const char* path) { object::ScriptProgramLoad(p, path); }
    void DestroyWorld(WorldScript& p) { WorldScriptDestruct(p); }
    void ConstructWorld(WorldScript& p) { WorldScriptConstruct(p); }
    uint32_t LoadWorld(WorldScript& p, const char* path, const char* logicalPath) { return WorldScriptLoad(p, path, logicalPath); }
};

} // namespace

} // namespace obl::world

namespace obl::world_script_host {
using world::g_addCalls;
using world::g_constructCalls;
using world::g_destructCalls;
using world::g_findCalls;
using world::g_loadCalls;
using world::GameEnv;

WorldScriptHost* WorldScriptHost::Create()
{
    GameEnv env;
    g_constructCalls.Count();
    return new WorldScriptHost(env);
}

void WorldScriptHost::Release(WorldScriptHost* host)
{
    GameEnv env;
    host->Destroy(env);
    delete host;
    g_destructCalls.Count();
}

uint32_t WorldScriptHost::Load(const char* path, const char* logicalPath)
{
    GameEnv env;
    const uint32_t result = LoadScript(path, logicalPath, env);
    g_loadCalls.Count();
    return result;
}

void WorldScriptHost::LoadModScripts()
{
    GameEnv env;
    DropModWorldScripts(env);
    for (const world::ModWorldScriptFile& file : world::DiscoverModWorldScripts()) {
        world_script::ModWorldScript& entry = AddModWorldScript(file.modId, file.scriptName, env);
        std::string error;
        if (!world::LoadModWorldScript(*entry.script, file, error)) game::FatalError("%s", error.c_str());
    }
}

void WorldScriptHost::AddScript(const char* name, const char* path)
{
    GameEnv env;
    AddScriptProgram(name, path, env);
    g_addCalls.Count();
}

script::ScriptProgram* WorldScriptHost::Find(const char* name) const
{
    g_findCalls.Count();
    return FindScriptProgram(name);
}

script::ScriptProgram* WorldScriptHost::FindOrLoad(const char* name)
{
    if (script::ScriptProgram* found = Find(name)) return found;
    const std::string path = std::string("data/script/object/") + name + ".lgc";
    AddScript(name, path.c_str());
    return FindScriptProgram(name);
}
} // namespace obl::world_script_host
