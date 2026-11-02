#include "game/ui/sprite_bank.h"
#include "game/objects/guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/guest_call_tap.h"
#include "game/objects/guest_update.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/objects/script_program_run.h"
#include "game/game/game_api.h"
#include "game/game/diagnostics.h"

#include <cstdint>

// Guest Update and ReceiveMessage dispatch frame and message work; state entry and exit run the corresponding script handlers.
// Send and SendDelayed run the script send handler before delivery. The shared decisions live in guest_update.h;
// the environment supplied here calls the base update, script interpreter and movement services.
namespace obl::object {
using game::FatalError;
using game::GetWorld;
using game::Rand;

// Never set; src/game/objects/staff_feedback.cpp asks for it.
GuestCallTap*& ActiveGuestCallTap()
{
    static GuestCallTap* tap = nullptr;
    return tap;
}

namespace {

using messages::Message;
using object::Guest;
using script::ScriptProgram;

constexpr auto kReleaseSprite = [](auto* owner, void* instance) { static_cast<ui::SpriteBank*>(owner)->ReleaseInstance(instance); };

test::CallCounter g_update("objects.guest-update");
test::CallCounter g_receive("objects.guest-receive-message");
test::CallCounter g_enter("objects.guest-on-enter-state");
test::CallCounter g_exit("objects.guest-on-exit-state");
test::CallCounter g_send("objects.guest-send-message");
test::CallCounter g_sendDelayed("objects.guest-send-message-delayed");

messages::Handler* Handler()
{
    return messages::message_facade::Current();
}

// The environment on the game.
struct UpdateEnv {
    Guest& guest;

    float GameDeltaTime() const { return GetWorld()->m_gameDeltaTime; }
    float GameTime() const { return GetWorld()->m_gameTime; }
    int32_t DayLengthSeconds() const
    {
        int32_t seconds = 0;
        game::game_parameters::GetInt(game::game_parameters::Current(), &seconds, "iDAY_LENGTH_IN_SECONDS");
        return seconds;
    }
    bool HumanUpdate() { return guest.Person::Update(); }
    void Fatal(const char* name) { FatalError("Update of guest \"%s\" failed", name); }
    void ReleaseSprite(void* sprite) { kReleaseSprite(ui::GetSpriteBank(), sprite); }
    void Post(Message message) { messages::message_facade::Post(Handler(), &message); }
    void PostDelayed(Message message, float delay) { messages::message_facade::PostDelayed(Handler(), &message, delay); }
    int Rand() { return game::Rand(); }
    void RunOnUpdate(int32_t state) { ScriptProgramRunOnUpdate(*guest.m_script, &guest, state); }
};

struct ReceiveEnv {
    Guest& guest;

    float GameTime() const { return GetWorld()->m_gameTime; }
    bool RunOnReceipt(const Message& message) { return ScriptProgramRunOnReceipt(*guest.m_script, &guest, message) != 0; }
    bool Movement(const Message& message) { return guest.Person::ReceiveMessage(const_cast<Message*>(&message)); }
};

struct ScriptEnv {
    Guest& guest;
    void RunOnEntry(int32_t state) { ScriptProgramRunOnEntry(*guest.m_script, &guest, state); }
    void RunOnExit(int32_t state) { ScriptProgramRunOnExit(*guest.m_script, &guest, state); }
    void Send(const Message& message) { ScriptProgramSendMessage(*guest.m_script, &guest, message); }
    void SendDelayed(const Message& message, float delay) { ScriptProgramSendMessageDelayed(*guest.m_script, &guest, message, delay); }
};

// ---- the dispatchers ---------------------------------------------------------------------------------------------------------

} // namespace

// The dispatchers: the per-frame update and the message receiver run the guest's Env-driven logic (guest_update.h).
bool Guest::Update()
{
    g_update.Count();
    UpdateEnv env{*this};
    return guest_update::UpdateGuest(*this, env);
}

bool Guest::ReceiveMessage(messages::Message* message)
{
    g_receive.Count();
    ReceiveEnv env{*this};
    return guest_update::ReceiveMessage(*this, *message, env);
}

// ---- the four wrappers -------------------------------------------------------------------------------------------------------
// Pure forwarding to ScriptProgram_*, which are the script module's own functions; the end-to-end check is the world-hash goldens and the counters of
// guest (every guest enters and leaves states and sends messages all the time).

bool Guest::OnEnterState(uint32_t state)
{
    g_enter.Count();
    ScriptEnv env{*this};
    return guest_update::EnterState(*this, state, env);
}

bool Guest::OnExitState()
{
    g_exit.Count();
    ScriptEnv env{*this};
    return guest_update::ExitState(*this, env);
}

void Guest::Send(messages::Message message)
{
    g_send.Count();
    ScriptEnv env{*this};
    guest_update::SendMessage(*this, message, env);
}

// SendMessageDelayed: the guest's own send.
void Guest::SendDelayed(messages::Message message, float delay)
{
    g_sendDelayed.Count();
    ScriptEnv env{*this};
    guest_update::SendMessageDelayed(*this, message, delay, env);
}
} // namespace obl::object
