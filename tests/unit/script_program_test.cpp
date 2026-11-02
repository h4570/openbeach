
#include "unit_test.h"

#include "game/objects/script_program_api.h"
#include "game/objects/script_program_ids.h"
#include "game/objects/script_program_types.h"
#include "game/objects/script_program.h"
#include "game/objects/script_teardown.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>

using namespace obl::script_program;
using obl::script::ProgramState;
using obl::script::ScriptProgram;

namespace {
std::vector<void*> g_lifecycleReleased;
void RecordRelease(void* p)
{
    g_lifecycleReleased.push_back(p);
}
const Memory kLifecycleMemory{nullptr, &RecordRelease};
} // namespace

OBL_TEST_CASE("script_program.lifecycle_construct_preserves_text")
{
    auto script = std::make_unique<ScriptProgram>();
    std::memset(script.get(), 0xa5, sizeof(*script));
    const char text[] = "existing cursor";
    script->m_text = text;
    Construct(*script);
    OBL_CHECK(script->m_text == text);
    OBL_CHECK(script->m_NumStates == 0 && !script->m_openCommand && !script->m_openSwitch);
    for (const auto& state : script->m_states) {
        OBL_CHECK(state.m_stateId == 0x0fffffff && state.m_NumHandlers == 0);
        OBL_CHECK(!state.m_onEntry && !state.m_onExit && !state.m_onUpdate);
        for (const auto& handler : state.m_handlers)
            OBL_CHECK(handler.m_messageId == 0x0fffffff && !handler.m_onReceipt && !handler.m_onSend);
    }
}

OBL_TEST_CASE("script_program.lifecycle_free_tail_first_retains_head_and_payload")
{
    obl::script_program::FunctionCommand a{1}, b{2};
    ScriptCommand tail{nullptr, obl::script_program::OpFunction, &b};
    ScriptCommand head{&tail, obl::script_program::OpFunction, &a};
    g_lifecycleReleased.clear();
    FreeCommand(head, kLifecycleMemory);
    OBL_CHECK(g_lifecycleReleased == std::vector<void*>({&b, &tail, &a}));
    OBL_CHECK(head.m_next == nullptr && head.m_data == &a && head.m_opcode == obl::script_program::OpFunction);
    int unsupportedData = 123;
    ScriptCommand unrecognized{nullptr, 12, &unsupportedData};
    head = {&unrecognized, -1, &unsupportedData};
    g_lifecycleReleased.clear();
    FreeCommand(head, kLifecycleMemory);
    OBL_CHECK(g_lifecycleReleased == std::vector<void*>({&unrecognized}));
    OBL_CHECK(!head.m_next && head.m_data == &unsupportedData);
}

OBL_TEST_CASE("script_program.lifecycle_owned_operand_kinds_and_null_delete")
{
    int block = 42;
    for (int kind : {0x36, 0xbc, 0xc2, 0xc6, 0xd2, 0xd3, 0xd4, 0xd5, 0xe3, 0xe4, 0xe8, 0, 0x110, -1}) {
        obl::script_program::ArithmeticCommand data{{kind, &block}, {kind, nullptr}};
        ScriptCommand node{nullptr, obl::script_program::OpSet, &data};
        g_lifecycleReleased.clear();
        FreeCommand(node, kLifecycleMemory);
        const bool owns = kind != 0 && kind != 0x110 && kind != -1;
        OBL_CHECK(g_lifecycleReleased == (owns ? std::vector<void*>({&block, nullptr, &data}) : std::vector<void*>({&data})));
        OBL_CHECK(data.m_target.m_data == &block && node.m_data == &data);
    }
}

OBL_TEST_CASE("script_program.lifecycle_all_states_handlers_reverse_no_clear")
{
    auto script = std::make_unique<ScriptProgram>();
    Construct(*script);
    obl::script_program::FunctionCommand a{1}, b{2}, c{3}, d{4};
    ScriptCommand entry{nullptr, obl::script_program::OpFunction, &a}, update{nullptr, obl::script_program::OpFunction, &b};
    ScriptCommand receipt{nullptr, obl::script_program::OpFunction, &c}, send{nullptr, obl::script_program::OpFunction, &d};
    script->m_states[59].m_onEntry = &entry;
    script->m_states[59].m_handlers[29].m_onReceipt = &receipt;
    script->m_states[59].m_handlers[0].m_onSend = &send;
    script->m_states[0].m_onUpdate = &update;
    script->m_NumStates = 0; // destruction ignores both counts
    g_lifecycleReleased.clear();
    DestructStates(*script, kLifecycleMemory);
    OBL_CHECK(g_lifecycleReleased == std::vector<void*>({&a, &entry, &c, &receipt, &d, &send, &b, &update}));
    OBL_CHECK(script->m_states[59].m_onEntry == &entry && script->m_states[0].m_onUpdate == &update);
}

