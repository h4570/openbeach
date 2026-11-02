// Object script Diff/Count and the public destructors on parsed scripts (src/game/objects/script_program.h): every branch of the
// comparer (one mutation per payload field) and FreeCommand/DestructStates returning exactly the blocks Parse took.

#include "unit_test.h"

#include "game/objects/script_program_types.h"
#include "game/objects/script_program.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>

using namespace obl::script_program;
using obl::script::ProgramState;
using obl::script::ScriptProgram;

namespace {

Vocabulary Vocab()
{
    Vocabulary v;
    v.states = {{0, "STATE_NULL"}, {1, "STATE_A"}, {2, "STATE_B"}};
    v.messages = {{0, "MT_ONE"}, {1, "MT_TWO"}, {2, "MT_THREE"}};
    v.functions = {{0, "DoFirst"}, {1, "DoSecond"}};
    v.values = {{0, "Sender"}, {5, "MessData"}, {0x40, "ActionState"}, {0xe9, "State"}};
    return v;
}

struct Heap {
    std::set<void*> live;
    int foreign = 0;
    static Heap*& Current()
    {
        static Heap* current = nullptr;
        return current;
    }
    static void* Allocate(size_t size)
    {
        void* block = std::malloc(size);
        Current()->live.insert(block);
        return block;
    }
    static void Release(void* block)
    {
        if (Current()->live.erase(block) == 0) ++Current()->foreign;
        std::free(block);
    }
    Memory memory()
    {
        Current() = this;
        return {&Allocate, &Release};
    }
};

struct Parsed {
    Heap heap;
    std::unique_ptr<ScriptProgram> script = std::make_unique<ScriptProgram>();
    explicit Parsed(const std::string& text)
    {
        std::memset(script.get(), 0, sizeof(ScriptProgram));
        std::string buffer = text + ".";
        Parse(*script, buffer.data(), text.size(), Vocab(), heap.memory());
    }
    ~Parsed()
    {
        Destroy(*script, heap.memory());
        for (void* block : heap.live) std::free(block);
    }
};

std::string DiffOf(const std::string& a, const std::string& b)
{
    Parsed x(a), y(b);
    return Diff(*x.script, *y.script);
}

// A script with every payload kind in OnEntry, so each Diff branch is reachable by editing one token.
std::string Rich(std::string message = {}, std::string change = {}, std::string animation = {}, std::string condition = {},
        std::string sw = {}, std::string arithmetic = {}, std::string sound = {})
{
    // An empty argument keeps the default command of that kind.
    auto pick = [](std::string& value, const char* fallback) {
        if (value.empty()) value = fallback;
    };
    pick(message, "Message MT_ONE Sender 1 2 Time 1 MaxTime 2 FloatData 3 State STATE_A SendFrom Sender;");
    pick(change, "ChangeState STATE_B Time 1;");
    pick(animation, "PlayAnimation w Finish f NotLooped Reverse ExitState STATE_A MaxTime 3 EndMessage MT_ONE;");
    pick(condition, "If (Sender == \"q\") Function DoFirst; Else Function DoSecond; End");
    pick(sw, "Switch (State) Case 1: Function DoFirst; Case \"s\": Function DoSecond; End");
    pick(arithmetic, "Add State 7; Set MessData \"t\";");
    pick(sound, "PlaySound boom;");
    return "State STATE_A OnEntry " + message + " " + change + " " + animation + " " + condition + " " + sw + " " + arithmetic + " " +
            sound + " End End\n";
}

} // namespace

OBL_TEST_CASE("script_program_diff.identical_rich_scripts_are_equal")
{
    OBL_CHECK_EQ(DiffOf(Rich(), Rich()), std::string());
}

