#include "unit_test.h"
#include "game/tables/catalog_load.h"
#include "game/world/world_script_types.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_api.h"
#include "game/world/world_script_load.h"
#include "game/world/world_script_parse.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

namespace {
using namespace obl::world_script_parse;
using obl::script::ScriptCommand;
using obl::world_script::kMaxWorldScriptStates;
using obl::world_script::ScriptAssignData;
using obl::world_script::ScriptFunctionData;
using obl::world_script::ScriptIfData;
using obl::world_script::WorldScript;
Vocabulary SmallVocabulary()
{
    return {{{1, "MT_ONE"}, {2, "MT_TWO"}}, {{0, "PauseGame"}, {7, "Debug"}}, {{0xee, "PlayerCash"}, {0xe2, "Me"}}, {{2, "CUSTOM_BEER"}},
            {{1, "Alcohol"}, {20, "Flirt"}, {21, "Umbrellas"}}};
}
struct Parsed {
    WorldScript script{};
    std::set<void*> live;
    int foreign = 0;
    int allocationsBeforeFailure = -1;
    static Parsed*& Current()
    {
        static Parsed* p = nullptr;
        return p;
    }
    static void* Allocate(size_t size)
    {
        if (Current()->allocationsBeforeFailure == 0) return nullptr;
        if (Current()->allocationsBeforeFailure > 0) --Current()->allocationsBeforeFailure;
        void* p = std::malloc(size ? size : 1);
        Current()->live.insert(p);
        return p;
    }
    static void Release(void* p)
    {
        if (!Current()->live.erase(p)) ++Current()->foreign;
        std::free(p);
    }
    Memory Heap()
    {
        Current() = this;
        return {&Allocate, &Release};
    }
    static int Beer(const char* name) { return std::strcmp(name, "TEST_BEER") == 0 ? 9 : 10; }
    void Load(const std::string& text, const Vocabulary& vocabulary = SmallVocabulary(), ParseDetails* details = nullptr)
    {
        Parse(script, text.data(), text.size(), vocabulary, Heap(), &Beer, details);
    }
    void Clear() { Destroy(script, Heap()); }
    ~Parsed()
    {
        Clear();
        for (void* p : live) std::free(p);
    }
};
template<class T> T& Data(ScriptCommand* node)
{
    return *static_cast<T*>(node->m_data);
}
float Float(const void* p)
{
    float result;
    std::memcpy(&result, p, 4);
    return result;
}
} // namespace

OBL_TEST_CASE("world_script_parse.values_nested_conditions_and_lists")
{
    Parsed p;
    p.Load("/comment\nState FIRST OnEntry Set playercash 1.25; Add Var2 Random5; Minus Flag3 RandDWORD8;\n"
           " Multiply ScenarioObj4 2; Divide BeerActiveTEST 3; Set DefaultBeerTEST \"x\"; Set BuildingCountTEST 4; Set EventActiveTEST 1;\n"
           " If (Var2 != 1) If (Flag3) Function Debug; Else Function PauseGame; End Else Set Var2 2; End End\n"
           " OnExit Function PauseGame; End OnUpdate Function Debug; End\n"
           " Message MT_TWO OnReceipt Message MT_ONE Me 1 2 Time 1.5 MaxTime 3; End OnSend ChangeState SECOND Time 2; End End End\n"
           "State SECOND End\n");
    OBL_REQUIRE(p.script.m_numStates == 2);
    auto* node = p.script.m_states[0].m_onEntry;
    OBL_CHECK_EQ(Data<ScriptAssignData>(node).m_leftKey, 0xee);
    OBL_CHECK_EQ(Float(Data<ScriptAssignData>(node).m_rightData), 1.25f);
    node = node->m_next;
    OBL_CHECK_EQ(Data<ScriptAssignData>(node).m_leftKey, 0xc2);
    OBL_CHECK_EQ(*static_cast<int*>(Data<ScriptAssignData>(node).m_leftData), 2);
    OBL_CHECK_EQ(Data<ScriptAssignData>(node).m_rightKey, 0xe4);
    OBL_CHECK_EQ(*static_cast<int*>(Data<ScriptAssignData>(node).m_rightData), 5);
    while (node->m_opcode != 4) node = node->m_next;
    auto& outer = Data<ScriptIfData>(node);
    OBL_CHECK_EQ(outer.m_op, 6);
    OBL_REQUIRE(outer.m_true && outer.m_false);
    auto& inner = Data<ScriptIfData>(outer.m_true);
    OBL_CHECK_EQ(inner.m_rightKey, 0x110);
    OBL_CHECK(inner.m_true && inner.m_false);
    OBL_CHECK_EQ(Count(p.script).nodes, 17);
    auto& state = p.script.m_states[0];
    OBL_CHECK_EQ(state.m_NumHandlers, 1u);
    auto& message = Data<obl::world_script::WorldScriptMessageData>(state.m_handlers[0].m_onReceipt);
    OBL_CHECK_EQ(message.m_messageId, 1);
    OBL_CHECK_EQ(message.m_time, 1.5f);
    OBL_CHECK_EQ(message.m_maxTime, 3.0f);
    OBL_CHECK_EQ(message.m_floatData.m_kind, 0x110);
    p.Clear();
    p.Clear();
    OBL_CHECK(p.live.empty());
    OBL_CHECK_EQ(p.foreign, 0);
}