namespace {

Vocabulary SmallVocabulary()
{
    Vocabulary vocabulary;
    vocabulary.states = {{0, "STATE_NULL"}, {1, "STATE_A"}, {2, "STATE_B"}};
    vocabulary.messages = {{0, "MT_ONE"}, {1, "MT_TWO"}, {2, "MT_THREE"}};
    vocabulary.functions = {{0, "DoFirst"}, {1, "DoSecond"}, {116, "ResetIdle"}, {139, "ResetIdle"}};
    vocabulary.values = {{0, "Sender"}, {5, "MessData"}, {0x40, "ActionState"}, {0xe9, "State"}};
    return vocabulary;
}

// Counts blocks so the tests can tell that Destroy returns exactly what Parse took.
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
    Parsed() { std::memset(script.get(), 0, sizeof(ScriptProgram)); }
    ~Parsed()
    {
        Destroy(*script, heap.memory());
        for (void* block : heap.live) std::free(block);
        heap.live.clear();
    }
};

// Parses `text` (a decoded file: the terminator is appended here as the file reader does).
void ParseText(Parsed& parsed, const std::string& text, const Vocabulary& vocabulary)
{
    std::string buffer = text + ".";
    Parse(*parsed.script, buffer.data(), text.size(), vocabulary, parsed.heap.memory());
}

const ScriptCommand* Only(const ScriptCommand* list)
{
    return list && !list->m_next ? list : nullptr;
}

template<typename T> const T& Payload(const ScriptCommand* node)
{
    return *static_cast<const T*>(node->m_data);
}

int32_t Integer(const obl::script_program::ScriptOperand& value)
{
    int32_t result;
    std::memcpy(&result, value.m_data, sizeof(result));
    return result;
}

float Number(const obl::script_program::ScriptOperand& value)
{
    float result;
    std::memcpy(&result, value.m_data, sizeof(result));
    return result;
}

} // namespace

OBL_TEST_CASE("script_program.layout_sizes")
{
    OBL_CHECK_EQ(sizeof(obl::script_program::MessageCommand), (sizeof(void*) == 4 ? size_t{0x3c} : size_t{112}));
    OBL_CHECK_EQ(sizeof(obl::script_program::IfCommand), (sizeof(void*) == 4 ? size_t{0x20} : size_t{56}));
    OBL_CHECK_EQ(sizeof(obl::script_program::AnimationCommand), (sizeof(void*) == 4 ? size_t{0x18} : size_t{32}));
    OBL_CHECK_EQ(sizeof(obl::script_program::SwitchCase), (sizeof(void*) == 4 ? size_t{0x10} : size_t{32}));
}

OBL_TEST_CASE("script_program.state_with_all_lists")
{
    Vocabulary vocabulary = SmallVocabulary();
    Parsed parsed;
    ParseText(parsed,
            "// comment\r\n"
            "State STATE_A\r\n"
            "  OnEntry Function DoFirst; End\r\n"
            "  OnExit Function DoSecond; End\r\n"
            "  OnUpdate Function ResetIdle; End\r\n"
            "  Message MT_TWO\r\n"
            "    OnReceipt Function DoFirst; End\r\n"
            "    OnSend Function DoSecond; End\r\n"
            "  End\r\n"
            "  Message MT_ONE\r\n"
            "    OnSend Function DoFirst; End\r\n"
            "  End\r\n"
            "End\r\n"
            "State STATE_B\r\nEnd\r\n",
            vocabulary);
    const ScriptProgram& script = *parsed.script;
    OBL_REQUIRE(script.m_NumStates == 2);
    const ProgramState& a = script.m_states[0];
    OBL_CHECK_EQ(a.m_stateId, 1);
    OBL_CHECK_EQ(Payload<obl::script_program::FunctionCommand>(Only(a.m_onEntry)).m_functionId, 0);
    OBL_CHECK_EQ(Payload<obl::script_program::FunctionCommand>(Only(a.m_onExit)).m_functionId, 1);
    OBL_CHECK_EQ(Payload<obl::script_program::FunctionCommand>(Only(a.m_onUpdate)).m_functionId,
            116); // the first of two rows of the same name wins
    OBL_REQUIRE(a.m_NumHandlers == 2);
    OBL_CHECK_EQ(a.m_handlers[0].m_messageId, 1);
    OBL_CHECK(Only(a.m_handlers[0].m_onReceipt) && Only(a.m_handlers[0].m_onSend));
    OBL_CHECK_EQ(a.m_handlers[1].m_messageId, 0);
    OBL_CHECK(!a.m_handlers[1].m_onReceipt && Only(a.m_handlers[1].m_onSend));
    OBL_CHECK_EQ(script.m_states[1].m_stateId, 2);
    OBL_CHECK(!script.m_states[1].m_onEntry);
    OBL_CHECK_EQ(script.m_states[1].m_NumHandlers, 0u);
}

