#pragma once

#include "script_types.h"

// Payloads behind ScriptCommand::m_data of an object script (`Data/Script/Object/*.ble`, docs/architecture/objects.md).
// The parser allocates command payloads with operator new; list teardown releases them by opcode, so a
// parser and a destructor that allocate and free the same blocks the same way can free each other's results.
namespace obl::script_program {

using script::ScriptCommand;

// ScriptCommand::m_opcode of an object script.
enum ObjectOpcode : int32_t {
    OpFunction = 0,
    OpMessage = 1,
    OpChangeState = 2,
    OpAnimation = 3,
    OpIf = 4,
    OpSwitch = 5,
    OpAdd = 6,
    OpMinus = 7,
    OpDivide = 8,
    OpMultiply = 9,
    OpSet = 10,
    OpPlaySound = 11,
};

// ScriptOperand::m_kind selects the meaning of an object script operand. The named variables take their ids
// from the script API's name table (0..0x10f); data is zero for them.
enum ObjectValueKind : int32_t {
    ValueString = 0x36, // data: heap copy of the text without its quotes
    ValueRandDword = 0xe3, // data: heap int32 holding n of `RandDWORD<n>`
    ValueRandom = 0xe4, // data: heap int32 holding n of `Random<n>`
    ValueNumber = 0xe8, // data: heap float (a literal or the number of a `STATE_...` name)
    ValueNone = 0x110, // no value
};

// The {kind, data} pair produced by operand resolution. Only the kinds above own a heap block (operand teardown also
// frees 0xbc, 0xc2, 0xc6 and 0xd2..0xd5, which the parser never produces).
struct ScriptOperand {
    int32_t m_kind;
    void* m_data;
};

// Opcode 0 `Function <name>;`: the id of a built-in function (0xb5 = unknown name is a FatalError in the parser).
struct FunctionCommand {
    int32_t m_functionId;
};

// Opcode 1 `Message <MT_...> <receiver> <data> <flags> [Time t] [MaxTime t] [FloatData v] [State v] [SendFrom v];`. Slots
// without a keyword hold kind 0x110; the three values after the message name are always set.
struct MessageCommand {
    int32_t m_messageId;
    ScriptOperand m_receiver;
    ScriptOperand m_data;
    ScriptOperand m_flags;
    ScriptOperand m_floatData;
    ScriptOperand m_state;
    ScriptOperand m_sendFrom;
    float m_time;
    float m_maxTime;
};

// Opcode 2 `ChangeState <STATE_...> [Time t];` (an unknown state name leaves the id 0).
struct ChangeStateCommand {
    int32_t m_stateId;
    float m_time;
};

// Opcode 3 `PlayAnimation <name> [Finish <name>] [NotLooped] [Reverse] [ExitState s] [MaxTime t] [EndMessage m];`.
struct AnimationCommand {
    char* m_name; // heap copy; a variable name (WalkAnimation, ...) is kept as text
    char* m_finish; // heap copy or null
    int32_t m_endMessage; // MessageType, 0x6c = none
    int32_t m_exitState; // 0 = none
    float m_maxTime;
    uint8_t m_reverse;
    uint8_t m_notLooped;
    uint8_t m_padding[2]; // never written by the parser
};

constexpr int32_t kNoMessage = 0x6c;

// Opcode 4 `If (left [op right]) ... [Else ...] End`. `m_op`: && 0, || 1, > 2, < 3, == 4, <= and =< 5, >= and => 6, != 7;
// the parser leaves it unset when the condition has no right value (kind 0x110).
struct IfCommand {
    ScriptCommand* m_then;
    ScriptCommand* m_else;
    ScriptOperand m_left;
    ScriptOperand m_right;
    int32_t m_op;
    int32_t m_inElse; // set by `Else` while parsing: later commands go to m_else
};

// One `Case <value>:` of a Switch: its own command list and the next case.
struct SwitchCase {
    ScriptOperand m_value;
    ScriptCommand* m_commands;
    SwitchCase* m_next;
};

// Opcode 5 `Switch (value) Case ...: ... End`.
struct SwitchCommand {
    SwitchCase* m_firstCase;
    ScriptOperand m_value;
};

// Opcodes 6..10 `Add|Minus|Divide|Multiply|Set <target> <source>;`.
struct ArithmeticCommand {
    ScriptOperand m_target;
    ScriptOperand m_source;
};

// Opcode 11 `PlaySound <name>;`.
struct SoundCommand {
    char* m_name;
};

} // namespace obl::script_program