OBL_TEST_CASE("script_program_diff.each_payload_field_is_compared")
{
    struct Case {
        std::string changed;
        const char* expect;
    };
    const Case cases[] = {
            {Rich("Message MT_TWO Sender 1 2 Time 1 MaxTime 2 FloatData 3 State STATE_A SendFrom Sender;"), "message"},
            {Rich("Message MT_ONE Sender 1 2 Time 9 MaxTime 2 FloatData 3 State STATE_A SendFrom Sender;"), "Time/MaxTime"},
            {Rich("Message MT_ONE Sender 1 2 Time 1 MaxTime 9 FloatData 3 State STATE_A SendFrom Sender;"), "Time/MaxTime"},
            {Rich("Message MT_ONE Sender 5 2 Time 1 MaxTime 2 FloatData 3 State STATE_A SendFrom Sender;"), "data"},
            {Rich("Message MT_ONE Sender 1 5 Time 1 MaxTime 2 FloatData 3 State STATE_A SendFrom Sender;"), "flags"},
            {Rich("Message MT_ONE Sender 1 2 Time 1 MaxTime 2 FloatData 9 State STATE_A SendFrom Sender;"), "FloatData"},
            {Rich("Message MT_ONE Sender 1 2 Time 1 MaxTime 2 FloatData 3 State STATE_B SendFrom Sender;"), "State"},
            {Rich("Message MT_ONE Sender 1 2 Time 1 MaxTime 2 FloatData 3 State STATE_A SendFrom MessData;"), "SendFrom"},
            {Rich("Message MT_ONE Sender 1 2 Time 1 MaxTime 2 FloatData 3 State STATE_A;"), "SendFrom"},
            {Rich({}, "ChangeState STATE_A Time 1;"), "ChangeState"},
            {Rich({}, "ChangeState STATE_B Time 2;"), "ChangeState"},
            {Rich({}, {}, "PlayAnimation x Finish f NotLooped Reverse ExitState STATE_A MaxTime 3 EndMessage MT_ONE;"), "animation names"},
            {Rich({}, {}, "PlayAnimation w Finish g NotLooped Reverse ExitState STATE_A MaxTime 3 EndMessage MT_ONE;"), "animation names"},
            {Rich({}, {}, "PlayAnimation w NotLooped Reverse ExitState STATE_A MaxTime 3 EndMessage MT_ONE;"), "animation names"},
            {Rich({}, {}, "PlayAnimation w Finish f Reverse ExitState STATE_A MaxTime 3 EndMessage MT_ONE;"), "animation options"},
            {Rich({}, {}, "PlayAnimation w Finish f NotLooped ExitState STATE_A MaxTime 3 EndMessage MT_ONE;"), "animation options"},
            {Rich({}, {}, "PlayAnimation w Finish f NotLooped Reverse ExitState STATE_B MaxTime 3 EndMessage MT_ONE;"),
                    "animation options"},
            {Rich({}, {}, "PlayAnimation w Finish f NotLooped Reverse ExitState STATE_A MaxTime 4 EndMessage MT_ONE;"),
                    "animation options"},
            {Rich({}, {}, "PlayAnimation w Finish f NotLooped Reverse ExitState STATE_A MaxTime 3 EndMessage MT_TWO;"),
                    "animation options"},
            {Rich({}, {}, {}, "If (Sender == \"z\") Function DoFirst; Else Function DoSecond; End"), "text differs"},
            {Rich({}, {}, {}, "If (Sender != \"q\") Function DoFirst; Else Function DoSecond; End"), "operator"},
            {Rich({}, {}, {}, "If (MessData == \"q\") Function DoFirst; Else Function DoSecond; End"), "left"},
            {Rich({}, {}, {}, "If (Sender == \"q\") Function DoSecond; Else Function DoSecond; End"), "function"},
            {Rich({}, {}, {}, "If (Sender == \"q\") Function DoFirst; Else Function DoFirst; End"), "function"},
            {Rich({}, {}, {}, "If (Sender == \"q\") Function DoFirst; End"), "else flag"},
            {Rich({}, {}, {}, {}, "Switch (MessData) Case 1: Function DoFirst; Case \"s\": Function DoSecond; End"), "value"},
            {Rich({}, {}, {}, {}, "Switch (State) Case 2: Function DoFirst; Case \"s\": Function DoSecond; End"), "number differs"},
            {Rich({}, {}, {}, {}, "Switch (State) Case 1: Function DoFirst; Case \"t\": Function DoSecond; End"), "text differs"},
            {Rich({}, {}, {}, {}, "Switch (State) Case 1: Function DoFirst; Case \"s\": Function DoFirst; End"), "function"},
            {Rich({}, {}, {}, {}, "Switch (State) Case 1: Function DoFirst; End"), "case count"},
            {Rich({}, {}, {}, {}, "Switch (State) Case 1: Function DoFirst; Case \"s\": Function DoSecond; Case 3: Function DoFirst; End"),
                    "case count"},
            {Rich({}, {}, {}, {}, {}, "Add State 8; Set MessData \"t\";"), "number differs"},
            {Rich({}, {}, {}, {}, {}, "Add State 7; Set MessData \"u\";"), "text differs"},
            {Rich({}, {}, {}, {}, {}, "Minus State 7; Set MessData \"t\";"), "opcode"},
            {Rich({}, {}, {}, {}, {}, {}, "PlaySound bang;"), "sound name"},
    };
    int index = 0;
    for (const Case& c : cases) {
        std::string diff = DiffOf(Rich(), c.changed);
        if (diff.find(c.expect) == std::string::npos)
            ::obl::unit::ReportFailure(
                    __FILE__, __LINE__, "case " + std::to_string(index) + " expected '" + c.expect + "' in '" + diff + "'");
        ++index;
    }
}