OBL_TEST_CASE("script_program.values_and_arithmetic")
{
    Vocabulary vocabulary = SmallVocabulary();
    Parsed parsed;
    ParseText(parsed,
            "State STATE_A OnEntry\n"
            " Set ActionState 3;\n Add actionstate -2.5;\n Minus ActionState MessData;\n Multiply ActionState Random7;\n"
            " Divide ActionState RandDWORD12;\n Set Sender STATE_B;\n Set MessData \"word\";\n PlaySound beep;\n"
            "End End\n",
            vocabulary);
    const ScriptCommand* node = parsed.script->m_states[0].m_onEntry;
    OBL_REQUIRE(node != nullptr);
    OBL_CHECK_EQ(node->m_opcode, static_cast<int>(obl::script_program::OpSet));
    auto& set = Payload<obl::script_program::ArithmeticCommand>(node);
    OBL_CHECK_EQ(set.m_target.m_kind, 0x40);
    OBL_CHECK(set.m_target.m_data == nullptr);
    OBL_CHECK_EQ(set.m_source.m_kind, static_cast<int>(obl::script_program::ValueNumber));
    OBL_CHECK_EQ(Number(set.m_source), 3.0f);

    node = node->m_next;
    OBL_CHECK_EQ(node->m_opcode, static_cast<int>(obl::script_program::OpAdd));
    OBL_CHECK_EQ(Payload<obl::script_program::ArithmeticCommand>(node).m_target.m_kind, 0x40); // names compare ignoring case
    OBL_CHECK_EQ(Number(Payload<obl::script_program::ArithmeticCommand>(node).m_source), -2.5f);

    node = node->m_next;
    OBL_CHECK_EQ(node->m_opcode, static_cast<int>(obl::script_program::OpMinus));
    OBL_CHECK_EQ(Payload<obl::script_program::ArithmeticCommand>(node).m_source.m_kind, 5);

    node = node->m_next;
    OBL_CHECK_EQ(node->m_opcode, static_cast<int>(obl::script_program::OpMultiply));
    OBL_CHECK_EQ(Payload<obl::script_program::ArithmeticCommand>(node).m_source.m_kind, static_cast<int>(obl::script_program::ValueRandom));
    OBL_CHECK_EQ(Integer(Payload<obl::script_program::ArithmeticCommand>(node).m_source), 7); // `Random<n>` keeps n as an int

    node = node->m_next;
    OBL_CHECK_EQ(node->m_opcode, static_cast<int>(obl::script_program::OpDivide));
    OBL_CHECK_EQ(
            Payload<obl::script_program::ArithmeticCommand>(node).m_source.m_kind, static_cast<int>(obl::script_program::ValueRandDword));
    OBL_CHECK_EQ(Integer(Payload<obl::script_program::ArithmeticCommand>(node).m_source), 12);

    node = node->m_next;
    OBL_CHECK_EQ(Number(Payload<obl::script_program::ArithmeticCommand>(node).m_source), 2.0f); // STATE_B
    node = node->m_next;
    OBL_CHECK_EQ(Payload<obl::script_program::ArithmeticCommand>(node).m_source.m_kind, static_cast<int>(obl::script_program::ValueString));
    OBL_CHECK_EQ(std::string(static_cast<const char*>(Payload<obl::script_program::ArithmeticCommand>(node).m_source.m_data)), "word");
    node = node->m_next;
    OBL_CHECK_EQ(node->m_opcode, static_cast<int>(obl::script_program::OpPlaySound));
}

