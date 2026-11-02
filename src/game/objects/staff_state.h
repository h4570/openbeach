#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_staff.h"
#include "game/objects/staff_profession_receive.h"

#include <cstdint>

// Profession Sack methods release job-specific resources before the shared severance payment.
// OnEnterState and OnExitState run the profession scripts and return true; Rep and Security share entry logic.
// An injected environment supplies the ordered game calls, and tests record those calls:
//   BaseSack pays severance; FreeBuilder releases a construction job; DidNotClean records unfinished cleaning.
//   LeaveTower, ReturnJobAndLeave and ReleaseGuest release profession-specific work.
//   Post sends a message with zeroed trailing words; SetState and EnterState change the staff state.
//   AddBuilder registers the builder; HomeInfluenceArea queries the tower's swimming area when its home exists.
//   RunEntryScript / RunExitScript execute the current state's handlers.
namespace obl::staff {

namespace staff_state {

// MT_STAFF_CREATED: state 1 of every profession tells the home building that its member has started.
constexpr uint32_t kStartState = 1;
// The states of the scripts that remember the home as the target (the member is on the job the building gave it).
constexpr uint32_t kBuilderHomeTargetState = 0x60;
constexpr uint32_t kCleanerHomeTargetState = 0x69;
constexpr uint32_t kMechanicHomeTargetState = 0x5c;

// Cleaner states in which a job is open (0x6d..0x73, and 0x83 while the member can do its job); the guard's offence states.
constexpr uint32_t kCleanerJobFirst = 0x6d, kCleanerJobLast = 0x73, kCleanerJobExtra = 0x83;

inline messages::Message StartedNotice(const object::Staff& member)
{
    messages::Message message{};
    message.sender = static_cast<int32_t>(member.m_id);
    message.receiver = member.m_HomeID;
    message.type = messages::MT_STAFF_CREATED;
    return message;
}

} // namespace staff_state

// ---- Profession dismissal -----------------------------------------------------------------------------------------------------------

template<class Env> void SackBuilder(object::Builder& member, Env& env)
{
    using namespace profession_receive;
    env.BaseSack();
    const uint32_t state = static_cast<uint32_t>(member.m_state);
    if (state >= kBuilderJobFirst && (state <= kBuilderJobLast || state == kBuilderJobExtra))
        env.FreeBuilder(static_cast<int32_t>(member.m_id));
    BecomeLeaving(env);
}

template<class Env> void SackCleaner(object::Cleaner& member, Env& env)
{
    using namespace profession_receive;
    using namespace staff_state;
    env.BaseSack();
    const uint32_t state = static_cast<uint32_t>(member.m_state);
    if (state >= kCleanerJobFirst && (state <= kCleanerJobLast || (state == kCleanerJobExtra && member.m_cantDoJob == 0)))
        env.DidNotClean(member.m_DestObjectID);
    BecomeLeaving(env);
}

template<class Env> void SackLifeguard(object::Lifeguard&, Env& env)
{
    env.BaseSack();
    env.LeaveTower();
}

template<class Env> void SackMechanic(object::Mechanic& member, Env& env)
{
    env.BaseSack();
    env.ReturnJobAndLeave(member.m_foundRepair != 0);
}

template<class Env> void SackRep(object::Rep& member, Env& env)
{
    env.BaseSack();
    env.ReleaseGuest(member.m_sentToGuest);
}

template<class Env> void SackSecurity(object::SecurityGuard& member, Env& env)
{
    using namespace profession_receive;
    env.BaseSack();
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
}

// ---- profession OnEnterState / staff OnExitState ---------------------------------------------------------------------------------------
// The state of the script is entered last (the script runs after the member's own bookkeeping); every one answers true.

template<class Env> bool EnterBuilder(object::Builder& member, uint32_t state, Env& env)
{
    using namespace staff_state;
    if (state == kStartState) {
        env.Post(StartedNotice(member));
        env.AddBuilder(static_cast<int32_t>(member.m_id));
    } else if (state == kBuilderHomeTargetState) {
        member.m_DestObjectID = member.m_HomeID;
    }
    env.RunEntryScript(state);
    return true;
}

template<class Env> bool EnterCleaner(object::Cleaner& member, uint32_t state, Env& env)
{
    using namespace staff_state;
    if (state == kStartState)
        env.Post(StartedNotice(member));
    else if (state == kCleanerHomeTargetState)
        member.m_DestObjectID = member.m_HomeID;
    env.RunEntryScript(state);
    return true;
}

template<class Env> bool EnterMechanic(object::Mechanic& member, uint32_t state, Env& env)
{
    using namespace staff_state;
    if (state == kStartState)
        env.Post(StartedNotice(member));
    else if (state == kMechanicHomeTargetState)
        member.m_DestObjectID = member.m_HomeID;
    env.RunEntryScript(state);
    return true;
}

// A lifeguard whose home is gone keeps its influence area field unchanged.
template<class Env> bool EnterLifeguard(object::Lifeguard& member, uint32_t state, Env& env)
{
    using namespace staff_state;
    if (state == kStartState) {
        env.Post(StartedNotice(member));
        int32_t area = 0;
        if (env.HomeInfluenceArea(area)) member.m_InfluenceArea = area;
    }
    env.RunEntryScript(state);
    return true;
}

// Rep and Security guard share the function.
template<class Staff, class Env> bool EnterRepOrSecurity(Staff& member, uint32_t state, Env& env)
{
    using namespace staff_state;
    if (state == kStartState) env.Post(StartedNotice(member));
    env.RunEntryScript(state);
    return true;
}

template<class Env> bool ExitStaff(const object::Staff& member, Env& env)
{
    env.RunExitScript(member.m_state);
    return true;
}

} // namespace obl::staff
