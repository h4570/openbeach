// Execution of the object scripts (src/game/objects/script_program_exec.h): the interpreter of the command list perform routine and its helpers against a mock host,
// and the state/handler lookup of the object script run on routine*/SendMessage*.

#include "unit_test.h"

#include "game/objects/script_program_types.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/messages/message_state.h"
#include "game/objects/script_program_exec.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace obl::script_program::exec;
using obl::script::ScriptValue;

namespace {

ScriptValue Byte(int32_t v)
{
    return {kTypeByte, v};
}
ScriptValue Int(int32_t v)
{
    return {kTypeInt, v};
}
ScriptValue Float(float v)
{
    return {kTypeFloat, FloatToBits(v)};
}
ScriptValue Text(const char* s)
{
    return {kTypeText, obl::script::text::Id(s)};
}

// A script object that answers GetValue from a table and logs everything the interpreter does to it.
struct MockHost {
    std::map<int32_t, ScriptValue> vars;
    std::vector<std::string> log;
    std::deque<int> randValues;
    obl::messages::Message lastMessage{};
    float lastDelay = -1.0f;
    int32_t id = 77;
    int32_t state = 5;

    ScriptValue GetValue(int32_t key, void* data)
    {
        log.push_back("get " + std::to_string(key));
        if (key == kKeyNumber) return {kTypeFloat, *static_cast<int32_t*>(data)};
        if (key == kKeyText) return {kTypeText, obl::script::text::Id(static_cast<const char*>(data))};
        if (key == kKeyTrue) return Byte(1);
        if (key == kKeyFalse) return Byte(0);
        if (key == kKeyRandDword) return Int(Rand() % *static_cast<int32_t*>(data));
        if (key == kKeyRandom) return Byte(Rand() % *static_cast<int32_t*>(data) == 0);
        return vars[key];
    }
    void SetValue(int32_t key, void*, int32_t type, int32_t bits)
    {
        log.push_back("set " + std::to_string(key) + " " + std::to_string(type) + " " + std::to_string(bits));
        vars[key] = {type, bits};
    }
    void CallFunction(int32_t functionId) { log.push_back("call " + std::to_string(functionId)); }
    void Send(const obl::messages::Message& m)
    {
        log.push_back("send");
        lastMessage = m;
        lastDelay = -1.0f;
    }
    void SendDelayed(const obl::messages::Message& m, float delay)
    {
        log.push_back("sendDelayed");
        lastMessage = m;
        lastDelay = delay;
    }
    float PlayAnimation(const char* name, int32_t reverse, int32_t exitState, int32_t loop, const char* finish)
    {
        log.push_back(std::string("anim ") + name + " " + std::to_string(reverse) + " " + std::to_string(exitState) + " " +
                std::to_string(loop) + " " + (finish ? finish : "-"));
        return 1.5f;
    }
    void PlaySound(const char* name) { log.push_back(std::string("sound ") + name); }
    int32_t Id() const { return id; }
    int32_t State() const { return state; }
    int Rand()
    {
        log.push_back("rand");
        if (randValues.empty()) return 0;
        int v = randValues.front();
        randValues.pop_front();
        return v;
    }
    int32_t FloatToIntWrap(int32_t bits) { return Truncate(BitsToFloat(bits)); }
    int Stricmp(const char* a, const char* b)
    {
        for (;; ++a, ++b) {
            int x = std::tolower(static_cast<unsigned char>(*a)), y = std::tolower(static_cast<unsigned char>(*b));
            if (x != y) return x < y ? -1 : 1;
            if (x == 0) return 0;
        }
    }
    void UndefinedCommand() { log.push_back("fatal"); }
};

// Owns the nodes and payloads of a hand-built script.
struct Builder {
    std::vector<std::shared_ptr<void>> keep;
    template<class T> T* Make()
    {
        auto p = std::make_shared<T>();
        std::memset(p.get(), 0, sizeof(T));
        keep.push_back(p);
        return p.get();
    }
    obl::script::ScriptCommand* Node(int opcode, void* data, obl::script::ScriptCommand* next = nullptr)
    {
        auto* node = Make<obl::script::ScriptCommand>();
        node->m_opcode = opcode;
        node->m_data = data;
        node->m_next = next;
        return node;
    }
    int32_t* IntData(int32_t v)
    {
        auto* p = Make<int32_t>();
        *p = v;
        return p;
    }
    int32_t* FloatData(float v)
    {
        auto* p = Make<int32_t>();
        *p = FloatToBits(v);
        return p;
    }
    obl::script::ScriptCommand* Function(int id, obl::script::ScriptCommand* next = nullptr)
    {
        auto* data = Make<obl::script_program::FunctionCommand>();
        data->m_functionId = id;
        return Node(obl::script_program::OpFunction, data, next);
    }
    obl::script::ScriptCommand* Arith(
            int opcode, int32_t targetKey, int32_t sourceKey, void* sourceData = nullptr, obl::script::ScriptCommand* next = nullptr)
    {
        auto* data = Make<obl::script_program::ArithmeticCommand>();
        data->m_target = {targetKey, nullptr};
        data->m_source = {sourceKey, sourceData};
        return Node(opcode, data, next);
    }
    obl::script::ScriptCommand* If(obl::script_program::ScriptOperand left, obl::script_program::ScriptOperand right, int op,
            obl::script::ScriptCommand* thenList, obl::script::ScriptCommand* elseList = nullptr,
            obl::script::ScriptCommand* next = nullptr)
    {
        auto* data = Make<obl::script_program::IfCommand>();
        data->m_left = left;
        data->m_right = right;
        data->m_op = op;
        data->m_then = thenList;
        data->m_else = elseList;
        return Node(obl::script_program::OpIf, data, next);
    }
};

bool Cond(MockHost& host, ScriptValue left, ScriptValue right, int op)
{
    host.vars[1] = left;
    host.vars[2] = right;
    return EvalCondition(host, 1, nullptr, 2, nullptr, op);
}

} // namespace

