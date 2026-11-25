#pragma once

#include "game/messages/message_state.h"
#include "game/objects/script_program_types.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_base.h"

#include <cstdint>

namespace obl::object {

// Flag set on MT_STATECHANGE posted by a building's own object script (ChangeState), so Building::ReceiveMessage
// routes it to Building::m_scriptState without touching Building::m_state (construction/working/demolition).
inline constexpr int32_t kBuildingScriptStateChangeFlag = 1;

// The object script interpreter (script_program_exec.h) run on a game object: the state lists (entry, exit, update), message
// receipt and sending. Everything the script does to the object (variables, built-in functions, messages, animation, sound) goes
// through the object's virtual methods (docs/architecture/objects.md "Execution"). Each entry counts its
// `objects.script-<name>` counter. RunOnUpdate does nothing while the world is paused; SendMessage/SendMessageDelayed run the
// OnSend handlers and post the message.
void ScriptProgramRunOnEntry(script::ScriptProgram& script, object::Base* object, int32_t state);
void ScriptProgramRunOnExit(script::ScriptProgram& script, object::Base* object, int32_t state);
void ScriptProgramRunOnUpdate(script::ScriptProgram& script, object::Base* object, int32_t state);
char ScriptProgramRunOnReceipt(script::ScriptProgram& script, object::Base* object, const messages::Message& message);
void ScriptProgramSendMessage(script::ScriptProgram& script, object::Base* object, const messages::Message& message);
void ScriptProgramSendMessageDelayed(script::ScriptProgram& script, object::Base* object, const messages::Message& message, float delay);

} // namespace obl::object