OBL_TEST_CASE("script_program.if_else_and_nesting")
{
    Vocabulary vocabulary = SmallVocabulary();
    Parsed parsed;
    ParseText(parsed,
            "State STATE_A OnUpdate\n"
            " If (ActionState == 1)\n"
            "   Function DoFirst;\n"
            "   If (MessData)\n Function DoSecond; End\n"
            "   Function ResetIdle;\n"
            " Else\n"
            "   Function DoSecond;\n"
            " End\n"
            " Function DoFirst;\n"
            " If ( State  !=  \"abc\" )\n End\n"
            "End End\n",
            vocabulary);
    const ScriptCommand* first = parsed.script->m_states[0].m_onUpdate;
    OBL_REQUIRE(first != nullptr);
    OBL_CHECK_EQ(first->m_opcode, static_cast<int>(obl::script_program::OpIf));
    auto& outer = Payload<obl::script_program::IfCommand>(first);
    OBL_CHECK_EQ(outer.m_left.m_kind, 0x40);
    OBL_CHECK_EQ(outer.m_op, 4);
    OBL_CHECK_EQ(outer.m_right.m_kind, static_cast<int>(obl::script_program::ValueNumber));
    OBL_CHECK_EQ(outer.m_inElse, 1);
    // then: Function, If, Function; else: Function
    int thenCount = 0;
    for (const ScriptCommand* n = outer.m_then; n; n = n->m_next) ++thenCount;
    OBL_CHECK_EQ(thenCount, 3);
    OBL_CHECK(Only(outer.m_else) != nullptr);
    auto& inner = Payload<obl::script_program::IfCommand>(outer.m_then->m_next);
    OBL_CHECK_EQ(inner.m_right.m_kind, static_cast<int>(obl::script_program::ValueNone));
    OBL_CHECK(Only(inner.m_then) != nullptr);
    OBL_CHECK(!inner.m_else);
    // After the outer If the commands are again on the state's list.
    const ScriptCommand* tail = first->m_next;
    OBL_REQUIRE(tail != nullptr);
    OBL_CHECK_EQ(tail->m_opcode, static_cast<int>(obl::script_program::OpFunction));
    auto& text = Payload<obl::script_program::IfCommand>(tail->m_next);
    OBL_CHECK_EQ(text.m_right.m_kind, static_cast<int>(obl::script_program::ValueString));
    OBL_CHECK_EQ(std::string(static_cast<const char*>(text.m_right.m_data)), "abc");
    OBL_CHECK_EQ(text.m_op, 7);
}

OBL_TEST_CASE("script_program.if_operators")
{
    Vocabulary vocabulary = SmallVocabulary();
    const char* operators[] = {"&&", "||", ">", "<", "==", "<=", ">=", "!="};
    for (int i = 0; i < 8; ++i) {
        Parsed parsed;
        ParseText(parsed, std::string("State STATE_A OnUpdate\n If (Sender ") + operators[i] + " 1)\n End\nEnd End\n", vocabulary);
        OBL_CHECK_EQ(Payload<obl::script_program::IfCommand>(parsed.script->m_states[0].m_onUpdate).m_op, i);
    }
    Parsed alias;
    ParseText(alias, "State STATE_A OnUpdate\n If (Sender =< 1)\n End\n If (Sender => 1)\n End\nEnd End\n", vocabulary);
    OBL_CHECK_EQ(Payload<obl::script_program::IfCommand>(alias.script->m_states[0].m_onUpdate).m_op, 5);
    OBL_CHECK_EQ(Payload<obl::script_program::IfCommand>(alias.script->m_states[0].m_onUpdate->m_next).m_op, 6);
}