OBL_TEST_CASE("script_program_exec.condition_default_and_bare")
{
    MockHost host;
    OBL_CHECK(EvalCondition(host, kKeyDefault, nullptr, 2, nullptr, kOpEqual));
    OBL_CHECK(EvalCondition(host, 1, nullptr, kKeyDefault, nullptr, kOpAnd));
    OBL_CHECK(host.log.empty()); // Default is answered without asking the object
    host.vars[1] = Byte(0x100); // only the low byte of a byte counts
    OBL_CHECK(!EvalCondition(host, 1, nullptr, kKeyNone, nullptr, 0));
    host.vars[1] = Byte(0x101);
    OBL_CHECK(EvalCondition(host, 1, nullptr, kKeyNone, nullptr, 0));
    host.vars[1] = Int(0);
    OBL_CHECK(!EvalCondition(host, 1, nullptr, kKeyNone, nullptr, 0));
    host.vars[1] = Int(-1);
    OBL_CHECK(EvalCondition(host, 1, nullptr, kKeyNone, nullptr, 0));
    host.vars[1] = Float(0.0f);
    OBL_CHECK(!EvalCondition(host, 1, nullptr, kKeyNone, nullptr, 0));
    host.vars[1] = Float(0.25f);
    OBL_CHECK(EvalCondition(host, 1, nullptr, kKeyNone, nullptr, 0));
    host.vars[1] = Float(std::numeric_limits<float>::quiet_NaN());
    OBL_CHECK(!EvalCondition(host, 1, nullptr, kKeyNone, nullptr, 0)); // unordered counts as equal to zero
    host.vars[1] = Text("x");
    OBL_CHECK(!EvalCondition(host, 1, nullptr, kKeyNone, nullptr, 0));
}

OBL_TEST_CASE("script_program_exec.condition_quoted_text")
{
    MockHost host;
    host.vars[1] = Text("Hello");
    const char* right = "hELLO";
    OBL_CHECK(EvalCondition(host, 1, nullptr, kKeyText, const_cast<char*>(right), kOpEqual));
    OBL_CHECK(!EvalCondition(host, 1, nullptr, kKeyText, const_cast<char*>(right), kOpNotEqual));
    host.log.clear();
    OBL_CHECK(!EvalCondition(host, 1, nullptr, kKeyText, const_cast<char*>(right), kOpGreater)); // only == and != exist for a text
    OBL_CHECK(host.log.empty());
    const char* other = "bye";
    OBL_CHECK(!EvalCondition(host, 1, nullptr, kKeyText, const_cast<char*>(other), kOpEqual));
    OBL_CHECK(EvalCondition(host, 1, nullptr, kKeyText, const_cast<char*>(other), kOpNotEqual));
}

