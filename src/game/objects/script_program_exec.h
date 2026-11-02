#pragma once

#include "core/ascii_case.h"
#include "game/messages/message_state.h"
#include "game/objects/script_program_types.h"
#include "script_arith.h"
#include "script_text.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// Object scripts run state entry, exit, update and message handlers through an injected Host.
// The interpreter evaluates conditions, builds messages and compares script values without depending on game state directly.
// Unit tests and call-drive check the order of host calls, outgoing messages and returned values.
// src/game/objects/script_program_run.cpp adapts Host to the object's virtual methods:
//   GetValue / SetValue read and write script values: type 0 byte, 1 text, 2 integer, 3 float.
//   CallFunction invokes a built-in script function by id.
//   Send / SendDelayed deliver immediate or delayed messages.
//   PlayAnimation starts an animation and returns its duration.
//   PlaySound plays a positional sound at the object.
//   Id / State read object fields; Rand and FloatToIntWrap supply random draws and float-to-integer conversion.
//   Stricmp compares strings without ASCII case; UndefinedCommand reports an unsupported opcode.
namespace obl::script_program::exec {

using script::ScriptCommand;
using script::ScriptValue;

// Operand ids with special handling in the interpreter.
constexpr int32_t kKeyText = 0x36, kKeyRandDword = 0xe3, kKeyRandom = 0xe4, kKeyTrue = 0xe6, kKeyFalse = 0xe7, kKeyNumber = 0xe8,
                  kKeyDefault = 0xeb, kKeyNone = 0x110;
// `Function` with this id is skipped (the parser's "no function").
constexpr int32_t kNoFunction = 0xb4;

// Condition operators of `If`/`Switch` (the parser's numbers).
constexpr int32_t kOpAnd = 0, kOpOr = 1, kOpGreater = 2, kOpLess = 3, kOpEqual = 4, kOpLessEqual = 5, kOpGreaterEqual = 6, kOpNotEqual = 7;

constexpr int32_t kTypeByte = 0, kTypeText = 1, kTypeInt = 2, kTypeFloat = 3;

// `rand` is scaled by this float (1/32767) to the 0..1 fraction that picks a delay between `Time` and `MaxTime`.
constexpr float kRandScale = 3.0518509447574615e-05f;
// The tolerance of `==` between floats (the double 9.999999747378752e-06, i.e. 1e-5f).
constexpr double kEqualTolerance = static_cast<double>(1e-5f);

inline float BitsToFloat(int32_t bits)
{
    return script::BitsToFloat(bits);
}
inline int32_t FloatToBits(float value)
{
    return script::FloatToBits(value);
}

// Truncation to int: NaN and values outside int give the "integer indefinite" 0x80000000.
inline int32_t Truncate(float value)
{
    return (value >= -2147483648.0f && value < 2147483648.0f) ? static_cast<int32_t>(value) : INT32_MIN;
}

// Compare of a value against a float: below, equal, or (for an unordered pair) both, so a NaN
// counts as "less" and as "equal" for the tests below.
struct Ordering {
    bool below, equal;
};

inline Ordering Compare(double value, float other)
{
    if (std::isnan(value) || std::isnan(other)) return {true, true};
    return {value < other, value == other};
}

// Script-to-integer conversion: byte masked, int as is, float truncated, anything else 0.
template<class Host> int32_t ToInt(Host& host, ScriptValue value)
{
    switch (value.type) {
    case kTypeByte: return value.bits & 0xff;
    case kTypeInt: return value.bits;
    case kTypeFloat: return host.FloatToIntWrap(value.bits);
    default: return 0;
    }
}

// Script-to-float conversion in double precision: byte, int (as an unsigned 32-bit number) and float; anything else 0.
inline double ToFloatWide(ScriptValue value)
{
    switch (value.type) {
    case kTypeByte: return static_cast<double>(value.bits & 0xff);
    case kTypeInt: return static_cast<double>(static_cast<uint32_t>(value.bits));
    case kTypeFloat: return static_cast<double>(BitsToFloat(value.bits));
    default: return 0.0;
    }
}

// The float the interpreter stores for the left side of `<`/`>` (stored as float): ToFloat rounded to single precision.
inline float ToFloatNarrow(ScriptValue value)
{
    return static_cast<float>(ToFloatWide(value));
}

// A text value's bits are a script::text id (a text address does not fit 32 bits on x64).
inline const char* AsText(int32_t bits)
{
    return script::text::Of(bits);
}

// Script equality comparison: both sides are fetched (left first); a text on the left compares byte by byte (case counts) with the
// right side's bits taken as a text, an int on the left compares with the right side as an integer (a text on the right is 0), any
// other left side compares as floats with a tolerance of 1e-5 (a NaN on either side counts as equal).
template<class Host> bool IsEqual(Host& host, int32_t leftKey, void* leftData, int32_t rightKey, void* rightData)
{
    const ScriptValue left = host.GetValue(leftKey, leftData);
    const ScriptValue right = host.GetValue(rightKey, rightData);
    if (left.type == kTypeText)
        return ascii::EqualsNoCase(AsText(left.bits), AsText(right.bits)); // content names may arrive in any spelling
    if (left.type == kTypeInt) return left.bits == ToInt(host, right);
    const double l = left.type == kTypeByte ? static_cast<double>(left.bits & 0xff)
            : left.type == kTypeFloat       ? static_cast<double>(BitsToFloat(left.bits))
                                            : 0.0;
    const double r = ToFloatWide(right);
    return !(std::fabs(l - r) > kEqualTolerance);
}

// Script inequality comparison: like IsEqual without the text branch (a text on the left counts as 0.0) and without the tolerance;
// a NaN on either side is "equal", so `!=` is false for it.
template<class Host> bool IsNotEqual(Host& host, int32_t leftKey, void* leftData, int32_t rightKey, void* rightData)
{
    const ScriptValue left = host.GetValue(leftKey, leftData);
    const ScriptValue right = host.GetValue(rightKey, rightData);
    if (left.type == kTypeInt) return left.bits != ToInt(host, right);
    const double l = left.type == kTypeByte ? static_cast<double>(left.bits & 0xff)
            : left.type == kTypeFloat       ? static_cast<double>(BitsToFloat(left.bits))
                                            : 0.0;
    const double r = ToFloatWide(right);
    return !(std::isnan(l) || std::isnan(r) || l == r);
}

// Evaluating a script condition: `If (left [op right])` and the comparison of a `Switch` with a `Case` (op 4). `Default` on either
// side is true without asking the object. A bare `If (value)` (right key 0x110) is true for a non-zero byte/int/float (NaN is false) and
// never for a text. A quoted text on the right allows only `==` and `!=` (case-insensitive, against the left side's bits taken as a text).
template<class Host> bool EvalCondition(Host& host, int32_t leftKey, void* leftData, int32_t rightKey, void* rightData, int32_t op)
{
    if (leftKey == kKeyDefault || rightKey == kKeyDefault) return true;
    if (rightKey == kKeyNone) {
        const ScriptValue value = host.GetValue(leftKey, leftData);
        switch (value.type) {
        case kTypeByte: return (value.bits & 0xff) != 0;
        case kTypeInt: return value.bits != 0;
        case kTypeFloat: return !Compare(0.0, BitsToFloat(value.bits)).equal;
        default: return false;
        }
    }
    if (rightKey == kKeyText) {
        if (op != kOpEqual && op != kOpNotEqual) return false;
        const ScriptValue left = host.GetValue(leftKey, leftData);
        const int different = host.Stricmp(AsText(left.bits), static_cast<const char*>(rightData));
        return op == kOpEqual ? different == 0 : different != 0;
    }
    switch (op) {
    case kOpAnd: {
        const ScriptValue left = host.GetValue(leftKey, leftData);
        const ScriptValue right = host.GetValue(rightKey, rightData);
        return ToInt(host, left) != 0 && ToInt(host, right) != 0;
    }
    case kOpOr: {
        const ScriptValue left = host.GetValue(leftKey, leftData);
        const ScriptValue right = host.GetValue(rightKey, rightData);
        return ToInt(host, left) != 0 || ToInt(host, right) != 0;
    }
    case kOpGreater:
    case kOpLess: {
        const ScriptValue left = host.GetValue(leftKey, leftData);
        const ScriptValue right = host.GetValue(rightKey, rightData);
        const Ordering c = Compare(ToFloatWide(right), ToFloatNarrow(left));
        return op == kOpGreater ? c.below : (!c.below && !c.equal);
    }
    case kOpEqual: return IsEqual(host, leftKey, leftData, rightKey, rightData);
    case kOpLessEqual: {
        const ScriptValue left = host.GetValue(leftKey, leftData);
        const ScriptValue right = host.GetValue(rightKey, rightData);
        if (left.type == kTypeInt) // unsigned comparison of the raw int with the right side as an integer
            return !(static_cast<uint32_t>(ToInt(host, right)) < static_cast<uint32_t>(left.bits));
        return !Compare(ToFloatWide(right), ToFloatNarrow(left)).below;
    }
    case kOpGreaterEqual: {
        const ScriptValue left = host.GetValue(leftKey, leftData);
        const ScriptValue right = host.GetValue(rightKey, rightData);
        if (left.type == kTypeInt) {
            const uint32_t l = static_cast<uint32_t>(ToInt(host, left));
            const uint32_t r = static_cast<uint32_t>(ToInt(host, right));
            return !(l < r);
        }
        const Ordering c = Compare(ToFloatWide(right), ToFloatNarrow(left));
        return c.below || c.equal;
    }
    case kOpNotEqual: return IsNotEqual(host, leftKey, leftData, rightKey, rightData);
    default: return false;
    }
}

// A value a `Message` argument or the sender is read as: byte masked, int as is, float truncated.
template<class Host> int32_t MessageInteger(Host& host, ScriptValue value)
{
    if (value.type == kTypeByte) return value.bits & 0xff;
    if (value.type == kTypeInt) return value.bits;
    return host.FloatToIntWrap(value.bits);
}

// The sender and the state are taken the same way except that a text or an int stays as it is and only a float is truncated.
template<class Host> int32_t MessageWord(Host& host, ScriptValue value)
{
    if (value.type == kTypeByte) return value.bits & 0xff;
    if (value.type == kTypeFloat) return host.FloatToIntWrap(value.bits);
    return value.bits;
}

// Building an object script message: the 8-word message of a `Message MT_x receiver data flags [FloatData v] [State v] [SendFrom v]`
// command. The values are fetched in the order receiver, data, flags, FloatData, State, SendFrom; omitted ones default to the running
// object's state / id. `time` (set by the post) and an omitted `floatData` are zero.
template<class Host> messages::Message BuildMessage(Host& host, const MessageCommand& command)
{
    messages::Message message{};
    message.type = command.m_messageId;
    message.receiver = MessageInteger(host, host.GetValue(command.m_receiver.m_kind, command.m_receiver.m_data));
    message.data = MessageInteger(host, host.GetValue(command.m_data.m_kind, command.m_data.m_data));
    message.flags = MessageInteger(host, host.GetValue(command.m_flags.m_kind, command.m_flags.m_data));
    if (command.m_floatData.m_kind != kKeyNone) {
        const ScriptValue value = host.GetValue(command.m_floatData.m_kind, command.m_floatData.m_data);
        float floatData;
        if (value.type == kTypeByte)
            floatData = static_cast<float>(value.bits & 0xff);
        else if (value.type == kTypeInt)
            floatData = static_cast<float>(static_cast<uint32_t>(value.bits));
        else
            floatData = BitsToFloat(value.bits);
        message.floatData = floatData;
    }
    if (command.m_state.m_kind != kKeyNone)
        message.extra = MessageWord(host, host.GetValue(command.m_state.m_kind, command.m_state.m_data));
    else
        message.extra = host.State();
    if (command.m_sendFrom.m_kind != kKeyNone)
        message.sender = MessageWord(host, host.GetValue(command.m_sendFrom.m_kind, command.m_sendFrom.m_data));
    else
        message.sender = host.Id();
    return message;
}

// The delay of a `Message` with `MaxTime`: Time + (MaxTime - Time) * rand * (1/32767), double arithmetic stored as a float.
inline float RandomDelay(int randValue, float time, float maxTime)
{
    const double fraction = static_cast<double>(randValue) * static_cast<double>(kRandScale);
    return static_cast<float>((static_cast<double>(maxTime) - static_cast<double>(time)) * fraction + static_cast<double>(time));
}

template<class Host> void Perform(Host& host, const ScriptCommand* node);

// A `Switch` over `RandDWORD<n>` / `Random<n>`: the drawn value takes the place of the switch value in the comparisons with the cases (the
// case is the left side). `Random<n>` becomes the constant `true`/`false` of its byte, `RandDWORD<n>` a number (an int as an unsigned
// 32-bit value). The data word of the constants is not read.
template<class Host> void PerformRandomSwitch(Host& host, const SwitchCommand& sw)
{
    int32_t rightKey;
    void* rightData = nullptr;
    float number = 0.0f;
    if (sw.m_value.m_kind == kKeyRandom) {
        const ScriptValue value = host.GetValue(sw.m_value.m_kind, sw.m_value.m_data);
        rightKey = (value.bits & 0xff) != 0 ? kKeyTrue : kKeyFalse;
    } else {
        const ScriptValue value = host.GetValue(sw.m_value.m_kind, sw.m_value.m_data);
        rightKey = kKeyNumber;
        number = value.type == kTypeFloat ? BitsToFloat(value.bits) : static_cast<float>(static_cast<uint32_t>(value.bits));
        rightData = &number;
    }
    for (const SwitchCase* c = sw.m_firstCase; c; c = c->m_next) {
        if (EvalCondition(host, c->m_value.m_kind, c->m_value.m_data, rightKey, rightData, kOpEqual)) {
            if (c->m_commands) Perform(host, c->m_commands);
            return;
        }
    }
}

template<class Host> void PerformMessage(Host& host, const MessageCommand& command)
{
    const messages::Message message = BuildMessage(host, command);
    const bool hasMaxTime = !Compare(0.0, command.m_maxTime).equal;
    if (hasMaxTime) {
        const int randValue = host.Rand();
        host.SendDelayed(message, RandomDelay(randValue, command.m_time, command.m_maxTime));
    } else if (Compare(0.0, command.m_time).equal) {
        host.Send(message);
    } else {
        host.SendDelayed(message, command.m_time);
    }
}

template<class Host> void PerformAnimation(Host& host, const AnimationCommand& animation)
{
    float duration;
    const int32_t reverse = animation.m_reverse;
    if (animation.m_finish)
        duration = host.PlayAnimation(animation.m_name, reverse, animation.m_exitState, 0, animation.m_finish);
    else if (animation.m_exitState != 0)
        duration = host.PlayAnimation(animation.m_name, reverse, animation.m_exitState, 0, nullptr);
    else
        duration = host.PlayAnimation(animation.m_name, reverse, -1, animation.m_notLooped == 0 ? 1 : 0, nullptr);
    if (animation.m_endMessage != kNoMessage) {
        messages::Message message{};
        message.sender = host.Id();
        message.receiver = host.Id();
        message.type = animation.m_endMessage;
        message.extra = host.State();
        host.SendDelayed(message, duration);
    }
}

// One arithmetic command: the target and the source are fetched (target first), combined by the kind of the target and stored back.
template<class Host> void PerformArithmetic(Host& host, script::Arith op, const ArithmeticCommand& command)
{
    const ScriptValue left = host.GetValue(command.m_target.m_kind, command.m_target.m_data);
    const ScriptValue right = host.GetValue(command.m_source.m_kind, command.m_source.m_data);
    const ScriptValue result = script::Arithmetic(op, left, right, [&host](int32_t bits) { return host.FloatToIntWrap(bits); });
    host.SetValue(command.m_target.m_kind, command.m_target.m_data, result.type, result.bits);
}

// Executing a command list: runs a command list on `host`. An opcode the parser never produces (12..0x19, above 0x1a) is skipped, 0x1a
// is the game's FatalError.
template<class Host> void Perform(Host& host, const ScriptCommand* node)
{
    for (; node; node = node->m_next) {
        switch (node->m_opcode) {
        case OpFunction: {
            const int32_t id = static_cast<const FunctionCommand*>(node->m_data)->m_functionId;
            if (id != kNoFunction) host.CallFunction(id);
            break;
        }
        case OpMessage: PerformMessage(host, *static_cast<const MessageCommand*>(node->m_data)); break;
        case OpChangeState: {
            const auto& change = *static_cast<const ChangeStateCommand*>(node->m_data);
            messages::Message message{};
            message.sender = host.Id();
            message.receiver = host.Id();
            message.type = messages::MT_STATECHANGE;
            message.data = change.m_stateId;
            if (!Compare(0.0, change.m_time).equal)
                host.SendDelayed(message, change.m_time);
            else
                host.Send(message);
            break;
        }
        case OpAnimation: PerformAnimation(host, *static_cast<const AnimationCommand*>(node->m_data)); break;
        case OpIf: {
            const auto& branch = *static_cast<const IfCommand*>(node->m_data);
            const bool condition = EvalCondition(
                    host, branch.m_left.m_kind, branch.m_left.m_data, branch.m_right.m_kind, branch.m_right.m_data, branch.m_op);
            const ScriptCommand* list = condition ? branch.m_then : branch.m_else;
            if (list) Perform(host, list);
            break;
        }
        case OpSwitch: {
            const auto& sw = *static_cast<const SwitchCommand*>(node->m_data);
            if (sw.m_value.m_kind == kKeyRandom || sw.m_value.m_kind == kKeyRandDword) {
                PerformRandomSwitch(host, sw);
                break;
            }
            for (const SwitchCase* c = sw.m_firstCase; c; c = c->m_next) {
                if (EvalCondition(host, sw.m_value.m_kind, sw.m_value.m_data, c->m_value.m_kind, c->m_value.m_data, kOpEqual)) {
                    if (c->m_commands) Perform(host, c->m_commands);
                    break;
                }
            }
            break;
        }
        case OpAdd: PerformArithmetic(host, script::Arith::Add, *static_cast<const ArithmeticCommand*>(node->m_data)); break;
        case OpMinus: PerformArithmetic(host, script::Arith::Minus, *static_cast<const ArithmeticCommand*>(node->m_data)); break;
        case OpDivide: PerformArithmetic(host, script::Arith::Divide, *static_cast<const ArithmeticCommand*>(node->m_data)); break;
        case OpMultiply: PerformArithmetic(host, script::Arith::Multiply, *static_cast<const ArithmeticCommand*>(node->m_data)); break;
        case OpSet: {
            const auto& set = *static_cast<const ArithmeticCommand*>(node->m_data);
            const ScriptValue value = host.GetValue(set.m_source.m_kind, set.m_source.m_data);
            host.SetValue(set.m_target.m_kind, set.m_target.m_data, value.type, value.bits);
            break;
        }
        case OpPlaySound: host.PlaySound(static_cast<const SoundCommand*>(node->m_data)->m_name); break;
        case 0x1a: host.UndefinedCommand(); break;
        default: break;
        }
    }
}

// The script state an object is in: the first state whose number is `state`; later states with the same number are never looked at.
inline const script::ProgramState* FindState(const script::ScriptProgram& script, int32_t state)
{
    for (uint32_t i = 0; i < script.m_NumStates; ++i)
        if (script.m_states[i].m_stateId == state) return &script.m_states[i];
    return nullptr;
}

// Select the entry, exit or update list for state; return null when the state or its list is absent.
// The caller skips update execution while the world is paused.
inline const ScriptCommand* SelectStateList(const script::ScriptProgram& script, int32_t state, ScriptCommand* script::ProgramState::* list)
{
    const script::ProgramState* found = FindState(script, state);
    return found ? found->*list : nullptr;
}

// Running message receipt commands: the first state with the object's number that has a handler for the message type decides, whether
// or not the handler has an OnReceipt list. `taken` tells the caller the script has the message (otherwise the class's own handling runs).
struct ReceiptChoice {
    bool taken = false;
    const ScriptCommand* list = nullptr;
};

inline ReceiptChoice SelectReceipt(const script::ScriptProgram& script, int32_t state, int32_t type)
{
    for (uint32_t i = 0; i < script.m_NumStates; ++i) {
        if (script.m_states[i].m_stateId != state) continue;
        const script::ProgramState& found = script.m_states[i];
        for (uint32_t h = 0; h < found.m_NumHandlers; ++h)
            if (found.m_handlers[h].m_messageId == type) return {true, found.m_handlers[h].m_onReceipt};
    }
    return {};
}

// Before sending an immediate or delayed message, inspect the first state matching the object state. Its
// first handler of the message type that has an OnSend list runs it (handlers without one are skipped). The caller posts the message afterwards.
inline const ScriptCommand* SelectSend(const script::ScriptProgram& script, int32_t state, int32_t type)
{
    const script::ProgramState* found = FindState(script, state);
    if (!found) return nullptr;
    for (uint32_t h = 0; h < found->m_NumHandlers; ++h)
        if (found->m_handlers[h].m_messageId == type && found->m_handlers[h].m_onSend) return found->m_handlers[h].m_onSend;
    return nullptr;
}

template<class Host>
void RunStateList(Host& host, const script::ScriptProgram& script, int32_t state, ScriptCommand* script::ProgramState::* list)
{
    if (const ScriptCommand* commands = SelectStateList(script, state, list)) Perform(host, commands);
}

template<class Host> bool RunOnReceipt(Host& host, const script::ScriptProgram& script, const messages::Message& message)
{
    const ReceiptChoice choice = SelectReceipt(script, host.State(), message.type);
    if (choice.list) Perform(host, choice.list);
    return choice.taken;
}

template<class Host> void RunOnSend(Host& host, const script::ScriptProgram& script, const messages::Message& message)
{
    if (const ScriptCommand* commands = SelectSend(script, host.State(), message.type)) Perform(host, commands);
}

} // namespace obl::script_program::exec
