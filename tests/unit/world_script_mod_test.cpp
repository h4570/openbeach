// Mod world scripts (`scripts/world/*.wsc`, docs/modding/reference/world-scripts.md): their order beside the scenario script, their own
// state budgets, the calls refused at start-up, `OnEvent` blocks and the reference page.
#include "unit_test.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_api.h"
#include "game/world/world_script_host.h"
#include "game/world/world_script_parse.h"
#include "game/world/world_script_types.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace obl::world_script_parse;
using obl::world_script::kMaxWorldScriptStates;
using obl::world_script::kWorldEventCount;
using obl::world_script::ModWorldScript;
using obl::world_script::WorldEvent;
using obl::world_script::WorldScript;
using obl::world_script_host::WorldScriptHost;

// The script host's parsers, writing down nothing: the tests look at the order of the scripts.
struct HostEnv {
    void SetHost(WorldScriptHost*) {}
    void ConstructWorld(WorldScript&) {}
    uint32_t LoadWorld(WorldScript&, const char*) { return 0; }
    void DestroyWorld(WorldScript&) {}
    void DestroyObject(obl::script::ScriptProgram&) {}
};

void* Allocate(size_t size)
{
    return std::malloc(size ? size : 1);
}
void Release(void* p)
{
    std::free(p);
}
const Memory kHeap{&Allocate, &Release};
int Beer(const char*)
{
    return 0;
}

Vocabulary ModVocabulary()
{
    return {{{1, "MT_ONE"}}, {{0, "PauseGame"}, {6, "UnlockNextScenario"}},
            {{0xee, "PlayerCash"}, {0xc2, "Var"}, {obl::world_script::kScriptValueEventKey, "EventKey"},
                    {obl::world_script::kScriptValueEventObjectId, "EventObjectId"},
                    {obl::world_script::kScriptValueEventValue, "EventValue"}},
            {}, {}};
}

// A parsed world script that frees itself.
struct Script {
    WorldScript script{};
    Script() { Construct(script); }
    ~Script() { Destroy(script, kHeap); }
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;
    // False with the parser's message when the text does not parse.
    bool Load(const std::string& text, std::string* error = nullptr)
    {
        try {
            Parse(script, text.data(), text.size(), ModVocabulary(), kHeap, &Beer);
            return true;
        } catch (const std::exception& failure) {
            if (error) *error = failure.what();
            return false;
        }
    }
};

std::string States(int count)
{
    std::string text;
    for (int i = 0; i < count; ++i)
        text += (i == 0 ? std::string("State STATE_WORLD_STARTUP") : "State STATE_" + std::to_string(i)) +
                " OnEntry End OnUpdate End OnExit End End\n";
    return text;
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
} // namespace

OBL_TEST_CASE("world_script_mod.scenario_script_then_mod_scripts_in_load_order_on_one_tick")
{
    HostEnv env;
    WorldScriptHost host(env);
    host.LoadScript("scenario.wsc", env);
    host.AddModWorldScript("first", "a.wsc", env);
    host.AddModWorldScript("second", "b.wsc", env);
    std::vector<std::string> order;
    std::vector<const WorldScript*> scripts;
    host.ForEachWorldScript([&](WorldScript& script, ModWorldScript* mod, size_t index) {
        order.push_back(mod ? mod->modId + "/" + mod->scriptName + "#" + std::to_string(index) : "scenario");
        scripts.push_back(&script);
    });
    OBL_CHECK(order == std::vector<std::string>({"scenario", "first/a.wsc#0", "second/b.wsc#1"}));
    OBL_REQUIRE(scripts.size() == 3u);
    OBL_CHECK(scripts[0] == host.WorldScript());
    OBL_CHECK(scripts[1] == host.ModWorldScriptAt(0).script.get() && scripts[2] == host.ModWorldScriptAt(1).script.get());
    order.clear();
    host.ForEachModWorldScript([&](WorldScript&, ModWorldScript* mod, size_t) { order.push_back(mod->modId); });
    OBL_CHECK(order == std::vector<std::string>({"first", "second"}));
    host.Destroy(env);
    OBL_CHECK_EQ(host.ModWorldScriptCount(), 0u);
}