OBL_TEST_CASE("world_script_parse.creation_geometry_and_schedules")
{
    Parsed p;
    p.Load("State FIRST OnEntry\n"
           " Create Building HOTEL 1 2 3 Age 4 Broken Prices 1 2 BeerType custom_beer 3.5;;\n"
           " Create Scenery LITTER Random Number 2 sprite; Create Guest GUEST Random Number 7;\n"
           " Path 1 2 3 nW; Grass 4 5 6 SE; Area Beach 2 1 2 3 4 0 0 4 0 4 4 0 4;\n"
           " BoatPoint 1 2 Unload Destroy; BoatEntry 1 2 3 -4 5; BoatExit 1 2 3 4 -5; CircuitPoint PowerBoat 1 2;\n"
           " BoatEvent Day+ 2 Time 7 Guests TYPE_A 3 TYPE_B Random 4;\n"
           " Disaster BeerShortage Day 1 Time 2 Duration 3 BeerType TEST_BEER ReplacementBeer OTHER ReplacementStrength 0.75;\n"
           " Weather ThunderStorm Day 4 Time 5 Flirt -0.1 Umbrellas 0.2 Alcohol 0.3;\n"
           " MoveCamera 1 2 3; ShowMenu ScenarioWon victory; LoadScript Logic logic.lgc; PlaySound beep; End End\n");
    auto* node = p.script.m_states[0].m_onEntry;
    auto& create = Data<obl::world_script::WorldScriptCreateData>(node);
    OBL_CHECK_EQ(create.m_prices[2], 3.5f);
    OBL_CHECK_EQ(create.m_beer, 2);
    OBL_CHECK_EQ(create.m_beerPrice, 3.5f);
    OBL_CHECK_EQ(create.m_age, 4);
    OBL_CHECK_EQ(create.m_broken, 1);
    for (node = node->m_next; node->m_opcode != 20; node = node->m_next) {}
    auto& boat = Data<obl::world_script::WorldScriptBoatEventData>(node);
    OBL_CHECK_EQ(boat.m_timeFlags, 2);
    OBL_CHECK_EQ(boat.m_day, 2);
    OBL_CHECK_EQ(boat.m_counts[0], 3);
    OBL_CHECK_EQ(boat.m_counts[1], 4);
    OBL_CHECK_EQ(boat.m_random[1], 1);
    OBL_CHECK_EQ(Data<obl::world_script::WorldScriptDisasterData>(node->m_next).m_beer, 9);
    node = node->m_next->m_next;
    auto& weather = Data<obl::world_script::WorldScriptWeatherData>(node);
    OBL_CHECK_EQ(weather.m_kind, 5);
    OBL_CHECK_EQ(weather.m_modifiers[20], -0.1f);
    OBL_CHECK_EQ(weather.m_modifiers[21], 0.2f);
    OBL_CHECK_EQ(weather.m_modifiers[1], 0.3f);
    OBL_CHECK_EQ(Count(p.script).nodes, 17);
    p.Clear();
    OBL_CHECK(p.live.empty());
    OBL_CHECK_EQ(p.foreign, 0);
}

OBL_TEST_CASE("world_script_parse.weather_modifiers_come_from_the_vocabulary")
{
    Parsed p;
    p.Load("State FIRST OnEntry Weather Normal Umbrellas 0.5; End End\n");
    const auto& weather = Data<obl::world_script::WorldScriptWeatherData>(p.script.m_states[0].m_onEntry);
    OBL_CHECK_EQ(weather.m_modifiers[21], 0.5f);
    p.script.m_numStates = 0;
    for (const char* word : {"Snacks", "umbrellas"}) {
        Parsed refused;
        const std::string text = std::string("State FIRST OnEntry Weather Normal ") + word + " 0.5; End End\n";
        bool failed = false;
        try {
            refused.Load(text);
        } catch (const ParseError&) {
            failed = true;
        }
        OBL_CHECK(failed);
    }
}

