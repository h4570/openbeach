#include "game/world/world_script.h"
#include "game/world/world_script_types.h"

#include "game/game/game_accessors.h"
#include "core/fatal.h"
#include "game/messages/message_state.h"
#include "core/call_counter.h"
#include "game/world/world_script_machine.h"
#include "game/ui/gui_tooltip.h"

// The state machine of the scenario script: the runners that walk a state's command lists (the update and exit lists,
// entering a state, a new game, a loaded game, a dispatched message) and the small state table queries (set and find a state). They only choose a
// list and call the interpreter (WorldScriptExecute) on its nodes.
// Rules: src/game/world/world_script_machine.h.
namespace obl::world {
namespace {

using messages::Message;
using script::ScriptCommand;
using world_script::WorldScript;

// Call counters (`call-report <name>`).
test::CallCounter g_runOnUpdate("world.script-run-on-update-list");
test::CallCounter g_runOnExit("world.script-run-on-exit-list");
test::CallCounter g_enterState("world.script-enter-state");
test::CallCounter g_setState("world.script-set-state");
test::CallCounter g_dispatch("world.script-dispatch-message");
test::CallCounter g_findState("world.script-find-state");
test::CallCounter g_startNew("world.script-start-new-game");
test::CallCounter g_startLoaded("world.script-start-loaded-game");

void ExecuteNode(const ScriptCommand* node)
{
    WorldScriptExecute(*const_cast<ScriptCommand*>(node));
}

// The nodes are executed one after the other, the next pointer is read after the node ran.
void RunList(const ScriptCommand* head)
{
    for (const ScriptCommand* node = head; node; node = node->m_next) ExecuteNode(node);
}

void ReportProgress(float progress)
{
    gui::LoadingBarSetProgress(progress);
}

constexpr float kProgressDone = 1.0f;

// The start state: an index into the state names, found by name. A script without it (or without any state) would read the state
// table at index -1 (and a script without states answers FindState with an undefined value), which no scenario does.
bool FindStartup(const WorldScript& script, uint32_t& index)
{
    const int32_t found = script::FindStateIndex(script.m_stateNames, script.m_numStates, script::kStartupState);
    if (script.m_numStates == 0 || found < 0 || !script::StateInRange(script, static_cast<uint32_t>(found))) return false;
    index = static_cast<uint32_t>(found);
    return true;
}

// The startup run after the state was chosen (WorldScriptStartNewGame): the loading bar moves before every 64th command and ends at 1 (only when there are commands).
void RunStartupScript(WorldScript& script, uint32_t state, float span)
{
    script.m_currentState = state;
    const ScriptCommand* head = script::OnEntryList(script, state);
    if (!head) return;
    const uint32_t count = script::CountCommands(head);
    uint32_t done = 0;
    for (const ScriptCommand* node = head; node; node = node->m_next, ++done) {
        if (done % script::kProgressEvery == 0) ReportProgress(script::StartProgress(done, count, span));
        ExecuteNode(node);
    }
    ReportProgress(kProgressDone);
}

} // namespace

void WorldScriptRunOnUpdate(WorldScript& script)
{
    g_runOnUpdate.Count();
    if (!script::CurrentStateInRange(script))
        fatal::UnhandledInput("world.script-run-on-update-list", "the current state is outside the state table");
    RunList(script::OnUpdateList(script, script.m_currentState));
}

void WorldScriptRunOnExit(WorldScript& script)
{
    g_runOnExit.Count();
    if (!script::CurrentStateInRange(script))
        fatal::UnhandledInput("world.script-run-on-exit-list", "the current state is outside the state table");
    RunList(script::OnExitList(script, script.m_currentState));
}

void WorldScriptEnterState(WorldScript& script, uint32_t state)
{
    g_enterState.Count();
    if (!script::StateInRange(script, state)) fatal::UnhandledInput("world.script-enter-state", "the state is outside the state table");
    script.m_currentState = state;
    RunList(script::OnEntryList(script, state));
}

void WorldScriptSetState(WorldScript& script, uint32_t state)
{
    g_setState.Count();
    script.m_currentState = state;
}

bool WorldScriptDispatch(WorldScript& script, Message message)
{
    g_dispatch.Count();
    if (!script::CurrentStateInRange(script))
        fatal::UnhandledInput("world.script-dispatch-message", "the current state is outside the state table");
    const script::Receipt receipt = script::FindReceiptList(script, script.m_currentState, message.type);
    if (receipt.handled) RunList(receipt.list);
    return receipt.handled;
}

int32_t WorldScriptFindState(const WorldScript& script, const char* name)
{
    g_findState.Count();
    if (script.m_numStates == 0) // no states: the answer is undefined
        fatal::UnhandledInput("world.script-find-state", "the script has no states");
    return script::FindStateIndex(script.m_stateNames, script.m_numStates, name);
}

void WorldScriptStartNewGame(WorldScript& script, float span)
{
    g_startNew.Count();
    uint32_t state;
    if (!FindStartup(script, state)) fatal::UnhandledInput("world.script-start-new-game", "the script has no startup state");
    RunStartupScript(script, state, span);
}

void WorldScriptStartLoadedGame(WorldScript& script)
{
    g_startLoaded.Count();
    uint32_t state;
    if (!FindStartup(script, state)) fatal::UnhandledInput("world.script-start-loaded-game", "the script has no startup state");
    script.m_currentState = state;
    RunList(script::OnEntryList(script, state));
}

} // namespace obl::world
