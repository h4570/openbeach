#pragma once

#include "game/world/world_script_types.h"
#include "game/objects/script_message.h"

#include <cstdint>

// The scenario script's state machine (run on update list, run on exit list, enter state, set state, dispatch message, find state,
// start new game, start loaded game): which command list a call walks and which numbers it reports. The walking
// itself (ExecuteCommand on every node) stays in game/world_script_machine.cpp. Checked against recorded behavior.
namespace obl::script {

// The state every scenario starts in.
constexpr const char* kStartupState = "STATE_WORLD_STARTUP";

// The runners index the state table with the current state; an index outside it (no script started yet) ends in fatal::UnhandledInput.
inline bool StateInRange(const world_script::WorldScript&, uint32_t index)
{
    return index < world_script::kMaxWorldScriptStates;
}
inline bool CurrentStateInRange(const world_script::WorldScript& script)
{
    return StateInRange(script, script.m_currentState);
}

inline const script::ScriptCommand* OnEntryList(const world_script::WorldScript& script, uint32_t state)
{
    return script.m_states[state].m_onEntry;
}
inline const script::ScriptCommand* OnExitList(const world_script::WorldScript& script, uint32_t state)
{
    return script.m_states[state].m_onExit;
}
inline const script::ScriptCommand* OnUpdateList(const world_script::WorldScript& script, uint32_t state)
{
    return script.m_states[state].m_onUpdate;
}

// `Message MT_x ... OnReceipt` of the current state: the first handler (in script order) of the message type. The lookup reports "handled"
// whenever it finds a handler, even one whose list is empty, and walks nothing otherwise.
struct Receipt {
    bool handled;
    const script::ScriptCommand* list;
};

inline Receipt FindReceiptList(const world_script::WorldScript& script, uint32_t state, int32_t messageType)
{
    const world_script::WorldScriptState& current = script.m_states[state];
    for (uint32_t i = 0; i < current.m_NumHandlers; ++i)
        if (current.m_handlers[i].m_messageId == messageType) return {true, current.m_handlers[i].m_onReceipt};
    return {false, nullptr};
}

// StartNewGame reports its progress to the loading bar before every 64th command of the start state's OnEntry list (counted from the first)
// and once more, with 1, at the end: the bar covers the last `span` of its range, `done` of `count` commands in. with the process's 53-bit
// precision: the quotient, the product and the sum are doubles, rounded to float when stored.
constexpr uint32_t kProgressEvery = 64;

inline float StartProgress(uint32_t done, uint32_t count, float span)
{
    const double covered = static_cast<double>(done) / static_cast<double>(static_cast<int32_t>(count)) * static_cast<double>(span);
    return static_cast<float>(covered + (1.0 - static_cast<double>(span)));
}

inline uint32_t CountCommands(const script::ScriptCommand* list)
{
    uint32_t count = 0;
    for (; list; list = list->m_next) ++count;
    return count;
}

} // namespace obl::script