OBL_TEST_CASE("script_program_exec.condition_logic_operators")
{
    MockHost host;
    OBL_CHECK(Cond(host, Int(1), Float(2.5f), kOpAnd));
    OBL_CHECK(!Cond(host, Int(0), Int(1), kOpAnd));
    OBL_CHECK(!Cond(host, Int(1), Float(0.5f), kOpAnd)); // 0.5 truncates to 0
    OBL_CHECK(!Cond(host, Text("a"), Int(1), kOpAnd)); // a text is 0
    OBL_CHECK(Cond(host, Int(0), Int(7), kOpOr));
    OBL_CHECK(Cond(host, Int(5), Int(0), kOpOr));
    OBL_CHECK(!Cond(host, Int(0), Float(0.9f), kOpOr));
    OBL_CHECK(Cond(host, Byte(0x1ff), Int(0), kOpOr)); // the byte is masked to 0xff
    OBL_CHECK(!Cond(host, Byte(0x100), Int(0), kOpOr));
    host.log.clear();
    OBL_CHECK(Cond(host, Int(1), Int(1), kOpOr));
    OBL_CHECK_EQ(host.log.size(), size_t(2)); // both sides are fetched even when the left decides
    OBL_CHECK(!Cond(host, Int(1), Int(1), 8)); // an unknown operator is false
}

OBL_TEST_CASE("script_program_exec.condition_float_order")
{
    MockHost host;
    OBL_CHECK(Cond(host, Float(2.0f), Float(1.0f), kOpGreater));
    OBL_CHECK(!Cond(host, Float(1.0f), Float(1.0f), kOpGreater));
    OBL_CHECK(Cond(host, Float(1.0f), Float(2.0f), kOpLess));
    OBL_CHECK(!Cond(host, Float(2.0f), Float(2.0f), kOpLess));
    // Mixed kinds: a byte and an int convert (int as an unsigned number), a text is 0.
    OBL_CHECK(Cond(host, Int(-1), Float(1.0f), kOpGreater)); // 4294967295 > 1
    OBL_CHECK(Cond(host, Byte(3), Int(2), kOpGreater));
    OBL_CHECK(Cond(host, Text("a"), Int(1), kOpLess));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(Cond(host, Float(nan), Float(1.0f), kOpGreater)); // an unordered compare reads as "below" for >
    OBL_CHECK(!Cond(host, Float(nan), Float(1.0f), kOpLess));
    OBL_CHECK(!Cond(host, Float(1.0f), Float(nan), kOpLess));
}

OBL_TEST_CASE("script_program_exec.condition_less_equal_greater_equal")
{
    MockHost host;
    // An int on the left compares unsigned against the right side as an integer.
    OBL_CHECK(Cond(host, Int(3), Int(3), kOpLessEqual));
    OBL_CHECK(Cond(host, Int(3), Int(4), kOpLessEqual));
    OBL_CHECK(!Cond(host, Int(-1), Int(4), kOpLessEqual)); // 0xffffffff <= 4 is false
    OBL_CHECK(Cond(host, Int(2), Float(3.9f), kOpLessEqual)); // 3.9 -> 3
    OBL_CHECK(!Cond(host, Int(4), Float(3.9f), kOpLessEqual));
    OBL_CHECK(Cond(host, Int(3), Int(3), kOpGreaterEqual));
    OBL_CHECK(Cond(host, Int(-1), Int(4), kOpGreaterEqual));
    OBL_CHECK(!Cond(host, Int(3), Int(4), kOpGreaterEqual));
    OBL_CHECK(Cond(host, Int(4), Float(3.9f), kOpGreaterEqual));
    // Any other left kind compares as floats.
    OBL_CHECK(Cond(host, Float(1.5f), Float(1.5f), kOpLessEqual));
    OBL_CHECK(Cond(host, Float(1.5f), Float(1.6f), kOpLessEqual));
    OBL_CHECK(!Cond(host, Float(1.7f), Float(1.6f), kOpLessEqual));
    OBL_CHECK(Cond(host, Float(1.5f), Float(1.5f), kOpGreaterEqual));
    OBL_CHECK(Cond(host, Float(1.7f), Float(1.6f), kOpGreaterEqual));
    OBL_CHECK(!Cond(host, Float(1.5f), Float(1.6f), kOpGreaterEqual));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(!Cond(host, Float(nan), Float(1.0f), kOpLessEqual));
    OBL_CHECK(Cond(host, Float(nan), Float(1.0f), kOpGreaterEqual));
}