OBL_TEST_CASE("world_script_parse.duplicate_state_and_unrecognized_token_quirks")
{
    Parsed p;
    p.Load("State A End State B End State A OnEntry Unrecognized Function Debug bogus End End\n");
    OBL_CHECK_EQ(p.script.m_numStates, 3u);
    OBL_CHECK_EQ(p.script.m_currentState, 1u);
    OBL_CHECK(p.script.m_stateNames[2].empty());
    OBL_REQUIRE(p.script.m_states[1].m_onEntry);
    OBL_CHECK_EQ(Data<ScriptFunctionData>(p.script.m_states[1].m_onEntry).m_function, 7);
    p.Clear();
    OBL_CHECK(p.live.empty());
}

OBL_TEST_CASE("world_script_parse.errors_rollback_allocations_and_existing_lists")
{
    const char* bad[] = {"State", "State A OnEntry If (Var1", "State A OnEntry Set Var2 -1; End End",
            "State A OnEntry Function unrecognized; End End", "State A OnEntry Else End End", "State A OnEntry Area Beach -1; End End",
            "State A Message MT_BAD End End", "State A OnEntry Weather Normal Unrecognized 1; End End",
            "State A OnEntry Path 1 2 3 INVALID; End End", "State A OnEntry Create Building HOTEL 1 2 3 Prices 1 2 3 4 5 6; End End"};
    Parsed p;
    p.Load("State A OnEntry Function Debug; End End\n");
    auto before = p.live;
    for (const char* text : bad) {
        bool caught = false;
        try {
            p.Load(text);
        } catch (const ParseError&) {
            caught = true;
        }
        OBL_CHECK(caught);
        OBL_CHECK(p.live == before);
        OBL_CHECK_EQ(p.script.m_numStates, 1u);
        OBL_CHECK(!p.script.m_states[0].m_onEntry->m_next);
    }
    p.Clear();
    OBL_CHECK(p.live.empty());
    OBL_CHECK_EQ(p.foreign, 0);
}

OBL_TEST_CASE("world_script_parse.oracle_detects_nested_data_and_ignores_unset_words")
{
    Parsed a, b;
    const char* text = "State A OnEntry If (Me == 1) Set Var2 3; Else Message MT_ONE Me 2 3; End End End\n";
    a.Load(text);
    b.Load(text);
    OBL_CHECK(Diff(a.script, b.script).empty());
    auto& condition = Data<ScriptIfData>(b.script.m_states[0].m_onEntry);
    condition.m_leftData = reinterpret_cast<int*>(0x1234); // named value has no meaningful data word
    OBL_CHECK(Diff(a.script, b.script).empty());
    auto& assign = Data<ScriptAssignData>(condition.m_true);
    *static_cast<float*>(assign.m_rightData) = 4;
    OBL_CHECK(!Diff(a.script, b.script).empty());
    a.Clear();
    b.Clear();
    OBL_CHECK(a.live.empty() && b.live.empty());
}

OBL_TEST_CASE("world_script_parse.oracle_optional_fields_are_source_driven")
{
    Parsed a, b;
    ParseDetails details;
    const char* text =
            "State A OnEntry BoatEvent Time 8 Performance 3; Disaster Strike Time 9 Duration 2 Target HOTEL; Weather Normal; End End\n";
    a.Load(text);
    b.Load(text, SmallVocabulary(), &details);
    auto* node = a.script.m_states[0].m_onEntry;
    Data<obl::world_script::WorldScriptBoatEventData>(node).m_day = 999; // never set by this source
    OBL_CHECK(Diff(a.script, b.script, &details).empty());
    Data<obl::world_script::WorldScriptBoatEventData>(node).m_hour = 7;
    OBL_CHECK(!Diff(a.script, b.script, &details).empty());
    Data<obl::world_script::WorldScriptBoatEventData>(node).m_hour = 8;
    node = node->m_next;
    Data<obl::world_script::WorldScriptDisasterData>(node).m_value = 999;
    OBL_CHECK(Diff(a.script, b.script, &details).empty());
    Data<obl::world_script::WorldScriptDisasterData>(node).m_duration = 3; // explicit even for Strike
    OBL_CHECK(!Diff(a.script, b.script, &details).empty());
    Data<obl::world_script::WorldScriptDisasterData>(node).m_duration = 2;
    node = node->m_next;
    Data<obl::world_script::WorldScriptWeatherData>(node).m_day = 999;
    OBL_CHECK(Diff(a.script, b.script, &details).empty());
    Data<obl::world_script::WorldScriptWeatherData>(node).m_modifiers[5] = 1;
    OBL_CHECK(!Diff(a.script, b.script, &details).empty());
}