OBL_TEST_CASE("script_program.switch_cases")
{
    Vocabulary vocabulary = SmallVocabulary();
    Parsed parsed;
    ParseText(parsed,
            "State STATE_A OnUpdate\n"
            " Switch (ActionState)\n"
            "  Case 0:\n   Function DoFirst;\n"
            "  Case 1:\n   If (Sender)\n Function DoSecond; End\n   Function ResetIdle;\n"
            "  Case Random3:\n"
            " End\n"
            " Function DoFirst;\n"
            "End End\n",
            vocabulary);
    const ScriptCommand* node = parsed.script->m_states[0].m_onUpdate;
    OBL_REQUIRE(node != nullptr);
    OBL_CHECK_EQ(node->m_opcode, static_cast<int>(obl::script_program::OpSwitch));
    auto& control = Payload<obl::script_program::SwitchCommand>(node);
    OBL_CHECK_EQ(control.m_value.m_kind, 0x40);
    const obl::script_program::SwitchCase* a = control.m_firstCase;
    OBL_REQUIRE(a && a->m_next && a->m_next->m_next);
    OBL_CHECK_EQ(Number(a->m_value), 0.0f);
    OBL_CHECK(Only(a->m_commands) != nullptr);
    const obl::script_program::SwitchCase* b = a->m_next;
    OBL_CHECK_EQ(b->m_commands->m_opcode,
            static_cast<int>(obl::script_program::OpIf)); // the If inside the Case, then the Function after its End
    OBL_CHECK_EQ(b->m_commands->m_next->m_opcode, static_cast<int>(obl::script_program::OpFunction));
    const obl::script_program::SwitchCase* c = b->m_next;
    OBL_CHECK_EQ(c->m_value.m_kind, static_cast<int>(obl::script_program::ValueRandom));
    OBL_CHECK(!c->m_commands);
    OBL_CHECK(node->m_next && node->m_next->m_opcode == obl::script_program::OpFunction); // after the Switch: state level again
}

OBL_TEST_CASE("script_program.message_and_animation_options")
{
    Vocabulary vocabulary = SmallVocabulary();
    Parsed parsed;
    ParseText(parsed,
            "State STATE_A OnEntry\n"
            " Message MT_TWO Sender 1 2;\n"
            " Message MT_THREE Sender 1 2 Time 1.5 MaxTime 3 FloatData 0.25 State STATE_B SendFrom MessData;\n"
            " ChangeState STATE_B;\n ChangeState STATE_A Time 2;\n ChangeState STATE_NOWHERE;\n"
            " PlayAnimation walk;\n"
            " PlayAnimation ActionState Finish stand NotLooped Reverse ExitState STATE_B MaxTime 3.5 EndMessage MT_TWO;\n"
            "End End\n",
            vocabulary);
    const ScriptCommand* node = parsed.script->m_states[0].m_onEntry;
    auto& plain = Payload<obl::script_program::MessageCommand>(node);
    OBL_CHECK_EQ(plain.m_messageId, 1);
    OBL_CHECK_EQ(plain.m_receiver.m_kind, 0);
    OBL_CHECK_EQ(plain.m_floatData.m_kind, static_cast<int>(obl::script_program::ValueNone));
    OBL_CHECK_EQ(plain.m_state.m_kind, static_cast<int>(obl::script_program::ValueNone));
    OBL_CHECK_EQ(plain.m_sendFrom.m_kind, static_cast<int>(obl::script_program::ValueNone));
    OBL_CHECK_EQ(plain.m_time, 0.0f);

    node = node->m_next;
    auto& full = Payload<obl::script_program::MessageCommand>(node);
    OBL_CHECK_EQ(full.m_messageId, 2);
    OBL_CHECK_EQ(full.m_time, 1.5f);
    OBL_CHECK_EQ(full.m_maxTime, 3.0f);
    OBL_CHECK_EQ(Number(full.m_floatData), 0.25f);
    OBL_CHECK_EQ(Number(full.m_state), 2.0f);
    OBL_CHECK_EQ(full.m_sendFrom.m_kind, 5);

    node = node->m_next;
    OBL_CHECK_EQ(Payload<obl::script_program::ChangeStateCommand>(node).m_stateId, 2);
    node = node->m_next;
    OBL_CHECK_EQ(Payload<obl::script_program::ChangeStateCommand>(node).m_stateId, 1);
    OBL_CHECK_EQ(Payload<obl::script_program::ChangeStateCommand>(node).m_time, 2.0f);
    node = node->m_next;
    OBL_CHECK_EQ(Payload<obl::script_program::ChangeStateCommand>(node).m_stateId, 0); // an unknown name is not an error

    node = node->m_next;
    auto& walk = Payload<obl::script_program::AnimationCommand>(node);
    OBL_CHECK_EQ(std::string(walk.m_name), "walk");
    OBL_CHECK(!walk.m_finish);
    OBL_CHECK_EQ(walk.m_endMessage, obl::script_program::kNoMessage);
    OBL_CHECK_EQ(walk.m_exitState, 0);

    node = node->m_next;
    auto& animation = Payload<obl::script_program::AnimationCommand>(node);
    OBL_CHECK_EQ(std::string(animation.m_name), "ActionState");
    OBL_CHECK_EQ(std::string(animation.m_finish), "stand");
    OBL_CHECK_EQ(animation.m_notLooped, 1);
    OBL_CHECK_EQ(animation.m_reverse, 1);
    OBL_CHECK_EQ(animation.m_exitState, 2);
    OBL_CHECK_EQ(animation.m_maxTime, 3.5f);
    OBL_CHECK_EQ(animation.m_endMessage, 1);
}