OBL_TEST_CASE("script_program_exec.condition_equality")
{
    MockHost host;
    const char* a = "same";
    const char* b = "same";
    const char* c = "Same";
    OBL_CHECK(Cond(host, Text(a), Text(b), kOpEqual));
    OBL_CHECK(Cond(host, Text(a), Text(c), kOpEqual)); // == on two texts ignores case
    OBL_CHECK(Cond(host, Int(5), Int(5), kOpEqual));
    OBL_CHECK(Cond(host, Int(5), Float(5.9f), kOpEqual)); // the float is truncated
    OBL_CHECK(Cond(host, Int(0xff), Byte(0x1ff), kOpEqual));
    OBL_CHECK(Cond(host, Int(0), Text("x"), kOpEqual)); // a text on the right is 0 next to an int
    OBL_CHECK(Cond(host, Float(1.0f), Float(1.000005f), kOpEqual)); // within 1e-5
    OBL_CHECK(!Cond(host, Float(1.0f), Float(1.0001f), kOpEqual));
    OBL_CHECK(Cond(host, Byte(3), Int(3), kOpEqual));
    OBL_CHECK(Cond(host, Float(std::numeric_limits<float>::quiet_NaN()), Float(1.0f), kOpEqual)); // NaN counts as equal for ==
    OBL_CHECK(!Cond(host, Int(5), Int(5), kOpNotEqual));
    OBL_CHECK(Cond(host, Int(5), Int(6), kOpNotEqual));
    OBL_CHECK(!Cond(host, Int(5), Float(5.5f), kOpNotEqual));
    OBL_CHECK(Cond(host, Float(1.0f), Float(1.000005f), kOpNotEqual)); // != has no tolerance
    OBL_CHECK(!Cond(host, Float(1.0f), Float(1.0f), kOpNotEqual));
    OBL_CHECK(!Cond(host, Float(std::numeric_limits<float>::quiet_NaN()), Float(1.0f), kOpNotEqual)); // and NaN is not "different"
    OBL_CHECK(Cond(host, Text(a), Int(0), kOpNotEqual) == false); // a text on the left of != is 0.0
}

OBL_TEST_CASE("script_program_exec.build_message_defaults_and_conversions")
{
    MockHost host;
    obl::script_program::MessageCommand command{};
    command.m_messageId = 42;
    command.m_receiver = {1, nullptr};
    command.m_data = {2, nullptr};
    command.m_flags = {3, nullptr};
    command.m_floatData = {kKeyNone, nullptr};
    command.m_state = {kKeyNone, nullptr};
    command.m_sendFrom = {kKeyNone, nullptr};
    host.vars[1] = Float(12.9f); // a float receiver is truncated
    host.vars[2] = Byte(0x1ab); // a byte is masked
    host.vars[3] = Int(-7);
    obl::messages::Message m = BuildMessage(host, command);
    OBL_CHECK_EQ(m.type, 42);
    OBL_CHECK_EQ(m.receiver, 12);
    OBL_CHECK_EQ(m.data, 0xab);
    OBL_CHECK_EQ(m.flags, -7);
    OBL_CHECK_EQ(m.sender, 77); // the running object
    OBL_CHECK_EQ(m.extra, 5); // its state
    OBL_CHECK_EQ(m.floatData, 0.0f);
    OBL_CHECK_EQ(host.log.size(), size_t(3)); // receiver, data, flags
    OBL_CHECK_EQ(host.log[0], std::string("get 1"));
    OBL_CHECK_EQ(host.log[1], std::string("get 2"));
    OBL_CHECK_EQ(host.log[2], std::string("get 3"));
}

