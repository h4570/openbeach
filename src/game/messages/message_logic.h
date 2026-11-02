#pragma once

#include "game/messages/message_state.h"

#include <cstddef>
#include <iterator>

// The delivery rules of the World's message queues (docs/specs/message-handler.md): when a post is delivered
// at once, the times written into time, which delayed messages are due and which survive a clear. The logic is a template over an
// Env that provides the world's clock and the delivery itself:
//   float Now()                   World::m_gameTime
//   bool Paused()                 World::m_paused
//   void Deliver(Message&)        Dispatch to the receiver id (the ObjectManager lookup; kMessageReceiverWorld goes to the world)
namespace obl::messages {

// The delay of a post that arrives while the world is paused.
constexpr float kPausedDelay = 0.0001f;

// A paused world defers every message except the "silent" sender's.
inline bool DeliversAtOnce(bool paused, int sender)
{
    return !paused || sender == kMessageSenderSilent;
}

// The time written into a message. The sums are float + float rounded to float.
inline float PostedTime(float now, bool deferred)
{
    return deferred ? now + kPausedDelay : now;
}
inline float DelayedTime(float now, float delay)
{
    return now + delay;
}

// Update takes an entry when its time is not greater than now: time <= now, and an unordered (NaN) time too.
inline bool IsDue(float time, float now)
{
    return !(time > now);
}

// ClearDelayedForReceiver keeps these types (0x3f, 0x54, 0x38, 0x1f, 0x16).
inline bool SurvivesClear(int type)
{
    return type == MT_UPDATEOPINIONS || type == MT_LOOK_AROUND || type == MT_FINISH_ACTION || type == MT_MOVE2QUEUEPOINT ||
            type == MT_MOVEIMHERE;
}

// Post: stamps time and delivers at once, or appends to the delayed list in a paused world. The caller's message gets the time.
template<class Env> void Post(Handler& handler, Env& env, Message& message)
{
    const bool atOnce = DeliversAtOnce(env.Paused(), message.sender);
    message.time = PostedTime(env.Now(), !atOnce);
    if (atOnce)
        env.Deliver(message);
    else
        handler.delayed.push_back(message);
}

// PostDelayed: always queued, due `delay` game seconds from now.
template<class Env> void PostDelayed(Handler& handler, Env& env, Message& message, float delay)
{
    message.time = DelayedTime(env.Now(), delay);
    handler.delayed.push_back(message);
}

// Update: the due entries move to the working list in list order and are delivered from there. A message posted during the delivery
// lands in the delayed list and waits for the next Update, even when it is already due.
template<class Env> void Update(Handler& handler, Env& env)
{
    const float now = env.Now();
    for (auto it = handler.delayed.begin(); it != handler.delayed.end();) {
        auto next = std::next(it);
        if (IsDue(it->time, now)) handler.working.splice(handler.working.end(), handler.delayed, it);
        it = next;
    }
    for (Message& message : handler.working) env.Deliver(message);
    handler.working.clear();
}

// ClearDelayedForReceiver: drops the receiver's delayed messages except the surviving types; returns how many went.
inline size_t ClearDelayedForReceiver(Handler& handler, int receiver)
{
    return handler.delayed.remove_if([&](const Message& m) { return m.receiver == receiver && !SurvivesClear(m.type); });
}

} // namespace obl::messages