OBL_TEST_CASE("script_program.rejects_malformed_scripts")
{
    Vocabulary vocabulary = SmallVocabulary();
    const char* bad[] = {
            "Strange\n", // not a `State`
            "State STATE_NOWHERE\nEnd\n", // unknown state name
            "State STATE_A OnEntry Function Nope; End End\n", // unknown function
            "State STATE_A OnEntry Message MT_NOPE Sender 1 2; End End\n",
            "State STATE_A OnEntry Set Mystery 1; End End\n", // not a number, not a variable
            "State STATE_A OnEntry Else End End\n", // Else without an If
            "State STATE_A OnEntry If (Sender ~ 1)\n End End End\n", // unknown operator
            "State STATE_A OnEntry Swoosh; End End\n", // unknown command
            "State STATE_A OnEntry Case 1: End End\n", // Case outside a Switch
            "State STATE_A OnEntry Function DoFirst;\n", // the text ends inside the block
            "State STATE_A OnEntry Switch (Sender)\n Function DoFirst; End End End\n", // no Case before a command
            "State STATE_A Wobble End\n", // unknown block
    };
    for (const char* text : bad) {
        Parsed parsed;
        bool failed = false;
        try {
            ParseText(parsed, text, vocabulary);
        } catch (const ParseError&) {
            failed = true;
        }
        if (!failed) ::obl::unit::ReportFailure(__FILE__, __LINE__, std::string("accepted: ") + text);
        Destroy(*parsed.script, parsed.heap.memory()); // a failed parse may leave blocks no list owns yet (the game exits there)
    }
}

OBL_TEST_CASE("script_program.destroy_returns_every_block")
{
    Vocabulary vocabulary = SmallVocabulary();
    Parsed parsed;
    ParseText(parsed,
            "State STATE_A\n"
            " OnEntry Set Sender \"text\"; Add State RandDWORD3; PlaySound x; PlayAnimation w Finish f; ChangeState STATE_B Time 1; End\n"
            " OnUpdate If (Sender == \"q\") Function DoFirst; Else Message MT_ONE Sender 1 2 FloatData 1 State STATE_A SendFrom Sender; "
            "End\n"
            "   Switch (State) Case 1: Function DoFirst; Case \"s\": End End\n"
            " Message MT_ONE OnReceipt Function DoFirst; End OnSend Function DoSecond; End End\n"
            "End\n",
            vocabulary);
    OBL_CHECK(!parsed.heap.live.empty());
    Destroy(*parsed.script, parsed.heap.memory());
    OBL_CHECK(parsed.heap.live.empty());
    OBL_CHECK_EQ(parsed.heap.foreign, 0);
    OBL_CHECK_EQ(parsed.script->m_NumStates, 0u);
    Destroy(*parsed.script, parsed.heap.memory()); // a second call does nothing
    OBL_CHECK_EQ(parsed.heap.foreign, 0);
}