OBL_TEST_CASE("world_script_mod.each_script_has_its_own_state_budget_flags_and_variables")
{
    // The scenario and two mods each fill all ten states; an eleventh state fails only in its own script.
    Script scenario, first, second;
    OBL_CHECK(scenario.Load(States(kMaxWorldScriptStates)));
    OBL_CHECK(first.Load(States(kMaxWorldScriptStates)));
    OBL_CHECK(second.Load(States(kMaxWorldScriptStates)));
    Script full;
    std::string error;
    OBL_CHECK(!full.Load(States(kMaxWorldScriptStates + 1), &error));
    OBL_CHECK(error.find("too many states") != std::string::npos);
    OBL_CHECK_EQ(scenario.script.m_numStates, static_cast<uint32_t>(kMaxWorldScriptStates));
    OBL_CHECK_EQ(first.script.m_numStates, static_cast<uint32_t>(kMaxWorldScriptStates));
    // Flag<n> and Var<n> of a mod script live in its own record, never in the world's.
    ModWorldScript a, b;
    a.scriptVars[0] = 7;
    a.scriptFlags = 1;
    OBL_CHECK(b.scriptVars[0] == 0 && b.scriptFlags == 0);
}

OBL_TEST_CASE("world_script_mod.winning_and_losing_calls_are_refused")
{
    const std::string head = "State STATE_WORLD_STARTUP OnEntry ";
    const std::string tail = " End OnUpdate End OnExit End End\n";
    const char* refused[] = {"Function UnlockNextScenario;", "ShowMenu ScenarioWon WON_TEXT", "ShowMenu GameWon;",
            "ShowMenu ScenarioLost LOST", "If ( PlayerCash > 5 ) ShowMenu scenariowon; End"};
    for (const char* call : refused) {
        Script script;
        OBL_REQUIRE(script.Load(head + call + tail));
        std::string error;
        OBL_CHECK(!ValidateModWorldScript(script.script, error));
        OBL_CHECK(error.find("the scenario owns winning and losing") != std::string::npos);
    }
    // In an OnEvent block too.
    {
        Script script;
        OBL_REQUIRE(script.Load("State STATE_WORLD_STARTUP OnEvent DayStarted Function UnlockNextScenario; End End\n"));
        std::string error;
        OBL_CHECK(!ValidateModWorldScript(script.script, error));
        OBL_CHECK(error.find("UnlockNextScenario") != std::string::npos);
    }
    // Other calls and menus pass.
    {
        Script script;
        OBL_REQUIRE(script.Load(head + "Function PauseGame; ShowMenu BONUS_INFO;" + tail));
        std::string error = "stale";
        OBL_CHECK(ValidateModWorldScript(script.script, error));
        OBL_CHECK(error.empty());
    }
    // A mod script needs its own STATE_WORLD_STARTUP.
    {
        Script script;
        OBL_REQUIRE(script.Load("State STATE_OTHER OnEntry End End\n"));
        std::string error;
        OBL_CHECK(!ValidateModWorldScript(script.script, error));
        OBL_CHECK(error.find("STATE_WORLD_STARTUP") != std::string::npos);
    }
}

OBL_TEST_CASE("world_script_mod.on_event_blocks_are_lists_of_their_state")
{
    Script script;
    OBL_REQUIRE(script.Load("State STATE_WORLD_STARTUP\n"
                            "  OnEvent DayStarted\n"
                            "    Add PlayerCash 5;\n"
                            "  End\n"
                            "  OnEvent BuildingFinished KIOSK\n"
                            "    Add ModVar.kiosks 1;\n"
                            "  End\n"
                            "  OnEvent BuildingFinished \"HUT\"\n"
                            "    Add ModVar.huts 1;\n"
                            "  End\n"
                            "End\n"
                            "State STATE_B\n"
                            "  OnEvent WeatherChanged Add Var0 1; End\n"
                            "  OnEvent GuestArrived End\n"
                            "End\n"));
    OBL_REQUIRE(script.script.m_onEvent[0].size() == 3u);
    OBL_CHECK(script.script.m_onEvent[0][0].event == WorldEvent::DayStarted);
    OBL_CHECK(script.script.m_onEvent[0][0].filter.empty());
    OBL_CHECK(script.script.m_onEvent[0][0].commands != nullptr);
    OBL_CHECK(script.script.m_onEvent[0][1].event == WorldEvent::BuildingFinished);
    OBL_CHECK_EQ(script.script.m_onEvent[0][1].filter, "KIOSK");
    OBL_CHECK(script.script.m_onEvent[0][1].commands != nullptr);
    OBL_CHECK(script.script.m_onEvent[0][2].event == WorldEvent::BuildingFinished);
    OBL_CHECK_EQ(script.script.m_onEvent[0][2].filter, "HUT");
    OBL_CHECK(script.script.m_onEvent[0][2].commands != nullptr);

    OBL_REQUIRE(script.script.m_onEvent[1].size() == 2u);
    OBL_CHECK(script.script.m_onEvent[1][0].event == WorldEvent::WeatherChanged);
    OBL_CHECK(script.script.m_onEvent[1][0].filter.empty());
    OBL_CHECK(script.script.m_onEvent[1][0].commands != nullptr);
    OBL_CHECK(script.script.m_onEvent[1][1].event == WorldEvent::GuestArrived);
    OBL_CHECK(script.script.m_onEvent[1][1].filter.empty());
    OBL_CHECK(script.script.m_onEvent[1][1].commands == nullptr);

    Script unknown;
    std::string error;
    OBL_CHECK(!unknown.Load("State STATE_WORLD_STARTUP OnEvent Midnight End End\n", &error));
    OBL_CHECK(error.find("Midnight") != std::string::npos);
    for (const WorldEventInfo& info : WorldEvents()) {
        OBL_CHECK(FindWorldEvent(info.name) == info.event);
        Script single;
        OBL_CHECK(single.Load(std::string("State STATE_WORLD_STARTUP OnEvent ") + info.name + " Add ModVar.seen 1; End End\n"));
    }
    OBL_CHECK_EQ(WorldEvents().size(), kWorldEventCount);
}

