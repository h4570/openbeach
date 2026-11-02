#include "game/ui/sound_api.h"
#include "game/objects/script_program_run.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/messages/message_state.h"
#include "game/objects/script_program_types.h"
#include "game/objects/object_base.h"
#include "game/objects/script_program_exec.h"
#include "core/ascii_case.h"
#include "core/call_counter.h"
#include "game/game/diagnostics.h"
#include "game/messages/message_handler.h"
#include "game/objects/script_value_convert.h"

#include <cstdint>
#include <cstring>

namespace obl::object {
using game::FatalError;
using game::GetWorld;
using game::Rand;

namespace {

using messages::Message;
using MessageHandler = messages::Handler;
using script::ProgramState;
using script::ScriptCommand;
using script::ScriptProgram;
using script::ScriptValue;
namespace ex = script_program::exec;

// `call-report objects.script-<name>`.
test::CallCounter g_entry("objects.script-run-on-entry");
test::CallCounter g_exit("objects.script-run-on-exit");
test::CallCounter g_update("objects.script-run-on-update");
test::CallCounter g_receipt("objects.script-run-on-receipt");
test::CallCounter g_send("objects.script-send-message");
test::CallCounter g_sendDelayed("objects.script-send-message-delayed");

// ---- The object the script runs on ----------------------------------------------------------------------------------------------

// Every call into the object goes through its virtual methods, so each object class answers the script its own way.
class GameHost {
public:
    explicit GameHost(object::Base* object) : m_object(object) {}

    ScriptValue GetValue(int32_t key, void* data) const
    {
        ScriptValue out{};
        m_object->GetValue(&out, key, reinterpret_cast<intptr_t>(data));
        return out;
    }
    void SetValue(int32_t key, void* data, int32_t type, int32_t bits) const
    {
        m_object->SetValue(key, reinterpret_cast<intptr_t>(data), type, bits);
    }
    void CallFunction(int32_t id) const { m_object->CallFunction(id); }
    void Send(const Message& message) const { m_object->Send(message); }
    void SendDelayed(const Message& message, float delay) const { m_object->SendDelayed(message, delay); }
    float PlayAnimation(const char* name, int32_t reverse, int32_t exitState, int32_t loop, const char* finish) const
    {
        return m_object->PlayAnimation(name, static_cast<uint32_t>(reverse), exitState, static_cast<uint32_t>(loop), finish);
    }
    void PlaySound(const char* name) const
    {
        float position[3] = {m_object->m_WorldXYZ[0], m_object->m_WorldXYZ[1], m_object->m_WorldXYZ[2]};
        audio::CurrentSound()->Play3D(name, position, 1, -1);
    }
    int32_t Id() const { return static_cast<int32_t>(m_object->m_id); }
    int32_t State() const { return m_object->m_state; }
    int Rand() const { return game::Rand(); }
    // A float script value converted to int the same way as everywhere else in the simulation (script_value::ToInt).
    int32_t FloatToIntWrap(int32_t floatBits) const { return script_value::ToInt(ex::kTypeFloat, static_cast<uint32_t>(floatBits)); }
    int Stricmp(const char* a, const char* b) const { return ascii::EqualsNoCase(a, b) ? 0 : 1; }
    void UndefinedCommand() const { FatalError("An object script ran an unrecognized command"); }

private:
    object::Base* m_object;
};

// Sent messages go to the global message handler, immediately or delayed.
void PostToHandler(Message& message)
{
    messages::message_facade::Post(messages::message_facade::Current(), &message);
}

void PostDelayedToHandler(Message& message, float delay)
{
    messages::message_facade::PostDelayed(messages::message_facade::Current(), &message, delay);
}

// ---- The entries ----------------------------------------------------------------------------------------------------------------

void RunList(ScriptProgram& script, object::Base* object, int32_t state, ScriptCommand* ProgramState::* list)
{
    GameHost host(object);
    ex::RunStateList(host, script, state, list);
}

// The world's pause flag turns the update off altogether.
bool UpdateSkipped()
{
    world_data::World* world = GetWorld();
    return world && world->m_paused != 0;
}

char Receive(ScriptProgram& script, object::Base* object, const Message& message)
{
    GameHost host(object);
    return ex::RunOnReceipt(host, script, message) ? 1 : 0;
}

void Send(ScriptProgram& script, object::Base* object, Message message)
{
    GameHost host(object);
    ex::RunOnSend(host, script, message);
    PostToHandler(message);
}

void SendDelayed(ScriptProgram& script, object::Base* object, Message message, float delay)
{
    GameHost host(object);
    ex::RunOnSend(host, script, message);
    PostDelayedToHandler(message, delay);
}

} // namespace

void ScriptProgramRunOnEntry(ScriptProgram& script, object::Base* object, int32_t state)
{
    g_entry.Count();
    RunList(script, object, state, &ProgramState::m_onEntry);
}

void ScriptProgramRunOnExit(ScriptProgram& script, object::Base* object, int32_t state)
{
    g_exit.Count();
    RunList(script, object, state, &ProgramState::m_onExit);
}

void ScriptProgramRunOnUpdate(ScriptProgram& script, object::Base* object, int32_t state)
{
    g_update.Count();
    if (!UpdateSkipped()) RunList(script, object, state, &ProgramState::m_onUpdate);
}

char ScriptProgramRunOnReceipt(ScriptProgram& script, object::Base* object, const Message& message)
{
    g_receipt.Count();
    return Receive(script, object, message);
}

void ScriptProgramSendMessage(ScriptProgram& script, object::Base* object, const Message& message)
{
    g_send.Count();
    Send(script, object, message);
}

void ScriptProgramSendMessageDelayed(ScriptProgram& script, object::Base* object, const Message& message, float delay)
{
    g_sendDelayed.Count();
    SendDelayed(script, object, message, delay);
}

} // namespace obl::object