OBL_TEST_CASE("script_program.diff_finds_changes")
{
    Vocabulary vocabulary = SmallVocabulary();
    const std::string base = "State STATE_A OnEntry Set Sender 1; If (Sender == 2) Function DoFirst; End End End\n";
    Parsed a, b;
    ParseText(a, base, vocabulary);
    ParseText(b, base, vocabulary);
    OBL_CHECK_EQ(Diff(*a.script, *b.script), std::string());
    const char* changed[] = {
            "State STATE_A OnEntry Set Sender 2; If (Sender == 2) Function DoFirst; End End End\n",
            "State STATE_A OnEntry Set Sender 1; If (Sender != 2) Function DoFirst; End End End\n",
            "State STATE_A OnEntry Set Sender 1; If (Sender == 2) Function DoSecond; End End End\n",
            "State STATE_B OnEntry Set Sender 1; If (Sender == 2) Function DoFirst; End End End\n",
            "State STATE_A OnEntry Set Sender 1; If (Sender == 2) Function DoFirst; Function DoFirst; End End End\n",
            "State STATE_A OnEntry Set MessData 1; If (Sender == 2) Function DoFirst; End End End\n",
    };
    for (const char* text : changed) {
        Parsed other;
        ParseText(other, text, vocabulary);
        OBL_CHECK(!Diff(*a.script, *other.script).empty());
    }
}

OBL_TEST_CASE("script_program.script_ids_json")
{
    std::string error;
    Vocabulary vocabulary;
    OBL_CHECK(!obl::object::ParseScriptIds("not json", vocabulary, error));
    OBL_CHECK(!obl::object::ParseScriptIds("{\"format\":\"obl.object_script_vocabulary\",\"version\":2}", vocabulary, error));
    OBL_CHECK(!obl::object::ParseScriptIds("{\"format\":\"obl.object_script_vocabulary\",\"version\":1,\"states\":[]}", vocabulary, error));
    OBL_CHECK(vocabulary.states.empty());
    OBL_CHECK(vocabulary.functions.empty());

    // A valid file names states and messages only; the functions and values are the engine's script API.
    std::string json =
            "{\"format\":\"obl.object_script_vocabulary\",\"version\":1,\"states\":[{\"id\":0,\"name\":\"STATE_NULL\"}],\"messages\":[";
    for (int i = 0; i < 108; ++i)
        json += (i ? ",{\"id\":" : "{\"id\":") + std::to_string(i) + ",\"name\":\"MT_" + std::to_string(i) + "\"}";
    json += "]}";
    OBL_REQUIRE(obl::object::ParseScriptIds(json, vocabulary, error));
    OBL_CHECK_EQ(vocabulary.messages.size(), 108u);
    OBL_CHECK_EQ(vocabulary.functions.size(), obl::script_program::EngineFunctions().size());
    OBL_CHECK_EQ(vocabulary.values.size(), obl::script_program::EngineValues().size());
}

// ---- Shipped data -------------------------------------------------------------------------------------------------------------------

namespace {

bool LoadVocabulary(Vocabulary& vocabulary)
{
    std::filesystem::path file = std::filesystem::path(OBL_GAME_DIR) / "packs" / "base" / "scripts" / "object_vocabulary.json";
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::string json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string error;
    return obl::object::ParseScriptIds(json, vocabulary, error);
}

std::string ReadDecoded(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::string data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    for (char& c : data) c = static_cast<char>(c ^ 0xFF);
    return data;
}

} // namespace

OBL_TEST_CASE("data.script_program_parses_shipped_scripts")
{
    const std::filesystem::path directory = std::filesystem::path(OBL_GAME_DIR) / "Data" / "Script" / "Object";
    Vocabulary vocabulary;
    if (!std::filesystem::exists(directory) || !LoadVocabulary(vocabulary)) OBL_SKIP("run/ has no imported game data");
    // Number of `State` blocks per file (docs/architecture/data-formats/converter/scripts.md).
    const struct {
        const char* file;
        uint32_t states;
    } expected[] = {
            {"guest.ble", 51},
            {"cleaner.ble", 20},
            {"lifeguard.ble", 15},
            {"security.ble", 19},
            {"mechanic.ble", 12},
            {"Representative.ble", 10},
            {"Builder.ble", 14},
            {"Shark.ble", 4},
    };
    for (const auto& item : expected) {
        std::string text = ReadDecoded(directory / item.file);
        OBL_REQUIRE(!text.empty());
        Parsed parsed;
        ParseText(parsed, text, vocabulary);
        OBL_CHECK_EQ(parsed.script->m_NumStates, item.states);
        ListStats stats = Count(*parsed.script);
        OBL_CHECK(stats.nodes > 50);
        // Parsing twice gives equal structures, and Destroy returns every block.
        Parsed again;
        ParseText(again, text, vocabulary);
        OBL_CHECK_EQ(Diff(*parsed.script, *again.script), std::string());
        Destroy(*parsed.script, parsed.heap.memory());
        OBL_CHECK(parsed.heap.live.empty());
        OBL_CHECK_EQ(parsed.heap.foreign, 0);
    }
}