OBL_TEST_CASE("script_program_diff.states_handlers_and_lists_are_compared")
{
    const std::string base = "State STATE_A OnEntry Function DoFirst; End OnExit Function DoFirst; End OnUpdate Function DoFirst; End\n"
                             " Message MT_ONE OnReceipt Function DoFirst; End OnSend Function DoFirst; End End\n"
                             "End\n";
    OBL_CHECK_EQ(DiffOf(base, base), std::string());
    auto expect = [&](const std::string& changed, const char* what) {
        std::string diff = DiffOf(base, changed);
        if (diff.find(what) == std::string::npos)
            ::obl::unit::ReportFailure(__FILE__, __LINE__, std::string("expected '") + what + "' in '" + diff + "' for " + changed);
    };
    expect(base + "State STATE_B End\n", "state count 1 vs 2");
    expect("State STATE_B OnEntry Function DoFirst; End End\n", "id 1 vs 2");
    expect("State STATE_A OnEntry Function DoFirst; End OnExit Function DoFirst; End OnUpdate Function DoFirst; End End\n",
            "handler count 1 vs 0");
    expect("State STATE_A OnEntry Function DoSecond; End OnExit Function DoFirst; End OnUpdate Function DoFirst; End\n"
           " Message MT_ONE OnReceipt Function DoFirst; End OnSend Function DoFirst; End End End\n",
            "OnEntry");
    expect("State STATE_A OnEntry Function DoFirst; End OnExit Function DoSecond; End OnUpdate Function DoFirst; End\n"
           " Message MT_ONE OnReceipt Function DoFirst; End OnSend Function DoFirst; End End End\n",
            "OnExit");
    expect("State STATE_A OnEntry Function DoFirst; End OnExit Function DoFirst; End OnUpdate Function DoSecond; End\n"
           " Message MT_ONE OnReceipt Function DoFirst; End OnSend Function DoFirst; End End End\n",
            "OnUpdate");
    expect("State STATE_A OnEntry Function DoFirst; End OnExit Function DoFirst; End OnUpdate Function DoFirst; End\n"
           " Message MT_TWO OnReceipt Function DoFirst; End OnSend Function DoFirst; End End End\n",
            "message 0 vs 1");
    expect("State STATE_A OnEntry Function DoFirst; End OnExit Function DoFirst; End OnUpdate Function DoFirst; End\n"
           " Message MT_ONE OnReceipt Function DoSecond; End OnSend Function DoFirst; End End End\n",
            "OnReceipt");
    expect("State STATE_A OnEntry Function DoFirst; End OnExit Function DoFirst; End OnUpdate Function DoFirst; End\n"
           " Message MT_ONE OnReceipt Function DoFirst; End OnSend Function DoSecond; End End End\n",
            "OnSend");
}

