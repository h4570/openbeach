#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_guest.h"

#include <bit>
#include <cstdint>
#include <cstring>

// Guest simulation dispatch (docs/architecture/objects.md): the per-frame dispatcher `Update`, the message dispatcher
// `ReceiveMessage` and the thin wrappers around the object script (`OnEnterState`, `OnExitState`,
// `SendMessage`, `SendMessageDelayed`). The base-class update, the script interpreter and the movement code are calls into the game;
// what is spelled out here is the guest's own part and the bracket around those calls.
//
// Each function is a template over an environment so it runs on the game and in the unit tests:
//   Update:   env.GameDeltaTime(), env.GameTime()   the world's frame and game time (read again after calls that may change them)
//             env.DayLengthSeconds()                GameParameters `iDAY_LENGTH_IN_SECONDS`
//             env.HumanUpdate()                     the human base class's Update, false = error
//             env.Fatal(name)                       FatalError("Update of guest \"%s\" failed")
//             env.ReleaseSprite(sprite)             returning the sprite to its bank
//             env.Post(message), env.PostDelayed(message, delay), env.Rand()
//             env.RunOnUpdate(state)                runs the guest script update list for the current state
//   Receive:  env.GameTime(), env.RunOnReceipt(message), env.Movement(message)
// The arithmetic runs at 53-bit precision: each operation is one double operation, stored to a float once, and a compare
// that follows a store looks at the double, not at the stored float.
namespace obl::guest_update {

namespace detail {

constexpr float kStillSeconds = 100.0f; // a guest that has not moved for this long in the "chatted up" state is given up on
constexpr float kThrowUpDrunk = 0.75f; // drunkenness above which a guest heading for a desire is sick
constexpr uint32_t kSoberedBits = 0x3f333333; // the drunkenness after being sick (0.7f)
constexpr float kThrowUpScale = std::bit_cast<float>(0x38000100u); // 1/32767 as a float
constexpr float kTen = 10.0f; // seconds of the "just littered" flag, scale and base of the delay of the sick message
constexpr float kOffenceSeconds = 20.0f; // how long a recorded offence stays on the guest

constexpr int32_t kStateSearchDesire = 12; // STATE_GUEST_SEARCH_DESIRE
constexpr int32_t kStateHeadingDesire = 13; // STATE_GUEST_HEADING_DESIRE
constexpr int32_t kStateLounger = 29; // STATE_GUEST_LOUNGER
constexpr int32_t kStateChatUp = 35; // STATE_GUEST_CHATUP

inline messages::Message Own(const object::Guest& guest, int32_t type, int32_t data)
{
    messages::Message message{}; // the words past data stay zero; nobody reads them
    message.sender = static_cast<int>(guest.m_id);
    message.receiver = static_cast<int>(guest.m_id);
    message.type = type;
    message.data = data;
    return message;
}

// The sun-burn level climbs back towards 0 (it is negative while the guest tans) by twice the frame time per day-length second. The day
// length is looked up unless the guest is a lounger with a level of 0 or more; with such a level the climb below does nothing either way.
// Treat NaN like a negative value by testing that the value is not zero or greater.
template<class Env> void RecoverSunBurn(object::Guest& guest, float delta, Env& env)
{
    if (guest.m_sunBurnLevel >= 0.0f && guest.m_state == kStateLounger) return;
    const int32_t seconds = env.DayLengthSeconds();
    if (guest.m_sunBurnLevel >= 0.0f) return;
    // dt doubled, divided, added to level and stored; then compared with 0: the test is on the double sum, not on the stored float.
    const double sum = static_cast<double>(delta) * 2.0 / static_cast<double>(seconds) + static_cast<double>(guest.m_sunBurnLevel);
    guest.m_sunBurnLevel = static_cast<float>(sum);
    if (sum > 0.0) guest.m_sunBurnLevel = 0.0f;
}

// A guest that stands still counts the time since its last activity (`m_lastUpdateHappened`, also set by every message that is not a background
// one); one chatted up for too long without a word lets go of the one who chatted it up and goes back to looking for a desire.
template<class Env> void WatchIdle(object::Guest& guest, Env& env)
{
    const float now = env.GameTime();
    if (guest.m_moving == 0) {
        if (!(static_cast<double>(now) - static_cast<double>(guest.m_lastUpdateHappened) > static_cast<double>(kStillSeconds))) return;
        if (guest.m_state == kStateChatUp) {
            if (guest.m_attachedSprite) {
                env.ReleaseSprite(guest.m_attachedSprite);
                guest.m_attachedSprite = nullptr;
            }
            env.Post(Own(guest, messages::MT_STATECHANGE, kStateSearchDesire));
            return;
        }
    }
    guest.m_lastUpdateHappened = now;
}

} // namespace detail

// The guest's Update. Always answers true.
template<class Env> bool UpdateGuest(object::Guest& guest, Env& env)
{
    using namespace detail;
    const float delta = env.GameDeltaTime();
    RecoverSunBurn(guest, delta, env);
    WatchIdle(guest, env);

    // The mood icon fades out: the test of the floor is on the double difference before it is stored, and a NaN is below 0 there too.
    if (guest.m_moodIconTime > 0.0f) {
        const double faded = static_cast<double>(guest.m_moodIconTime) - static_cast<double>(delta);
        guest.m_moodIconTime = static_cast<float>(faded);
        if (!(faded >= 0.0)) guest.m_moodIconTime = 0.0f;
    }
    if (guest.m_debugBreak) guest.m_debugBreak = 0;

    if (!env.HumanUpdate()) env.Fatal(guest.m_Name);

    if (guest.m_state == kStateHeadingDesire && guest.m_drunk > kThrowUpDrunk) {
        std::memcpy(&guest.m_drunk, &kSoberedBits, sizeof(float));
        const messages::Message sick = Own(guest, messages::MT_THROWUP, 0);
        // rand * (1/32767) * 10 + 10, stored as float
        double seconds = static_cast<double>(env.Rand()) * static_cast<double>(kThrowUpScale);
        seconds = seconds * static_cast<double>(kTen);
        seconds = seconds + static_cast<double>(kTen);
        env.PostDelayed(sick, static_cast<float>(seconds));
    }

    if (guest.m_justLittered && static_cast<double>(env.GameTime()) > static_cast<double>(guest.m_timeLittered) + static_cast<double>(kTen))
        guest.m_justLittered = 0;
    if (guest.m_OffenceState != 0 &&
            static_cast<double>(env.GameTime()) > static_cast<double>(kOffenceSeconds) + static_cast<double>(guest.m_timeOfOffence))
        guest.m_OffenceState = 0;

    env.RunOnUpdate(guest.m_state);
    return true;
}

namespace detail {

// A float copied from the message has a signalling NaN quieted.
inline float Quieted(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0) bits |= 0x00400000u;
    std::memcpy(&value, &bits, sizeof(bits));
    return value;
}

// The messages that do not count as activity of the guest: its own periodic opinion update, a spot opinion and the closing of a building.
inline bool IsBackground(int32_t type)
{
    return type == messages::MT_UPDATEOPINIONS || type == messages::MT_SPOT_OPINION || type == messages::MT_BUILDING_CLOSING;
}

} // namespace detail