OBL_TEST_CASE("data.script_program_guest_shape")
{
    const std::filesystem::path file = std::filesystem::path(OBL_GAME_DIR) / "Data" / "Script" / "Object" / "guest.ble";
    Vocabulary vocabulary;
    if (!std::filesystem::exists(file) || !LoadVocabulary(vocabulary)) OBL_SKIP("run/ has no imported game data");
    Parsed parsed;
    ParseText(parsed, ReadDecoded(file), vocabulary);
    ListStats stats = Count(*parsed.script);
    // Every opcode the grammar has appears in the guest logic except Divide and Multiply.
    OBL_CHECK(stats.opcodes[obl::script_program::OpFunction] > 100);
    OBL_CHECK(stats.opcodes[obl::script_program::OpMessage] > 20);
    OBL_CHECK(stats.opcodes[obl::script_program::OpChangeState] > 50);
    OBL_CHECK(stats.opcodes[obl::script_program::OpAnimation] > 50);
    OBL_CHECK(stats.opcodes[obl::script_program::OpIf] > 100);
    OBL_CHECK(stats.opcodes[obl::script_program::OpSwitch] > 5);
    OBL_CHECK(stats.opcodes[obl::script_program::OpSet] > 50);
    OBL_CHECK_EQ(stats.opcodes[obl::script_program::OpDivide], 0);
    OBL_CHECK_EQ(stats.opcodes[obl::script_program::OpMultiply], 0);
}

OBL_TEST_CASE("script_program.lifecycle_destruct_preserves_fields_and_frees_handlers_backwards")
{
    struct Env {
        std::vector<void*> trace;
        void FreeCommands(obl::script::ScriptCommand* p) { trace.push_back(p); }
        void Delete(void* p) { trace.push_back(p); }
    } env;
    ProgramState state{};
    obl::script::ScriptCommand commands[63]{};
    state.m_onEntry = &commands[0];
    state.m_onExit = &commands[1];
    state.m_onUpdate = &commands[2];
    for (int i = 0; i < 30; ++i) {
        state.m_handlers[i].m_onReceipt = &commands[3 + 2 * i];
        state.m_handlers[i].m_onSend = &commands[4 + 2 * i];
    }
    const auto before = state;
    obl::script_teardown::Destruct(state, env);
    OBL_CHECK_EQ(env.trace.size(), 126u);
    OBL_CHECK(std::memcmp(&before, &state, sizeof(state)) == 0);
    for (int i = 0; i < 3; ++i) {
        OBL_CHECK(env.trace[i * 2] == &commands[i]);
        OBL_CHECK(env.trace[i * 2 + 1] == &commands[i]);
    }
    for (int i = 0; i < 30; ++i) {
        OBL_CHECK(env.trace[6 + 4 * i] == &commands[3 + 2 * (29 - i)]);
        OBL_CHECK(env.trace[8 + 4 * i] == &commands[4 + 2 * (29 - i)]);
    }
}

OBL_TEST_CASE("script_program.lifecycle_operand_free_covers_all_owned_kinds")
{
    struct Env {
        int calls = 0;
        void* last = nullptr;
        void Delete(void* p)
        {
            ++calls;
            last = p;
        }
    } env;
    int payload = 1;
    const int kinds[] = {0x36, 0xbc, 0xc2, 0xc6, 0xd2, 0xd3, 0xd4, 0xd5, 0xe3, 0xe4, 0xe8};
    for (int kind = 0; kind < 512; ++kind) {
        bool owned = false;
        for (int k : kinds) owned |= k == kind;
        obl::script_program::ScriptOperand value{kind, &payload};
        const int before = env.calls;
        obl::script_teardown::Free(value, env);
        OBL_CHECK_EQ(env.calls - before, owned ? 1 : 0);
        OBL_CHECK(value.m_data == &payload);
        OBL_CHECK_EQ(value.m_kind, kind);
    }
    obl::script_program::ScriptOperand value{0x36, nullptr};
    obl::script_teardown::Free(value, env);
    OBL_CHECK(env.last == nullptr);
}