OBL_TEST_CASE("world_script_parse.allocation_failure_restores_append_transaction")
{
    Parsed p;
    p.Load("State A OnEntry Function Debug; End End\n");
    auto before = p.live;
    const char* text = "State A OnEntry Set Var1 2; If (Var1 == 2) PlaySound beep; End End End\n";
    Parsed preview;
    preview.Load("State A OnEntry Function Debug; End End\n");
    size_t priorBlocks = preview.live.size();
    preview.Load(text);
    int allocationCount = static_cast<int>(preview.live.size() - priorBlocks);
    for (int failAt = 0; failAt < allocationCount; ++failAt) {
        p.allocationsBeforeFailure = failAt;
        bool caught = false;
        try {
            p.Load(text);
        } catch (const std::bad_alloc&) {
            caught = true;
        }
        OBL_REQUIRE(caught);
        OBL_CHECK(p.live == before);
        OBL_CHECK_EQ(p.script.m_numStates, 1u);
        OBL_CHECK(!p.script.m_states[0].m_onEntry->m_next);
    }
    p.allocationsBeforeFailure = -1;
    p.Clear();
    OBL_CHECK(p.live.empty());
    OBL_CHECK_EQ(p.foreign, 0);
}

OBL_TEST_CASE("world_script_parse.vocabulary_validation")
{
    Vocabulary out;
    std::string error;
    OBL_CHECK(!obl::world::ParseWorldScriptIds("[]", out, error));
    OBL_CHECK(!obl::world::ParseWorldScriptIds("{\"format\":0,\"version\":[]}", out, error));
    OBL_CHECK(!obl::world::ParseWorldScriptIds("{\"format\":\"obl.world_script_vocabulary\",\"version\":1}", out, error));
    OBL_CHECK(out.messages.empty());

    // The messages come from the file, the functions and values from the engine's script API.
    std::string json = "{\"format\":\"obl.world_script_vocabulary\",\"version\":1,\"messages\":[";
    for (int i = 0; i < 108; ++i)
        json += (i ? ",{\"id\":" : "{\"id\":") + std::to_string(i) + ",\"name\":\"MT_" + std::to_string(i) + "\"}";
    OBL_CHECK(!obl::world::ParseWorldScriptIds(json + ",{\"id\":108,\"name\":\"MT_X\"}]}", out, error)); // exactly 108
    OBL_REQUIRE(obl::world::ParseWorldScriptIds(json + "]}", out, error));
    OBL_CHECK_EQ(out.messages.size(), 108u);
    OBL_CHECK_EQ(out.functions.size(), obl::world_script_parse::EngineFunctions().size());
    OBL_CHECK_EQ(out.values.size(), obl::world_script_parse::EngineValues().size());
}

namespace {
// Heap that records every release call, null ones included: the destructor's call sequence is the contract.
struct Recording {
    static std::vector<void*>& Calls()
    {
        static std::vector<void*> calls;
        return calls;
    }
    static std::set<void*>& Live()
    {
        static std::set<void*> live;
        return live;
    }
    static void* Allocate(size_t size)
    {
        void* p = std::malloc(size ? size : 1);
        Live().insert(p);
        return p;
    }
    static void Release(void* p)
    {
        Calls().push_back(p);
        if (p) {
            OBL_CHECK(Live().erase(p) == 1);
            std::free(p);
        }
    }
    static Memory Heap()
    {
        Calls().clear();
        return {&Allocate, &Release};
    }
    template<class T> static T* New()
    {
        auto* p = static_cast<T*>(Allocate(sizeof(T)));
        std::memset(p, 0, sizeof(T));
        return p;
    }
    static ScriptCommand* Node(int opcode, void* payload, ScriptCommand* next = nullptr)
    {
        auto* node = New<ScriptCommand>();
        node->m_opcode = opcode;
        node->m_data = payload;
        node->m_next = next;
        return node;
    }
};
} // namespace