OBL_TEST_CASE("world_script_mod.named_and_shared_mod_variables_parse_and_validate")
{
    Script script;
    OBL_REQUIRE(script.Load("State STATE_WORLD_STARTUP\n"
                            "  OnEntry\n"
                            "    Set ModVar.bonus_count 10;\n"
                            "    Add ModVar.economy.tax_rate 2;\n"
                            "    If ( ModVar.bonus_count > ModVar.economy.tax_rate )\n"
                            "      Set ModVar.flag_ok 1;\n"
                            "    End\n"
                            "  End\n"
                            "  OnEvent BuildingFinished\n"
                            "    If ( EventKey == \"KIOSK\" )\n"
                            "      Set ModVar.last_id EventObjectId;\n"
                            "      Set ModVar.last_val EventValue;\n"
                            "    End\n"
                            "  End\n"
                            "End\n"));
    std::string error;
    OBL_CHECK(ValidateModWorldScript(script.script, error));

    for (const char* bad : {"ModVar.", "ModVar.a.", "ModVar..b", "ModVar.a.b.c", "ModVar.bad$name"}) {
        Script invalid;
        std::string parseError;
        OBL_CHECK(!invalid.Load(std::string("State STATE_WORLD_STARTUP OnEntry Set ") + bad + " 1; End End\n", &parseError));
        OBL_CHECK(!parseError.empty());
    }

    HostEnv env;
    WorldScriptHost host(env);
    OBL_CHECK_EQ(host.GetSharedModVar("alpha", "rate"), 0);
    host.SetSharedModVar("alpha", "rate", 42);
    OBL_CHECK_EQ(host.GetSharedModVar("alpha", "rate"), 42);
    OBL_CHECK_EQ(host.GetSharedModVar("beta", "rate"), 0);
    host.Destroy(env);
    OBL_CHECK(host.SharedModVars().empty());
}

OBL_TEST_CASE("world_script_mod.the_reference_page_lists_the_engine_vocabulary")
{
    const std::string page = ReadText(std::filesystem::path(OBL_REPO_DIR) / "docs/modding/reference/world-scripts.md");
    OBL_REQUIRE(!page.empty());
    for (const WorldEventInfo& info : WorldEvents()) OBL_CHECK(page.find(std::string("| `") + info.name + "` |") != std::string::npos);
    for (const char* menu : {"ScenarioWon", "GameWon", "ScenarioLost"}) {
        OBL_CHECK(IsRefusedModMenu(menu));
        OBL_CHECK(page.find(std::string("`ShowMenu ") + menu + "`") != std::string::npos);
    }
    OBL_CHECK(page.find("`Function UnlockNextScenario;`") != std::string::npos);
    for (const NamedId& function : EngineFunctions()) {
        const std::string token = IsRefusedModFunction(function.id) ? "Function " + function.name + ";" : function.name;
        OBL_CHECK(page.find("`" + token + "`") != std::string::npos);
    }
    const size_t start = page.find("### Built-in condition values");
    OBL_REQUIRE(start != std::string::npos);
    const auto values = page.substr(start, page.find("\n## ", start) - start);
    for (const NamedId& value : EngineValues()) OBL_CHECK(values.find("`" + value.name + "`") != std::string::npos);
}
