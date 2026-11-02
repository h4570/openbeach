#include "unit_test.h"
#include "game/world/world_script_types.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_host.h"
#include <string>
#include <vector>

using obl::script::ScriptProgram;
using obl::world_script::WorldScript;
using obl::world_script_host::WorldScriptHost;
namespace {
// Writes down every call with the object it got, so the tests see the order of the parsers' calls.
struct Env {
    std::vector<std::string> calls;
    std::vector<const void*> pointers;
    WorldScriptHost* global = nullptr;
    void Note(const std::string& call, const void* p)
    {
        calls.push_back(call);
        pointers.push_back(p);
    }
    void SetHost(WorldScriptHost* p)
    {
        global = p;
        Note("global", p);
    }
    void ConstructObject(ScriptProgram& p) { Note("construct object", &p); }
    void LoadObject(ScriptProgram& p, const char* path) { Note(std::string("load ") + path, &p); }
    void DestroyObject(ScriptProgram& p) { Note("destroy object", &p); }
    void ConstructWorld(WorldScript& p) { Note("construct world", &p); }
    uint32_t LoadWorld(WorldScript& p, const char* path)
    {
        Note(std::string("load ") + path, &p);
        return 12345;
    }
    void DestroyWorld(WorldScript& p) { Note("destroy world", &p); }
};
} // namespace

OBL_TEST_CASE("world_script_host.construct_binds_the_global_and_starts_empty")
{
    Env env;
    WorldScriptHost host(env);
    OBL_CHECK(env.global == &host);
    OBL_CHECK(host.WorldScript() == nullptr && host.ScriptProgramCount() == 0);
    host.Destroy(env);
}

OBL_TEST_CASE("world_script_host.add_script_program_constructs_then_loads_in_order")
{
    Env env;
    WorldScriptHost host(env);
    host.AddScriptProgram("GuestLogic", "Data/Scripts/guest.lgc", env);
    host.AddScriptProgram("CleanerLogic", "Data/Scripts/cleaner.lgc", env);
    OBL_CHECK_EQ(host.ScriptProgramCount(), 2u);
    OBL_CHECK(host.ScriptProgramName(0) == "GuestLogic" && host.ScriptProgramName(1) == "CleanerLogic");
    OBL_CHECK(env.calls ==
            std::vector<std::string>(
                    {"global", "construct object", "load Data/Scripts/guest.lgc", "construct object", "load Data/Scripts/cleaner.lgc"}));
    OBL_CHECK(env.pointers[1] == &host.ScriptProgramAt(0) && env.pointers[2] == &host.ScriptProgramAt(0));
    OBL_CHECK(env.pointers[3] == &host.ScriptProgramAt(1));
    OBL_CHECK(host.FindScriptProgram("CleanerLogic") == &host.ScriptProgramAt(1));
    host.Destroy(env);
}

OBL_TEST_CASE("world_script_host.find_is_case_sensitive_and_first_match_wins")
{
    Env env;
    WorldScriptHost host(env);
    OBL_CHECK(host.FindScriptProgram("GuestLogic") == nullptr);
    host.AddScriptProgram("GuestLogic", "a.lgc", env);
    host.AddScriptProgram("GuestLogic", "b.lgc", env);
    host.AddScriptProgram("", "c.lgc", env);
    OBL_CHECK(host.FindScriptProgram("GuestLogic") == &host.ScriptProgramAt(0));
    OBL_CHECK(host.FindScriptProgram("guestlogic") == nullptr);
    OBL_CHECK(host.FindScriptProgram("GuestLogicX") == nullptr);
    OBL_CHECK(host.FindScriptProgram("") == &host.ScriptProgramAt(2));
    host.Destroy(env);
}

OBL_TEST_CASE("world_script_host.load_replaces_the_world_script_and_keeps_script_programs")
{
    Env env;
    WorldScriptHost host(env);
    host.AddScriptProgram("GuestLogic", "a.lgc", env);
    OBL_CHECK_EQ(host.LoadScript("scenario.ble", env), 12345u);
    const WorldScript* first = host.WorldScript();
    OBL_CHECK(first != nullptr);
    env.calls.clear();
    env.pointers.clear();
    OBL_CHECK_EQ(host.LoadScript("scenario.ble", env), 12345u);
    OBL_CHECK(env.calls == std::vector<std::string>({"destroy world", "construct world", "load scenario.ble"}));
    OBL_CHECK(env.pointers[0] == first && env.pointers[1] == host.WorldScript());
    OBL_CHECK_EQ(host.ScriptProgramCount(), 1u);
    host.Destroy(env);
}

OBL_TEST_CASE("world_script_host.destroy_frees_script_programs_then_the_world_script_then_the_global")
{
    Env env;
    WorldScriptHost host(env);
    host.AddScriptProgram("GuestLogic", "a.lgc", env);
    host.AddScriptProgram("CleanerLogic", "b.lgc", env);
    host.LoadScript("scenario.ble", env);
    const void* guest = &host.ScriptProgramAt(0);
    const void* cleaner = &host.ScriptProgramAt(1);
    const void* world = host.WorldScript();
    env.calls.clear();
    env.pointers.clear();
    host.Destroy(env);
    OBL_CHECK(env.calls == std::vector<std::string>({"destroy object", "destroy object", "destroy world", "global"}));
    OBL_CHECK(env.pointers == std::vector<const void*>({guest, cleaner, world, nullptr}));
    OBL_CHECK(host.WorldScript() == nullptr && host.ScriptProgramCount() == 0 && env.global == nullptr);
    env.calls.clear();
    host.Destroy(env);
    OBL_CHECK(env.calls == std::vector<std::string>({"global"}));
}
