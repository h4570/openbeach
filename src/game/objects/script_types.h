#pragma once

#include <cstdint>

// Shared command nodes, state tables and values for the script interpreters.
namespace obl::script {

constexpr int kMaxProgramStates = 60;

constexpr int kScriptProgramHandlers = 30;

struct ScriptCommand {
    ScriptCommand* m_next;
    int m_opcode; // WorldScript: Function 0, Message 1, ChangeState 2, If 4, ...
    void* m_data;
};

struct ScriptHandler {
    int m_messageId; // messages::MessageType
    ScriptCommand* m_onReceipt;
    ScriptCommand* m_onSend;
};

template<int N> struct ScriptState {
    int m_stateId; // ScriptProgram: STATE_... number; WorldScript never writes it
    ScriptHandler m_handlers[N];
    uint32_t m_NumHandlers;
    ScriptCommand* m_onEntry;
    ScriptCommand* m_onExit;
    ScriptCommand* m_onUpdate;
};

using ProgramState = ScriptState<kScriptProgramHandlers>;

struct ScriptProgram {
    ProgramState m_states[kMaxProgramStates];
    uint32_t m_NumStates;
    ScriptCommand* m_openCommand; // If block being filled by appending a script command
    void* m_openSwitch; // Switch/Case being filled, null outside one
    const char* m_text; // parse cursor, only meaningful inside loading an object script
};

struct ScriptValue {
    int32_t type;
    int32_t bits;
};

} // namespace obl::script
