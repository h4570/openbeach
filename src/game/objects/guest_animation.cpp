#include "game/objects/guest.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "core/fatal.h"
#include "game/objects/guest_anim_record.h"
#include "game/objects/guest_animation.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/world/world_state.h"
#include "game/objects/script_text.h"
#include "core/call_counter.h"

#include <cstdint>
#include <cstring>

// PlayAnimation ( name, reverse, exitState, loop, finish; returns the duration), the guest's side of the
// script command `PlayAnimation` and of the guest functions that stand, turn or walk (src/game/objects/guest_animation.h, guest animation, docs/architecture/objects.md): the name
// may stand for the walk, stand or return animation, the one in the message being handled or a fidget; the sprite's animation type follows from the two
// flags; a looped animation just starts, any other one remembers what to come back to (and the state to leave for), tells the guest to leave state 0x15 when
// the return animation is no longer the played one, and sends the guest a delayed change of state after the animation. The sprite's own call
// (starting the named sprite animation) and the sends go through their vtable slots, so overrides are honoured.
namespace obl::object {
using game::GetWorld;

namespace {

using guest_anim_record::AnimRecording;
using messages::Message;
using object::Guest;
using ui::SpriteInstance;

test::CallCounter g_play("objects.guest-play-animation");

SpriteInstance* SpriteOf(object::Base& object)
{
    return static_cast<SpriteInstance*>(object.m_sprite);
}

// The calls into the guest go through its vtable, so subclass overrides are honoured.
void Send(Guest& guest, const Message& message)
{
    guest.Send(message);
}

void SendDelayed(Guest& guest, const Message& message, float delay)
{
    guest.SendDelayed(message, delay);
}

Message StateChange(const Guest& guest, int32_t flags, int32_t state)
{
    Message message{};
    message.sender = message.receiver = static_cast<int>(guest.m_id);
    message.type = guest_anim::kEndMessageType;
    message.flags = flags;
    message.data = state;
    return message;
}

void Copy(char* to, const char* from)
{
    std::memmove(to, from, std::strlen(from) + 1);
}

// `reverse` and `loop` are tested as bytes. The sprite is read without a check, and so is the name, the message of "MessData" and the `finish`.
float Play(Guest& guest, const char* name, int32_t reverse, int32_t exitState, int32_t loop, const char* finish)
{
    const bool reversed = (reverse & 0xff) != 0, looped = (loop & 0xff) != 0;
    const char* messData = nullptr;
    if (guest_anim::Equal(name, guest_anim::kMessData)) messData = script::text::Of(static_cast<Message*>(guest.m_currentMessage)->data);
    const guest_anim::Names names{messData, guest.m_WalkAnimation, guest.m_StandAnimation, guest.m_ReturnAnimation};
    static char
            scratch[guest_anim::kScratchSize]; // a static buffer: nothing else keeps the name past the call, but the sprite may look at it
    name = guest_anim::Resolve(name, names, scratch);

    SpriteInstance* sprite = SpriteOf(guest);
    sprite->m_animType = guest_anim::AnimType(reversed, looped);
    if (!looped) {
        Copy(guest.m_ReturnAnimation, guest_anim::ReturnTo(name, finish, names));
        guest.m_LeaveState = guest_anim::LeaveState(exitState, guest.m_state);
        if (guest_anim::SendsEndState(guest_anim::Equal(guest.m_ReturnAnimation, name), guest.m_state))
            Send(guest, StateChange(guest, 0, guest_anim::kEndStateDefault));
    }
    const float duration = guest_anim::Duration((sprite)->SetAnimByName(name, GetWorld()->m_gameTime, guest.m_WorldXYZ));
    Copy(guest.m_CurrAnimation, name);
    // The state and the leave state are read again: the end message may have been delivered at once.
    if (!looped && guest_anim::SendsDelayedLeave(guest.m_LeaveState, guest.m_state, guest_anim::Equal(guest.m_ReturnAnimation, name)))
        SendDelayed(guest, StateChange(guest, 1, guest.m_LeaveState), duration);
    return duration;
}

} // namespace

float Guest::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    AnimRecording& recording = guest_anim_record::Recording();
    if (recording.active) { // the call is listed and skipped
        recording.Add({false, reinterpret_cast<uintptr_t>(this), name ? name : "", reverse, static_cast<uint32_t>(exitState), loop,
                finish ? 1u : 0u});
        return guest_anim_record::kRecordedAnimationLength;
    }
    if (!m_sprite) fatal::UnhandledInput("objects.guest-play-animation", "the guest has no sprite");
    g_play.Count();
    return Play(*this, name, reverse, exitState, loop, finish);
}
} // namespace obl::object
