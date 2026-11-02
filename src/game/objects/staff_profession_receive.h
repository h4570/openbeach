#pragma once

#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_staff.h"

#include <cstdint>

// ReceiveMessage for the six staff professions,
// each taking the message and answering a bool.
// Every one is a `switch` over the type of the message with a few messages of the profession and, on every path, the same last call:
// the staff ReceiveMessage (`staff_receive.h`), whose answer is the answer of the function. What the switch does first:
//   MT_NEW_DAY               the counters of the day go to zero and the messages the player was told about the profession are forgotten
//   MT_NEW_HOUR              every fourth hour a member who is not happy (below 0.6) forgets that it asked for feedback
//   MT_BUILDING_BEING_REMOVED  the home building goes away: the member leaves its job (a message from any other building matters only to a rep and
//                            a security guard whose target it is)
//   MT_DIRECT_TO_POINT       the player sends the member somewhere: the member stops what it is doing (messages to itself)
//   MT_CONSTRUCT / MT_DEMOLISH (builder only) the order of the construction manager is the new target
// The logic is a template over an environment, so it runs on the game and in the unit tests:
//   env.ClearStaffMessages(type)   feedback::Feedback::ClearStaffMessages(feedback queue, class of the profession)
//   env.GameDayTime()              fractional simulation hour
//   env.Post(message)              posts the message (the words after data are zero)
//   env.Base(message)              the staff ReceiveMessage, the answer of the function
//   env.LeaveTower()               the lifeguard leaves its tower
//   env.ReleaseGuest(sentToGuest)  the rep releases its guest and leaves (m_talking_id, m_sentToGuest)
//   env.MoveToNextBuilding()       MoveToNextBuilding
//   env.FreeBuilder(id)            releases the builder's construction job
//   env.SetState(state)            the object's SetState
//   env.EnterState(state)          the member's own OnEnterState
//   env.LookForOffender()          LookForOffender(member, &m_MisdemeanourPos)
namespace obl::staff {

namespace profession_receive {

// feedback::Feedback::ClearStaffMessages takes the class of the profession.
constexpr int32_t kBuilderClass = 10;
constexpr int32_t kLifeguardClass = 7;
constexpr int32_t kRepClass = 9;
constexpr int32_t kSecurityClass = 12;
constexpr int32_t kCleanerClass = 11;
constexpr int32_t kMechanicClass = 8;

// A member who loses the job goes to this state (the script's "leaving" state) in the two professions that do not have a function of their own for it.
constexpr int32_t kLeavingState = 0x4c;

// The states of the scripts the messages ask about (docs/architecture/objects.md): the lifeguard on its way to or at a rescue, the rep with a guest, the
// security guard on a patrol, the builder at a job.
constexpr int32_t kLifeguardStateA = 0x4e;
constexpr int32_t kLifeguardStateB = 0x51;
constexpr int32_t kRepStateWithGuest = 8;
constexpr int32_t kSecurityPatrolState = 0x78;
constexpr int32_t kSecurityStopState = 0x77;
constexpr uint32_t kSecurityOffenceFirst = 0x7d, kSecurityOffenceLast = 0x7f, kSecurityOffenceExtra = 0x83;
constexpr int32_t kOffenderReleasedState = 0xc; // the state the offender's own script goes to when the guard stops dealing with it
constexpr uint32_t kBuilderJobFirst = 0x61, kBuilderJobLast = 0x64, kBuilderJobExtra = 0x67;

// The happiness a member needs not to be asked about feedback again ().
constexpr float kUnhappyBelow = 0.6f;
constexpr uint32_t kFeedbackAsked = 0x00800000;

// The truncated hour `% 4 == 0` with the C remainder, so a negative multiple of four
// passes too. The truncation of NaN and of anything beyond 64 bits is 0 (float_to_int.h), which passes.
inline bool EveryFourthHour(float clock)
{
    uint32_t bits = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(clock))) & 0x80000003u;
    if (static_cast<int32_t>(bits) < 0) bits = ((bits - 1u) | 0xfffffffcu) + 1u;
    return bits == 0;
}

// The test of the hourly message: an hour divisible by four and a member below 0.6 (an unordered NaN counts as below too).
inline bool AsksNoMore(float clock, float happiness)
{
    return EveryFourthHour(clock) && !(happiness >= kUnhappyBelow);
}

template<class Staff> void ForgetFeedbackRequest(Staff& member)
{
    member.m_FeedbackStatus = static_cast<int32_t>(static_cast<uint32_t>(member.m_FeedbackStatus) & ~kFeedbackAsked);
    member.m_RequiredFeedback = 0;
}

// The message a member sends to itself (sender and receiver are its own id, read once for both).
inline messages::Message OwnMessage(const object::Staff& member, int32_t type, int32_t data = 0)
{
    messages::Message message{};
    message.sender = static_cast<int32_t>(member.m_id);
    message.receiver = static_cast<int32_t>(member.m_id);
    message.type = type;
    message.data = data;
    return message;
}