OBL_TEST_CASE("script_program_exec.build_message_optional_values")
{
    MockHost host;
    obl::script_program::MessageCommand command{};
    command.m_receiver = {1, nullptr};
    command.m_data = {1, nullptr};
    command.m_flags = {1, nullptr};
    command.m_floatData = {4, nullptr};
    command.m_state = {5, nullptr};
    command.m_sendFrom = {6, nullptr};
    host.vars[1] = Int(1);
    host.vars[4] = Int(-2); // an int FloatData is converted as an unsigned number
    host.vars[5] = Float(9.7f); // the state of a float is truncated
    host.vars[6] = Text(""); // a text sender keeps its bits
    obl::messages::Message m = BuildMessage(host, command);
    OBL_CHECK_EQ(m.floatData, static_cast<float>(4294967294.0));
    OBL_CHECK_EQ(m.extra, 9);
    OBL_CHECK_EQ(m.sender, host.vars[6].bits);
    host.vars[4] = Byte(0x181);
    host.vars[6] = Int(31);
    m = BuildMessage(host, command);
    OBL_CHECK_EQ(m.floatData, 129.0f);
    OBL_CHECK_EQ(m.sender, 31);
    host.vars[4] = Float(0.75f);
    m = BuildMessage(host, command);
    OBL_CHECK_EQ(m.floatData, 0.75f);
}

OBL_TEST_CASE("script_program_exec.message_command_timing")
{
    Builder b;
    auto* command = b.Make<obl::script_program::MessageCommand>();
    command->m_messageId = 9;
    command->m_receiver = {1, nullptr};
    command->m_data = {1, nullptr};
    command->m_flags = {1, nullptr};
    command->m_floatData = {kKeyNone, nullptr};
    command->m_state = {kKeyNone, nullptr};
    command->m_sendFrom = {kKeyNone, nullptr};
    auto* node = b.Node(obl::script_program::OpMessage, command);
    MockHost host;
    host.vars[1] = Int(3);

    Perform(host, node); // no time: sent at once
    OBL_CHECK_EQ(host.log.back(), std::string("send"));

    command->m_time = 2.5f;
    Perform(host, node); // Time only: delayed by Time
    OBL_CHECK_EQ(host.log.back(), std::string("sendDelayed"));
    OBL_CHECK_EQ(host.lastDelay, 2.5f);

    host.log.clear();
    command->m_time = 1.0f;
    command->m_maxTime = 3.0f;
    host.randValues = {16383};
    Perform(host, node); // MaxTime: the message is built first, then rand() picks the delay in [Time, MaxTime]
    OBL_CHECK_EQ(host.log.size(), size_t(5));
    OBL_CHECK_EQ(host.log[3], std::string("rand"));
    OBL_CHECK_EQ(host.log[4], std::string("sendDelayed"));
    OBL_CHECK(std::fabs(host.lastDelay - (1.0f + 2.0f * 16383.0f / 32767.0f)) < 1e-4f);
    host.randValues = {0};
    Perform(host, node);
    OBL_CHECK_EQ(host.lastDelay, 1.0f);

    command->m_time = 0.0f; // MaxTime alone counts from zero
    host.randValues = {32767};
    Perform(host, node);
    OBL_CHECK(std::fabs(host.lastDelay - 3.0f) < 1e-4f);
}