// The guest's ReceiveMessage. While the script and the movement code run, `m_currentMessage` is a copy of the message (a copy on the
// stack) and is put back afterwards. The script
// gets the message by value, the movement code the pointer the caller passed.
template<class Env> bool ReceiveMessage(object::Guest& guest, const messages::Message& message, Env& env)
{
    using namespace detail;
    void* const saved = guest.m_currentMessage;
    messages::Message copy = message;
    copy.floatData = Quieted(copy.floatData);
    guest.m_currentMessage = &copy;

    if (!IsBackground(message.type)) guest.m_lastUpdateHappened = env.GameTime();
    if (message.type == messages::MT_NEW_DAY) {
        // The best and the worst spot of the day start over.
        guest.m_BestSpotOpinion = 0;
        guest.m_WorstSpotOpinion = 0;
        guest.m_Best = -2;
        guest.m_Worst = -2;
        guest.m_bestScore = 0.0f;
        guest.m_worstScore = 0.0f;
    }
    const bool handled = env.RunOnReceipt(message) ? true : env.Movement(message);
    guest.m_currentMessage = saved;
    return handled;
}

// The four wrappers: the script is the guest's script word. `OnEnterState` and `OnExitState` always answer true; the exit hands the script the state the guest
// is in and neither looks for a missing script, the two senders do.
template<class Env> bool EnterState(object::Guest&, uint32_t state, Env& env)
{
    env.RunOnEntry(static_cast<int32_t>(state));
    return true;
}

template<class Env> bool ExitState(object::Guest& guest, Env& env)
{
    env.RunOnExit(guest.m_state);
    return true;
}

template<class Env> void SendMessage(object::Guest& guest, const messages::Message& message, Env& env)
{
    if (guest.m_script) env.Send(message);
}

template<class Env> void SendMessageDelayed(object::Guest& guest, const messages::Message& message, float delay, Env& env)
{
    if (guest.m_script) env.SendDelayed(message, delay);
}

} // namespace obl::guest_update