// "Stop (MT_MOVESTOP) and go to the state": the two messages of the DirectToPoint of the three professions that have one.
template<class Env> void StopAndChangeState(const object::Staff& member, int32_t state, Env& env)
{
    env.Post(OwnMessage(member, messages::MT_MOVESTOP));
    env.Post(OwnMessage(member, messages::MT_STATECHANGE, state));
}

// The home building is gone: the leaving state is taken (a profession that has no function for leaving the job does it by hand: the state is written,
// then entered through the member's own slot).
template<class Env> void BecomeLeaving(Env& env)
{
    env.SetState(kLeavingState);
    env.EnterState(kLeavingState);
}

} // namespace profession_receive

// The facing a mechanic takes when it stops: the last direction up to four directions (-1 for zero directions), otherwise 4.
inline int32_t MechanicStoppedDirection(uint8_t directions)
{
    return directions <= 4 ? static_cast<int32_t>(directions) - 1 : 4;
}

template<class Env> bool ReceiveCleaner(object::Cleaner& member, const messages::Message& message, Env& env)
{
    using namespace profession_receive;
    switch (message.type) {
    case messages::MT_NEW_DAY:
        member.m_ToiletsToday = member.m_BinsToday = member.m_LitterToday = 0;
        env.ClearStaffMessages(kCleanerClass);
        break;
    case messages::MT_NEW_HOUR:
        if (AsksNoMore(env.GameDayTime(), member.m_happiness)) ForgetFeedbackRequest(member);
        break;
    case messages::MT_BUILDING_BEING_REMOVED:
        if (message.sender == member.m_HomeID) {
            const uint32_t state = static_cast<uint32_t>(member.m_state);
            if (state > 0x6c && (state < 0x74 || (state == 0x83 && member.m_cantDoJob == 0))) env.DidNotClean(member.m_DestObjectID);
            BecomeLeaving(env);
        } else if (message.sender == member.m_DestObjectID) {
            switch (member.m_state) {
            case 0x6d:
            case 0x6f:
            case 0x70:
            case 0x83:
                env.CancelCleaning(member.m_DestObjectID);
                env.Post(OwnMessage(member, messages::MT_MOVESTOP));
                env.SetState(0x68);
                env.EnterState(0x68);
                break;
            case 0x6e:
            case 0x71:
            case 0x73: {
                auto next = OwnMessage(member, messages::MT_DO_NEXT_ACTION);
                next.extra = member.m_state;
                env.Post(next);
                break;
            }
            case 0x72:
                env.CancelCleaning(member.m_DestObjectID);
                env.Post(OwnMessage(member, messages::MT_LEAVE_BUILDING));
                break;
            default: break;
            }
        }
        break;
    case messages::MT_DIRECT_TO_POINT:
        if (member.m_state == 0x6d || member.m_state == 0x6f || member.m_state == 0x70) {
            env.DidNotClean(member.m_DestObjectID);
            StopAndChangeState(member, 0x68, env);
        }
        break;
    default: break;
    }
    return env.Base(message);
}

template<class Env> bool ReceiveMechanic(object::Mechanic& member, const messages::Message& message, Env& env)
{
    using namespace profession_receive;
    switch (message.type) {
    case messages::MT_NEW_DAY:
        member.m_wateredToday = member.m_vandalsToday = member.m_repairsToday = member.m_inspectionsToday = 0;
        env.ClearStaffMessages(kMechanicClass);
        break;
    case messages::MT_NEW_HOUR:
        if (AsksNoMore(env.GameDayTime(), member.m_happiness)) ForgetFeedbackRequest(member);
        break;
    case messages::MT_BUILDING_BEING_REMOVED:
        if (message.sender == member.m_HomeID) {
            env.ReturnJobAndLeave(member.m_foundRepair != 0);
        } else if (message.sender == member.m_DestObjectID) {
            bool release = false;
            switch (member.m_state) {
            case 0x58:
                // Preserve the second query and its short circuit, rather than caching the first answer.
                if (env.RepairType(member.m_DestObjectID) == 1 || env.RepairType(member.m_DestObjectID) == 3)
                    release = true;
                else if (env.MechanicClaimed(member.m_DestObjectID))
                    env.Post(OwnMessage(member, messages::MT_LEAVE_BUILDING));
                break;
            case 0x5b:
            case 0x5d: release = true; break;
            case 0x83: release = member.m_foundRepair != 0; break;
            default: break;
            }
            if (release) {
                env.ReleaseRepair(member.m_DestObjectID);
                env.SetState(0x5b);
                env.EnterState(0x5b);
            }
        }
        break;
    case messages::MT_DIRECT_TO_POINT:
        if (member.m_state == 0x5d) {
            const uint32_t type = static_cast<uint32_t>(env.RepairType(member.m_DestObjectID));
            if (type <= 3) env.ReturnRepair(member.m_DestObjectID, type);
            env.Post(OwnMessage(member, messages::MT_MOVESTOP));
            env.StopMechanicAnimation();
            env.Post(OwnMessage(member, messages::MT_STATECHANGE, 0x5b));
        }
        break;
    default: break;
    }
    return env.Base(message);
}