OBL_TEST_CASE("script_program_exec.change_state_and_animation")
{
    Builder b;
    auto* change = b.Make<obl::script_program::ChangeStateCommand>();
    change->m_stateId = 11;
    MockHost host;
    Perform(host, b.Node(obl::script_program::OpChangeState, change));
    OBL_CHECK_EQ(host.log.back(), std::string("send"));
    OBL_CHECK_EQ(host.lastMessage.type, int(obl::messages::MT_STATECHANGE));
    OBL_CHECK_EQ(host.lastMessage.data, 11);
    OBL_CHECK_EQ(host.lastMessage.sender, 77);
    OBL_CHECK_EQ(host.lastMessage.receiver, 77);
    change->m_time = 0.5f;
    Perform(host, b.Node(obl::script_program::OpChangeState, change));
    OBL_CHECK_EQ(host.log.back(), std::string("sendDelayed"));
    OBL_CHECK_EQ(host.lastDelay, 0.5f);

    auto* anim = b.Make<obl::script_program::AnimationCommand>();
    anim->m_endMessage = obl::script_program::kNoMessage;
    char walk[] = "walk", stand[] = "stand";
    anim->m_name = walk;
    host.log.clear();
    Perform(host, b.Node(obl::script_program::OpAnimation, anim)); // plain: looped, no exit state
    OBL_CHECK_EQ(host.log[0], std::string("anim walk 0 -1 1 -"));
    anim->m_notLooped = 1;
    anim->m_reverse = 1;
    Perform(host, b.Node(obl::script_program::OpAnimation, anim));
    OBL_CHECK_EQ(host.log[1], std::string("anim walk 1 -1 0 -"));
    anim->m_exitState = 7;
    Perform(host, b.Node(obl::script_program::OpAnimation, anim)); // an exit state: no loop flag
    OBL_CHECK_EQ(host.log[2], std::string("anim walk 1 7 0 -"));
    anim->m_notLooped = 0;
    anim->m_finish = stand;
    Perform(host, b.Node(obl::script_program::OpAnimation, anim));
    OBL_CHECK_EQ(host.log[3], std::string("anim walk 1 7 0 stand"));
    OBL_CHECK_EQ(host.log.size(), size_t(4)); // no EndMessage, no message

    anim->m_endMessage = 21;
    host.state = 8;
    Perform(host, b.Node(obl::script_program::OpAnimation, anim)); // EndMessage: to itself after the animation's duration
    OBL_CHECK_EQ(host.log.back(), std::string("sendDelayed"));
    OBL_CHECK_EQ(host.lastDelay, 1.5f);
    OBL_CHECK_EQ(host.lastMessage.type, 21);
    OBL_CHECK_EQ(host.lastMessage.sender, 77);
    OBL_CHECK_EQ(host.lastMessage.receiver, 77);
    OBL_CHECK_EQ(host.lastMessage.extra, 8);
}

OBL_TEST_CASE("script_program_exec.arithmetic_and_set")
{
    Builder b;
    MockHost host;
    host.vars[10] = Int(10);
    host.vars[11] = Float(2.5f);
    host.vars[12] = Float(1.5f);
    auto* sub = b.Arith(obl::script_program::OpMinus, 10, 11, nullptr); // int - float: the float truncates
    auto* add = b.Arith(obl::script_program::OpAdd, 12, 10, nullptr, sub); // float + int: the int counts as unsigned
    Perform(host, add);
    OBL_CHECK_EQ(host.vars[12].type, kTypeFloat);
    OBL_CHECK_EQ(host.vars[12].bits, FloatToBits(11.5f));
    OBL_CHECK_EQ(host.vars[10].type, kTypeInt);
    OBL_CHECK_EQ(host.vars[10].bits, 8);
    // Set copies the source value including its kind; a literal is a float.
    int32_t* literal = b.FloatData(4.0f);
    Perform(host, b.Arith(obl::script_program::OpSet, 13, kKeyNumber, literal));
    OBL_CHECK_EQ(host.vars[13].type, kTypeFloat);
    OBL_CHECK_EQ(host.vars[13].bits, FloatToBits(4.0f));
    Perform(host, b.Arith(obl::script_program::OpMultiply, 13, 11));
    OBL_CHECK_EQ(host.vars[13].bits, FloatToBits(10.0f));
    Perform(host, b.Arith(obl::script_program::OpDivide, 10, 11)); // 8 / 2
    OBL_CHECK_EQ(host.vars[10].bits, 4);
    // The target is fetched before the source.
    host.log.clear();
    Perform(host, b.Arith(obl::script_program::OpAdd, 10, 11));
    OBL_CHECK_EQ(host.log[0], std::string("get 10"));
    OBL_CHECK_EQ(host.log[1], std::string("get 11"));
}

OBL_TEST_CASE("script_program_exec.if_else_and_nested")
{
    Builder b;
    MockHost host;
    host.vars[1] = Int(1);
    auto* thenList = b.Function(5);
    auto* elseList = b.Function(6);
    Perform(host, b.If({1, nullptr}, {kKeyNone, nullptr}, 0, thenList, elseList));
    OBL_CHECK_EQ(host.log.back(), std::string("call 5"));
    host.vars[1] = Int(0);
    Perform(host, b.If({1, nullptr}, {kKeyNone, nullptr}, 0, thenList, elseList));
    OBL_CHECK_EQ(host.log.back(), std::string("call 6"));
    host.log.clear();
    Perform(host, b.If({1, nullptr}, {kKeyNone, nullptr}, 0, thenList, nullptr)); // false with no else
    OBL_CHECK_EQ(host.log.size(), size_t(1));
    // The commands after an If run whatever it did; Function 0xb4 does nothing.
    host.log.clear();
    Perform(host, b.If({1, nullptr}, {kKeyNone, nullptr}, 0, thenList, elseList, b.Function(kNoFunction, b.Function(8))));
    OBL_CHECK_EQ(host.log.back(), std::string("call 8"));
    OBL_CHECK_EQ(host.log.size(), size_t(3));
}

