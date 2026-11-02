#pragma once
#include "game/objects/script_program_types.h"
#include "game/objects/script_types.h"

// Releases what a parsed object script owns: operand data and the command lists of its states and handlers.
namespace obl::script_teardown {
inline bool OwnsData(int32_t kind)
{
    switch (kind) {
    case 0x36:
    case 0xbc:
    case 0xc2:
    case 0xc6:
    case 0xd2:
    case 0xd3:
    case 0xd4:
    case 0xd5:
    case 0xe3:
    case 0xe4:
    case 0xe8: return true;
    default: return false;
    }
}
template<class Env> void Free(script_program::ScriptOperand& value, Env& env)
{
    if (OwnsData(value.m_kind)) env.Delete(value.m_data);
}
template<class Env> void DestroyList(script::ScriptCommand* head, Env& env)
{
    if (head) {
        env.FreeCommands(head);
        env.Delete(head);
    }
}
template<class Env> void Destruct(script::ScriptHandler& handler, Env& env)
{
    DestroyList(handler.m_onReceipt, env);
    DestroyList(handler.m_onSend, env);
}
template<class Env> void Destruct(script::ProgramState& state, Env& env)
{
    DestroyList(state.m_onEntry, env);
    DestroyList(state.m_onExit, env);
    DestroyList(state.m_onUpdate, env);
    // The vector destructor visits all thirty handlers in reverse order, independent of the used count.
    for (int i = 29; i >= 0; --i) Destruct(state.m_handlers[i], env);
}
} // namespace obl::script_teardown