template<class Env> bool ReceiveLifeguard(object::Lifeguard& member, const messages::Message& message, Env& env)
{
    using namespace profession_receive;
    switch (message.type) {
    case messages::MT_NEW_DAY:
        member.m_NumSaves = 0;
        member.m_NumDrowned = 0;
        env.ClearStaffMessages(kLifeguardClass);
        break;
    case messages::MT_BUILDING_BEING_REMOVED:
        if (message.sender == member.m_HomeID) env.LeaveTower();
        break;
    case messages::MT_NEW_HOUR:
        if (AsksNoMore(env.GameDayTime(), member.m_happiness)) ForgetFeedbackRequest(member);
        break;
    case messages::MT_DIRECT_TO_POINT:
        if (member.m_state == kLifeguardStateA || member.m_state == kLifeguardStateB) StopAndChangeState(member, kLifeguardStateA, env);
        break;
    default: break;
    }
    return env.Base(message);
}

template<class Env> bool ReceiveRep(object::Rep& member, const messages::Message& message, Env& env)
{
    using namespace profession_receive;
    switch (message.type) {
    case messages::MT_NEW_DAY:
        member.m_happinessBoostToday = 0;
        member.m_GuestsTalkedToToday = 0;
        env.ClearStaffMessages(kRepClass);
        break;
    case messages::MT_BUILDING_BEING_REMOVED:
        if (message.sender == member.m_HomeID) {
            env.ReleaseGuest(member.m_sentToGuest);
        } else if (member.m_state == kRepStateWithGuest && message.sender == member.m_DestObjectID) {
            env.Post(OwnMessage(member, messages::MT_MOVESTOP));
            env.MoveToNextBuilding();
        }
        break;
    case messages::MT_NEW_HOUR:
        if (AsksNoMore(env.GameDayTime(), member.m_happiness)) ForgetFeedbackRequest(member);
        break;
    case messages::MT_DIRECT_TO_POINT:
        if (member.m_state == kRepStateWithGuest && member.m_sentToGuest == 0) StopAndChangeState(member, kRepStateWithGuest, env);
        break;
    default: break;
    }
    return env.Base(message);
}

template<class Env> bool ReceiveSecurity(object::SecurityGuard& member, const messages::Message& message, Env& env)
{
    using namespace profession_receive;
    switch (message.type) {
    case messages::MT_NEW_DAY:
        member.m_numMinorFinesToday = 0;
        member.m_numSeriousFinesToday = 0;
        member.m_fineTotalToday = 0.0f;
        member.m_numCautioned = 0;
        member.m_numVandals = 0;
        member.m_numFights = 0;
        env.ClearStaffMessages(kSecurityClass);
        member.m_numJailed = 0;
        break;
    case messages::MT_BUILDING_BEING_REMOVED:
        if (message.sender == member.m_HomeID) {
            // The offender the guard was dealing with is told to go on (state 12 of its own script) when the guard was in one of the offence states.
            const uint32_t state = static_cast<uint32_t>(member.m_state);
            const bool dealing = state >= kSecurityOffenceFirst &&
                    (state <= kSecurityOffenceLast || (state == kSecurityOffenceExtra && member.m_cantDoJob == 0));
            if (dealing && member.m_offenderId != 0) {
                messages::Message told{};
                told.sender = static_cast<int32_t>(member.m_id);
                told.receiver = member.m_offenderId;
                told.type = messages::MT_STATECHANGE;
                told.data = kOffenderReleasedState;
                env.Post(told);
            }
            BecomeLeaving(env);
        } else if (member.m_state == kSecurityPatrolState && message.sender == member.m_DestObjectID) {
            env.Post(OwnMessage(member, messages::MT_MOVESTOP));
            env.LookForOffender();
        }
        break;
    case messages::MT_NEW_HOUR:
        if (AsksNoMore(env.GameDayTime(), member.m_happiness)) ForgetFeedbackRequest(member);
        break;
    case messages::MT_DIRECT_TO_POINT:
        if (member.m_state == kSecurityPatrolState) StopAndChangeState(member, kSecurityStopState, env);
        break;
    default: break;
    }
    return env.Base(message);
}

template<class Env> bool ReceiveBuilder(object::Builder& member, const messages::Message& message, Env& env)
{
    using namespace profession_receive;
    switch (message.type) {
    case messages::MT_NEW_DAY: env.ClearStaffMessages(kBuilderClass); break;
    case messages::MT_CONSTRUCT:
    case messages::MT_DEMOLISH: member.m_DestObjectID = message.data; break;
    case messages::MT_BUILDING_BEING_REMOVED:
        if (message.sender == member.m_HomeID) {
            const uint32_t state = static_cast<uint32_t>(member.m_state);
            if (state >= kBuilderJobFirst && (state <= kBuilderJobLast || state == kBuilderJobExtra))
                env.FreeBuilder(static_cast<int32_t>(member.m_id));
            BecomeLeaving(env);
        }
        break;
    case messages::MT_NEW_HOUR:
        if (AsksNoMore(env.GameDayTime(), member.m_happiness)) ForgetFeedbackRequest(member);
        break;
    default: break;
    }
    return env.Base(message);
}

} // namespace obl::staff