OBL_TEST_CASE("script_program_exec.switch_cases")
{
    Builder b;
    MockHost host;
    host.vars[1] = Float(2.0f);
    auto* sw = b.Make<obl::script_program::SwitchCommand>();
    sw->m_value = {1, nullptr};
    auto* c3 = b.Make<obl::script_program::SwitchCase>();
    c3->m_value = {kKeyDefault, nullptr};
    c3->m_commands = b.Function(30);
    auto* c2 = b.Make<obl::script_program::SwitchCase>();
    c2->m_value = {kKeyNumber, b.FloatData(2.0f)};
    c2->m_commands = b.Function(20);
    c2->m_next = c3;
    auto* c1 = b.Make<obl::script_program::SwitchCase>();
    c1->m_value = {kKeyNumber, b.FloatData(1.0f)};
    c1->m_commands = b.Function(10);
    c1->m_next = c2;
    sw->m_firstCase = c1;
    auto* node = b.Node(obl::script_program::OpSwitch, sw);
    Perform(host, node);
    OBL_CHECK_EQ(host.log.back(), std::string("call 20")); // only the first matching case runs
    host.vars[1] = Float(9.0f);
    Perform(host, node);
    OBL_CHECK_EQ(host.log.back(), std::string("call 30")); // Default
    c3->m_commands = nullptr;
    host.log.clear();
    Perform(host, node); // a matching case without commands ends the switch
    OBL_CHECK(host.log.size() > 0);
    OBL_CHECK(host.log.back() != std::string("call 30"));
}

OBL_TEST_CASE("script_program_exec.switch_on_random")
{
    Builder b;
    MockHost host;
    auto* sw = b.Make<obl::script_program::SwitchCommand>();
    sw->m_value = {kKeyRandDword, b.IntData(5)};
    auto* c2 = b.Make<obl::script_program::SwitchCase>();
    c2->m_value = {kKeyNumber, b.FloatData(3.0f)};
    c2->m_commands = b.Function(30);
    auto* c1 = b.Make<obl::script_program::SwitchCase>();
    c1->m_value = {kKeyNumber, b.FloatData(1.0f)};
    c1->m_commands = b.Function(10);
    c1->m_next = c2;
    sw->m_firstCase = c1;
    auto* node = b.Node(obl::script_program::OpSwitch, sw);
    host.randValues = {8}; // 8 % 5 == 3
    Perform(host, node);
    OBL_CHECK_EQ(host.log.back(), std::string("call 30"));
    host.randValues = {6}; // 1
    Perform(host, node);
    OBL_CHECK_EQ(host.log.back(), std::string("call 10"));
    host.log.clear();
    host.randValues = {4}; // no case
    Perform(host, node);
    OBL_CHECK_EQ(host.log.back() != std::string("call 10") && host.log.back() != std::string("call 30"), true);

    // Random<n>: the byte becomes true/false and the cases compare against it.
    sw->m_value = {kKeyRandom, b.IntData(4)};
    c1->m_value = {kKeyTrue, nullptr};
    c2->m_value = {kKeyFalse, nullptr};
    host.randValues = {8}; // 8 % 4 == 0 -> true
    Perform(host, node);
    OBL_CHECK_EQ(host.log.back(), std::string("call 10"));
    host.randValues = {9}; // false
    Perform(host, node);
    OBL_CHECK_EQ(host.log.back(), std::string("call 30"));
}

OBL_TEST_CASE("script_program_exec.play_sound_and_unrecognized_opcodes")
{
    Builder b;
    MockHost host;
    auto* sound = b.Make<obl::script_program::SoundCommand>();
    char name[] = "splash";
    sound->m_name = name;
    Perform(host, b.Node(obl::script_program::OpPlaySound, sound, b.Node(12, nullptr, b.Node(0x20, nullptr, b.Function(3)))));
    OBL_CHECK_EQ(host.log[0], std::string("sound splash"));
    OBL_CHECK_EQ(host.log[1], std::string("call 3")); // opcodes 12..0x19 and above 0x1a are skipped
    OBL_CHECK_EQ(host.log.size(), size_t(2));
    Perform(host, b.Node(0x1a, nullptr));
    OBL_CHECK_EQ(host.log.back(), std::string("fatal"));
}

