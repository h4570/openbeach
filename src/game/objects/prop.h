#pragma once

#include "game/ui/sprite.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"

#include <cstdint>

// Prop (docs/architecture/objects.md): the base of Scenery and Building, 0x60 bytes.
// Construct (the constructor's own fields)
// Update (no arguments, answers true) ReceiveMessage (the message)
// SetGridWidth / SetGridHeight / SetCleanerClaimed / SetMechanicClaimed / SetCalledRepairing /
// SetUrgentCleaningCalled / SetUrgentRepairCalled (all one byte: only the low byte of the argument is stored)
// IsMechanicClaimed / IsCleanerClaimed (no arguments, a byte answer)
// Scenery::SetVandalClaim (one byte): lives here because it is the same family of claim setters.
//
// Env:
//   ReceiveMessage:   bool BaseReceive(Prop&, const Message&)        the base object's ReceiveMessage
//   SetVandalClaim:   void Post(const Message&)                              Post of g_MessageHandler
namespace obl::prop {
using object::Prop;

// A message of this type is dropped without a word: not even the Entity handler sees it (the type table has 108 entries, so 108 is "none").
inline constexpr int kDroppedMessageType = 0x6c;
// dwState 4: a static object in this state counts as claimed by both the cleaner and the mechanic whatever the flags say.
inline constexpr int kAlwaysClaimedState = 4;

// Prop's constructor runs it after the base object's.
inline void Construct(Prop& s)
{
    s.m_gridW = 0;
    s.m_gridH = 0;
    s.m_cleaning = 0;
    s.m_repairing = 0;
    s.m_calledCleaning = 0;
    s.m_calledRepairing = 0;
    s.m_urgentCleaningCalled = 0;
    s.m_urgentRepairCalled = 0;
}

inline bool Update(Prop& s)
{
    s.m_debugBreak = 0;
    return true;
}

template<class Env> bool ReceiveMessage(Prop& s, const messages::Message& message, Env& env)
{
    if (message.type == kDroppedMessageType) return false;
    return env.BaseReceive(s, message);
}

inline void SetGridWidth(Prop& s, uint32_t width)
{
    s.m_gridW = static_cast<uint8_t>(width);
}
inline void SetGridHeight(Prop& s, uint32_t height)
{
    s.m_gridH = static_cast<uint8_t>(height);
}
inline void SetCleanerClaimed(Prop& s, uint32_t value)
{
    s.m_cleaning = static_cast<uint8_t>(value);
}
inline void SetMechanicClaimed(Prop& s, uint32_t value)
{
    s.m_repairing = static_cast<uint8_t>(value);
}
inline void SetCalledRepairing(Prop& s, uint32_t value)
{
    s.m_calledRepairing = static_cast<uint8_t>(value);
}
inline void SetUrgentCleaningCalled(Prop& s, uint32_t value)
{
    s.m_urgentCleaningCalled = static_cast<uint8_t>(value);
}
inline void SetUrgentRepairCalled(Prop& s, uint32_t value)
{
    s.m_urgentRepairCalled = static_cast<uint8_t>(value);
}

inline bool IsCleanerClaimed(const Prop& s)
{
    return s.m_state == kAlwaysClaimedState || s.m_cleaning != 0;
}
inline bool IsMechanicClaimed(const Prop& s)
{
    return s.m_state == kAlwaysClaimedState || s.m_repairing != 0;
}

// SetVandalClaim: a change of the flag tells the guests nearby (a look-around message from the object to itself). Claiming also puts the
// object into repair state 1 and remembers the animation it was in. The message's last three words are zero.
template<class Env> void SetVandalClaim(object::Scenery& s, uint32_t claimed, Env& env)
{
    const uint8_t value = static_cast<uint8_t>(claimed);
    if (s.m_vandalised == value) return;
    s.m_vandalised = value;
    if (value != 0) {
        s.m_RepairState = 1;
        s.m_PreVandlismAnim = static_cast<uint32_t>(static_cast<const ui::SpriteInstance*>(s.m_sprite)->m_animIndex);
    }
    messages::Message message{};
    message.sender = static_cast<int>(s.m_id);
    message.receiver = static_cast<int>(s.m_id);
    message.type = messages::MT_LOOK_AROUND;
    env.Post(message);
}

} // namespace obl::prop