OBL_TEST_CASE("world_script_parse.construct_writes_every_field_but_cursor_and_parsed_list")
{
    WorldScript script;
    // Every plain field gets a junk pattern (the owned names cannot be memset).
    script.m_text = reinterpret_cast<const char*>(uintptr_t{0xa5a5a5a5u});
    script.m_openCommand = reinterpret_cast<ScriptCommand*>(uintptr_t{0xa5a5a5a5u});
    std::memset(static_cast<void*>(script.m_states), 0xa5, sizeof(script.m_states));
    script.m_numStates = script.m_currentState = 0xa5a5a5a5u;
    script.m_parsedList = static_cast<int>(0xa5a5a5a5u);
    for (auto& name : script.m_stateNames) name = "junk";
    Construct(script);
    OBL_CHECK_EQ(script.m_text, reinterpret_cast<const char*>(uintptr_t{0xa5a5a5a5u}));
    OBL_CHECK_EQ(script.m_parsedList, static_cast<int>(0xa5a5a5a5u));
    OBL_CHECK(script.m_openCommand == nullptr);
    OBL_CHECK_EQ(script.m_numStates, 0u);
    OBL_CHECK_EQ(script.m_currentState, 0u);
    for (int i = 0; i < kMaxWorldScriptStates; ++i) {
        auto& state = script.m_states[i];
        OBL_CHECK(script.m_stateNames[i].empty());
        OBL_CHECK_EQ(state.m_stateId, 0x0fffffff);
        OBL_CHECK_EQ(state.m_NumHandlers, 0u);
        OBL_CHECK(state.m_onEntry == nullptr);
        OBL_CHECK(state.m_onExit == nullptr);
        OBL_CHECK(state.m_onUpdate == nullptr);
        for (auto& handler : state.m_handlers) {
            OBL_CHECK_EQ(handler.m_messageId, 0x0fffffff);
            OBL_CHECK(handler.m_onReceipt == nullptr);
            OBL_CHECK(handler.m_onSend == nullptr);
        }
    }
}

OBL_TEST_CASE("world_script_parse.destruct_release_sequence_follows_the_exe")
{
    WorldScript script;
    Construct(script);
    script.m_stateNames[0] = "STATE_A"; // owned std::string: not released through the game memory
    // State 0: OnEntry = Function (payload released), then opcode 5 whose payload the destructor leaves alone.
    static int foreign = 0;
    auto* function = Recording::New<ScriptFunctionData>();
    auto* kept = Recording::Node(5, &foreign);
    auto* entry = Recording::Node(0, function, kept);
    script.m_states[0].m_onEntry = entry;
    // State 3, handler 1: OnReceipt = text command, OnSend = change-state with a null name.
    auto* text = Recording::New<obl::world_script::WorldScriptTextData>();
    text->m_text = static_cast<char*>(Recording::Allocate(4));
    auto* receipt = Recording::Node(11, text);
    auto* change = Recording::New<obl::world_script::WorldScriptChangeStateData>();
    auto* send = Recording::Node(2, change);
    script.m_states[3].m_handlers[1].m_onReceipt = receipt;
    script.m_states[3].m_handlers[1].m_onSend = send;
    // State 9: OnUpdate = Set with one owned operand and one plain one.
    auto* assign = Recording::New<ScriptAssignData>();
    assign->m_leftKey = 0xc2;
    assign->m_leftData = Recording::Allocate(4);
    assign->m_rightKey = 0xee;
    assign->m_rightData = &foreign;
    auto* update = Recording::Node(10, assign);
    script.m_states[9].m_onUpdate = update;
    void* assignOwned = assign->m_leftData; // read before the blocks are released
    void* textOwned = text->m_text;

    Destruct(script, Recording::Heap());
    std::vector<void*> expected = {assignOwned, assign, update}; // state 9 first
    expected.insert(expected.end(), {textOwned, text, receipt, change, send}); // state 3: OnReceipt, then OnSend (null name skipped)
    expected.insert(expected.end(), {function, entry, kept}); // state 0; opcode 5 keeps its payload
    OBL_CHECK_EQ(Recording::Calls().size(), expected.size());
    OBL_CHECK(Recording::Calls() == expected);
    OBL_CHECK_EQ(Recording::Live().size(), 0u);
    OBL_CHECK(script.m_states[0].m_onEntry == nullptr);
    OBL_CHECK(script.m_states[3].m_handlers[1].m_onSend == nullptr);
}