OBL_TEST_CASE("script_program_exec.state_and_handler_lookup")
{
    Builder b;
    auto* script = b.Make<obl::script::ScriptProgram>();
    obl::script::ScriptCommand* entryA = b.Function(1);
    obl::script::ScriptCommand* entryB = b.Function(2);
    obl::script::ScriptCommand* receipt = b.Function(3);
    obl::script::ScriptCommand* send = b.Function(4);
    script->m_NumStates = 3;
    script->m_states[0].m_stateId = 7;
    script->m_states[0].m_onEntry = entryA;
    script->m_states[1].m_stateId = 5; // the first state with a number wins; a second one with the same number is ignored
    script->m_states[1].m_onExit = entryA;
    script->m_states[1].m_NumHandlers = 3;
    script->m_states[1].m_handlers[0] = {50, nullptr, nullptr};
    script->m_states[1].m_handlers[1] = {51, receipt, nullptr};
    script->m_states[1].m_handlers[2] = {51, nullptr, send}; // a second handler of the same type is not the one RunOnReceipt takes
    script->m_states[2].m_stateId = 5;
    script->m_states[2].m_onEntry = entryB;

    OBL_CHECK(SelectStateList(*script, 7, &obl::script::ProgramState::m_onEntry) == entryA);
    OBL_CHECK(SelectStateList(*script, 5, &obl::script::ProgramState::m_onEntry) == nullptr);
    OBL_CHECK(SelectStateList(*script, 5, &obl::script::ProgramState::m_onExit) == entryA);
    OBL_CHECK(SelectStateList(*script, 99, &obl::script::ProgramState::m_onEntry) == nullptr);

    ReceiptChoice choice = SelectReceipt(*script, 5, 50);
    OBL_CHECK(choice.taken && choice.list == nullptr); // a handler without an OnReceipt still takes the message
    choice = SelectReceipt(*script, 5, 51);
    OBL_CHECK(choice.taken && choice.list == receipt);
    choice = SelectReceipt(*script, 5, 52);
    OBL_CHECK(!choice.taken);
    OBL_CHECK(!SelectReceipt(*script, 7, 51).taken);

    OBL_CHECK(SelectSend(*script, 5, 50) == nullptr);
    OBL_CHECK(SelectSend(*script, 5, 51) == send); // handlers without an OnSend are skipped
    OBL_CHECK(SelectSend(*script, 6, 51) == nullptr);

    MockHost host;
    host.state = 5;
    obl::messages::Message message{};
    message.type = 51;
    OBL_CHECK(RunOnReceipt(host, *script, message));
    OBL_CHECK_EQ(host.log.back(), std::string("call 3"));
    host.log.clear();
    message.type = 50;
    OBL_CHECK(RunOnReceipt(host, *script, message));
    OBL_CHECK(host.log.empty());
    message.type = 52;
    OBL_CHECK(!RunOnReceipt(host, *script, message));
    message.type = 51;
    RunOnSend(host, *script, message);
    OBL_CHECK_EQ(host.log.back(), std::string("call 4"));
    host.log.clear();
    RunStateList(host, *script, 7, &obl::script::ProgramState::m_onEntry);
    OBL_CHECK_EQ(host.log.back(), std::string("call 1"));
}

OBL_TEST_CASE("script_text.ids_name_texts_by_address")
{
    namespace text = obl::script::text;
    static const char kA[] = "Walk", kB[] = "Walk";
    const int32_t id = text::Id(kA);
    OBL_CHECK_EQ(text::Id(kA), id); // the same address keeps its id
    OBL_CHECK(text::Id(kB) != id); // equal contents at another address are another text, as the pointers were
    OBL_CHECK_EQ(text::Of(id), kA);
    OBL_CHECK_EQ(text::Id(nullptr), 0);
    OBL_CHECK(text::Of(0) == nullptr);
    OBL_CHECK(text::Of(7) == nullptr); // a plain number is no text
    OBL_CHECK(text::Of(static_cast<int32_t>(text::kIdTag | 0xffffffu)) == nullptr); // a tagged number past the table
}