OBL_TEST_CASE("script_program_diff.unrecognized_opcode_and_missing_payload")
{
    obl::script_program::FunctionCommand f{1};
    ScriptCommand a{nullptr, 12, &f}, b{nullptr, 12, &f};
    auto one = std::make_unique<ScriptProgram>(), two = std::make_unique<ScriptProgram>();
    Construct(*one);
    Construct(*two);
    one->m_NumStates = two->m_NumStates = 1;
    one->m_states[0].m_stateId = two->m_states[0].m_stateId = 1;
    one->m_states[0].m_onEntry = &a;
    two->m_states[0].m_onEntry = &b;
    OBL_CHECK(Diff(*one, *two).find("unsupported opcode 12") != std::string::npos);
    ScriptCommand c{nullptr, obl::script_program::OpFunction, nullptr};
    two->m_states[0].m_onEntry = &c;
    OBL_CHECK(Diff(*one, *two).find("opcode 12 vs 0") != std::string::npos);
    ScriptCommand d{nullptr, 12, nullptr};
    one->m_states[0].m_onEntry = &d;
    OBL_CHECK(Diff(*one, *two).find("opcode") != std::string::npos);
    two->m_states[0].m_onEntry = &d;
    OBL_CHECK_EQ(Diff(*one, *two), std::string()); // both without payload: equal
    ScriptCommand e{nullptr, 12, &f};
    two->m_states[0].m_onEntry = &e;
    OBL_CHECK(Diff(*one, *two).find("payload missing") != std::string::npos);
}

OBL_TEST_CASE("script_program_diff.count_walks_nested_lists")
{
    Parsed p(Rich());
    ListStats stats = Count(*p.script);
    // Message, ChangeState, Animation, If, Switch, Add, Set, PlaySound at the top; Function in then/else; Function in two cases.
    OBL_CHECK_EQ(stats.nodes, 8 + 2 + 2);
    OBL_CHECK_EQ(stats.opcodes[obl::script_program::OpFunction], 4);
    OBL_CHECK_EQ(stats.opcodes[obl::script_program::OpMessage], 1);
    OBL_CHECK_EQ(stats.opcodes[obl::script_program::OpIf], 1);
    OBL_CHECK_EQ(stats.opcodes[obl::script_program::OpSwitch], 1);
    OBL_CHECK_EQ(stats.opcodes[obl::script_program::OpSet], 1);
    OBL_CHECK_EQ(stats.opcodes[obl::script_program::OpAdd], 1);
    OBL_CHECK_EQ(stats.opcodes[obl::script_program::OpPlaySound], 1);

    // A node with an out-of-range opcode counts as a node but in no bucket; handlers are walked too.
    Parsed h("State STATE_A Message MT_ONE OnReceipt Function DoFirst; End OnSend Function DoFirst; Function DoSecond; End End End\n");
    ListStats handlers = Count(*h.script);
    OBL_CHECK_EQ(handlers.nodes, 3);
}

OBL_TEST_CASE("script_program_diff.destruct_states_frees_exactly_what_parse_took")
{
    Parsed p(Rich());
    OBL_CHECK(!p.heap.live.empty());
    Memory memory = p.heap.memory();
    DestructStates(*p.script, memory);
    OBL_CHECK(p.heap.live.empty());
    OBL_CHECK_EQ(p.heap.foreign, 0);
    // DestructStates leaves dangling heads: clear them so the fixture's own Destroy has nothing left to free.
    p.script->m_NumStates = 0;
    for (auto& state : p.script->m_states) {
        state.m_onEntry = state.m_onExit = state.m_onUpdate = nullptr;
        for (auto& handler : state.m_handlers) handler.m_onReceipt = handler.m_onSend = nullptr;
    }
}

OBL_TEST_CASE("script_program_diff.free_command_keeps_the_head_and_frees_nested_lists")
{
    Parsed p(Rich());
    ScriptCommand* head = p.script->m_states[0].m_onEntry;
    OBL_REQUIRE(head != nullptr);
    Memory memory = p.heap.memory();
    FreeCommand(*head, memory); // everything but the head node and its own payload
    OBL_CHECK(head->m_next == nullptr);
    OBL_CHECK(head->m_data != nullptr);
    OBL_CHECK_EQ(p.heap.foreign, 0);
    // The payload is gone too; release the head node by hand and detach it so the fixture does not free it twice.
    memory.release(head);
    p.script->m_states[0].m_onEntry = nullptr;
    OBL_CHECK(p.heap.live.empty());
}