OBL_TEST_CASE("world_script_parse.destruct_calls_release_for_null_owned_pointers_like_the_exe")
{
    WorldScript script;
    Construct(script);
    // Message: owning kinds are released even when their data pointer is null; non-owning kinds are not.
    auto* message = Recording::New<obl::world_script::WorldScriptMessageData>();
    message->m_receiver = {0xe2, nullptr};
    message->m_data = {0xe8, nullptr};
    message->m_flags = {0x36, nullptr};
    message->m_floatData = {0xc2, nullptr};
    message->m_state = {0x110, nullptr};
    message->m_sendFrom = {0xd3, nullptr};
    auto* create = Recording::New<obl::world_script::WorldScriptCreateData>(); // null type and sprite: skipped
    auto* area = Recording::New<obl::world_script::WorldScriptAreaData>(); // null points: skipped
    auto* second = Recording::Node(12, create);
    auto* first = Recording::Node(1, message, Recording::Node(16, area, second));
    script.m_states[0].m_onEntry = first;
    Destruct(script, Recording::Heap());
    std::vector<void*> expected = {nullptr, nullptr, nullptr, nullptr, message, first}; // 4 owning operands (0xe8, 0x36, 0xc2, 0xd3)
    OBL_CHECK_EQ(Recording::Calls().size(), expected.size() + 4); // + area, its node, create, its node
    OBL_CHECK(std::vector<void*>(Recording::Calls().begin(), Recording::Calls().begin() + expected.size()) == expected);
}

OBL_TEST_CASE("world_script_parse.destruct_frees_every_onsend_list")
{
    Parsed p;
    p.Load("State FIRST Message MT_TWO OnReceipt Set Var1 2; End OnSend Set Var2 3; Message MT_ONE Me 1 2; End End End\n");
    OBL_REQUIRE(p.script.m_states[0].m_handlers[0].m_onSend != nullptr);
    static std::set<void*>* heap;
    heap = &p.live;
    Memory memory{&Parsed::Allocate, [](void* block) {
                      if (block) {
                          heap->erase(block);
                          std::free(block);
                      }
                  }};
    Destruct(p.script, memory);
    OBL_CHECK(p.live.empty());
    OBL_CHECK_EQ(p.script.m_states[0].m_handlers[0].m_onSend, static_cast<ScriptCommand*>(nullptr));
    p.script.m_numStates = 0;
}

OBL_TEST_CASE("data.world_script_parse.all_scenario_and_sandbox_files")
{
    auto root = std::filesystem::path(OBL_GAME_DIR);
    if (!std::filesystem::exists(root / "Data" / "Scenario")) OBL_SKIP("run not prepared");
    std::ifstream in(root / "packs" / "base" / "scripts" / "world_vocabulary.json", std::ios::binary);
    if (!in) OBL_SKIP("scripts/world_vocabulary.json not converted");
    std::string json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    Vocabulary vocabulary;
    std::string error;
    OBL_REQUIRE(obl::world::ParseWorldScriptIds(json, vocabulary, error));
    auto params = std::make_unique<obl::tables::WorldParams>();
    OBL_REQUIRE(obl::catalog::LoadPack(root / "packs" / "base", *params, error));
    obl::world::AddCatalogModifiers(*params, vocabulary);
    int files = 0, commands = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "Data" / "Scenario")) {
        if (entry.path().extension() != ".ble") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        for (char& c : text) c = static_cast<char>(c ^ 0xff);
        Parsed p;
        p.Load(text, vocabulary);
        OBL_CHECK(p.script.m_numStates > 0);
        commands += Count(p.script).nodes;
        ++files;
        p.Clear();
        OBL_CHECK(p.live.empty());
        OBL_CHECK_EQ(p.foreign, 0);
    }
    OBL_CHECK_EQ(files, 24);
    OBL_CHECK(commands > 5000);
}

OBL_TEST_CASE("world_script_parse.creation_beer_uses_catalog_ids_and_preserves_missing_name")
{
    auto vocabulary = SmallVocabulary();
    vocabulary.beers = {{4, "second"}, {1, "first"}};
    Parsed p;
    p.Load("State A OnEntry Create Building BAR 1 2 3 BeerType FIRST 6; Create Building BAR 1 2 3 BeerType missing 7; End End", vocabulary);
    auto* node = p.script.m_states[0].m_onEntry;
    OBL_CHECK_EQ(Data<obl::world_script::WorldScriptCreateData>(node).m_beer, 1);
    OBL_CHECK_EQ(Data<obl::world_script::WorldScriptCreateData>(node->m_next).m_beer, -1);
}
