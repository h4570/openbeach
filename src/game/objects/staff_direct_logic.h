#pragma once

#include "game/messages/message_state.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_staff.h"

#include <cstdint>
#include <cstring>

// The orders the player gives a staff member (docs/architecture/objects.md): `DirectToPoint` / `DirectToTarget` (what
// the send-staff GUI mode calls on a click) and the six professions' DirectToObject (takes the object id and returns no value):
// Builder, Cleaner, Lifeguard, Mechanic, Rep, SecurityGuard.
//
// Every copy of a position keeps the raw 32 bits. Only the message words listed below carry meaning
// (the constants 0x83 / 0 below are checked by the unit tests).
//
// DirectToObject asks the profession's manager (or the object itself) whether the job can be taken. Yes: the member remembers the target, clears
// `m_cantDoJob` and starts the job with a message to itself. No: the `m_cantDoJob` byte is set, the required feedback is cleared and the bit of this
// failure is taken off the feedback status, then MT_PLAYANIMATION with `extra` 0x83 and `data` 0 reports it (the same pair of words, bit 1u << 22
// or the lifeguard's 8/0x10/0x20, are what UpdateFeedback tests).
//
// Env (everything outside the member):
//   void Post(const messages::Message&)                              posts to the global message handler
//   void Send(const messages::Message&)                              the member's own Send (OnSend script, then Post; Builder and SecurityGuard)
//   object::Base* Object(int32_t id)                         the object of an id
//   bool IsApproachable(object::Base*)                       the guest's IsApproachable
//   bool FindWalkablePointNear(float pos[3], int dx, int dy)       the member's FindWalkablePointNear(pos, dx, dy)
//   int32_t AssignBuilder(int32_t builderId, uint32_t jobId)       assigning a builder to a job (-1 = refused)
//   int32_t ClaimForCleaner(uint32_t objectId, int32_t cleanerId)  cleaning::cleaning_facade::ClaimObject (-1 = refused)
//   int32_t ClaimForMechanic(uint32_t objectId, int32_t mechanicId, int32_t* type)   repair::repair_facade::ClaimObject (-1 = refused; writes the job type)
namespace obl::staff::direct {

constexpr int32_t kMsgStateChange = 3; // MT_STATECHANGE
constexpr int32_t kMsgPlayAnimation = 0x3c; // MT_PLAYANIMATION: data 1 = the job was taken, 0 = refused; extra 0x83
constexpr int32_t kMsgDirectToPoint =
        0x56; // MT_DIRECT_TO_POINT (also sent for DirectToTarget: the receive branch tells them apart by `m_directedToObject`)
constexpr int32_t kReportMember14 = 0x83;
constexpr int32_t kStateGuestNeedsRescue = 0x13; // the guest state the lifeguard accepts
constexpr int32_t kBuilderJobTaken = 0x61, kBuilderJobShared = 0x62; // data of the MT_STATECHANGE to the builder itself
constexpr int32_t kSecurityArrest = 0x7d, kSecurityFight = 0x7e;
constexpr uint32_t kSeverityBit = 1u << 22; // feedback bit the refusal clears (Cleaner/Mechanic/Rep/Security)
constexpr uint32_t kBuilderFeedbackBit = 4;
constexpr uint32_t kLifeguardNoObject = 8, kLifeguardBusy = 0x10, kLifeguardNotGuest = 0x20;

namespace detail {

inline messages::Message Make(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data, int32_t extra = 0)
{
    messages::Message message{};
    message.sender = sender;
    message.receiver = receiver;
    message.type = type;
    message.flags = flags;
    message.data = data;
    message.extra = extra;
    return message;
}

inline int32_t Id(const object::Base& object)
{
    return static_cast<int32_t>(object.m_id);
}

inline void CopyBits(float& to, const float& from)
{
    std::memcpy(&to, &from, sizeof(float));
}
inline void CopyPoint(float (&to)[3], const float (&from)[3])
{
    std::memcpy(to, from, sizeof(to));
}

// The refusal every profession ends with: MT_PLAYANIMATION to itself with data 0.
template<class Env> void Refuse(const object::Staff& employee, Env& env)
{
    env.Post(Make(Id(employee), Id(employee), kMsgPlayAnimation, 0, 0, kReportMember14));
}
// The acceptance of the professions that post it: the same message with data 1.
template<class Env> void Accept(const object::Staff& employee, Env& env)
{
    env.Post(Make(Id(employee), Id(employee), kMsgPlayAnimation, 0, 1, kReportMember14));
}

} // namespace detail

// ---- Staff DirectToPoint / DirectToTarget ------------------------------------------------------------------------------------------
// `point` is the world point (x, y) the player clicked; the member posts MT_DIRECT_TO_POINT to itself either way.
template<class Env> void DirectToPoint(object::Staff& employee, const float* point, Env& env)
{
    employee.m_DirectStaff = 1;
    detail::CopyBits(employee.m_DirectPos[0], point[0]);
    detail::CopyBits(employee.m_DirectPos[1], point[1]);
    env.Post(detail::Make(detail::Id(employee), detail::Id(employee), kMsgDirectToPoint, 0, 0));
}

template<class Env> void DirectToTarget(object::Staff& employee, int32_t targetId, Env& env)
{
    employee.m_DirectStaff = 1;
    employee.m_directedToObject = 1;
    employee.m_GotoObject = targetId;
    env.Post(detail::Make(detail::Id(employee), detail::Id(employee), kMsgDirectToPoint, 0, 0));
}

// ---- Builder DirectToObject ----------------------------------------------------------------------------------------------------------
// `m_DestObjectID` is written even when the manager refuses. The acceptance is sent through the member's own Send with `m_cantDoJob` already cleared.
template<class Env> bool BuilderDirectToObject(object::Builder& employee, uint32_t objectId, Env& env)
{
    const int32_t result = env.AssignBuilder(detail::Id(employee), objectId);
    employee.m_DestObjectID = static_cast<int32_t>(objectId);
    if (result != -1) {
        employee.m_cantDoJob = 0;
        env.Send(detail::Make(
                detail::Id(employee), detail::Id(employee), kMsgStateChange, 0, result == 0 ? kBuilderJobTaken : kBuilderJobShared));
        return true;
    }
    employee.m_FeedbackStatus = static_cast<int32_t>(static_cast<uint32_t>(employee.m_FeedbackStatus) & ~kBuilderFeedbackBit);
    employee.m_cantDoJob = 1;
    employee.m_RequiredFeedback = 0;
    detail::Refuse(employee, env);
    return true;
}

// ---- Cleaner DirectToObject ----------------------------------------------------------------------------------------------------------
// The kind cleaning::cleaning_facade::ClaimObject answers (0 litter, 1 bin, 2 toilet; any other kind leaves `m_UrgentType` as it is) is kept in `m_UrgentType`.
template<class Env> bool CleanerDirectToObject(object::Cleaner& employee, uint32_t objectId, Env& env)
{
    const int32_t kind = env.ClaimForCleaner(objectId, detail::Id(employee));
    if (kind != -1) {
        employee.m_DestObjectID = static_cast<int32_t>(objectId);
        if (kind == 2 || kind == 1 || kind == 0) employee.m_UrgentType = kind;
        employee.m_cantDoJob = 0;
        detail::Accept(employee, env);
        return true;
    }
    employee.m_RequiredFeedback = 0;
    employee.m_cantDoJob = 1;
    employee.m_FeedbackStatus = static_cast<int32_t>(static_cast<uint32_t>(employee.m_FeedbackStatus) & ~kSeverityBit);
    detail::Refuse(employee, env);
    return true;
}

// ---- Mechanic DirectToObject ---------------------------------------------------------------------------------------------------------
// repair::repair_facade::ClaimObject writes the job type straight into `m_repairType`.
template<class Env> bool MechanicDirectToObject(object::Mechanic& employee, uint32_t objectId, Env& env)
{
    const int32_t result = env.ClaimForMechanic(objectId, detail::Id(employee), &employee.m_repairType);
    if (result != -1) {
        employee.m_DestObjectID = static_cast<int32_t>(objectId);
        employee.m_foundRepair = 1;
        employee.m_cantDoJob = 0;
        detail::Accept(employee, env);
        return true;
    }
    employee.m_foundRepair = 0;
    employee.m_cantDoJob = 1;
    employee.m_RequiredFeedback = 0;
    employee.m_FeedbackStatus = static_cast<int32_t>(static_cast<uint32_t>(employee.m_FeedbackStatus) & ~kSeverityBit);
    detail::Refuse(employee, env);
    return true;
}

// ---- Lifeguard DirectToObject --------------------------------------------------------------------------------------------------------
// Only a guest in state 0x13 whose action state is 0 can be rescued. Refusals: no object (bit 8), the guest is busy (bit 0x10; the rescue is
// forgotten: `m_saving_id` = -1), another state (bit 0x20); the bit is taken off `m_FeedbackStatus`.
template<class Env> bool LifeguardDirectToObject(object::Lifeguard& employee, uint32_t objectId, Env& env)
{
    uint32_t bit = 0;
    object::Base* object = env.Object(static_cast<int32_t>(objectId));
    if (!object) {
        bit = kLifeguardNoObject;
    } else {
        if (object->m_state == kStateGuestNeedsRescue) {
            employee.m_saving_id = static_cast<int32_t>(objectId);
            if (static_cast<object::Guest*>(object)->m_ActionState == 0) {
                employee.m_saving = 1;
                detail::Accept(employee, env);
            } else {
                employee.m_saving_id = -1;
                employee.m_saving = 0;
                bit = kLifeguardBusy;
            }
        } else {
            bit = kLifeguardNotGuest;
        }
        employee.m_cantDoJob = 0;
        if (bit == 0) return true;
    }
    employee.m_FeedbackStatus = static_cast<int32_t>(static_cast<uint32_t>(employee.m_FeedbackStatus) & ~bit);
    employee.m_cantDoJob = 1;
    employee.m_RequiredFeedback = 0;
    detail::Refuse(employee, env);
    return true;
}

// ---- Rep DirectToObject --------------------------------------------------------------------------------------------------------------
// The guest has to be approachable and a walkable point (+-20, +-10) has to exist next to it: the member remembers the guest (`m_talking_id`), tells it
// MT_STATECHANGE 0x1a (the guest stops), walks to the point (`m_GuestOffSet*`, `m_sentToGuest`) and starts the job. No object: false (the caller ends the
// game with fatal::UnhandledInput).
template<class Env> bool RepDirectToObject(object::Rep& employee, uint32_t objectId, Env& env)
{
    object::Base* guest = env.Object(static_cast<int32_t>(objectId));
    if (!guest) return false;
    if (env.IsApproachable(guest)) {
        float point[3];
        detail::CopyPoint(point, guest->m_WorldXYZ);
        if (env.FindWalkablePointNear(point, 0x14, 10)) {
            const int32_t guestId = detail::Id(*guest);
            employee.m_talking_id = guestId;
            env.Post(detail::Make(detail::Id(employee), guestId, kMsgStateChange, 0, 0x1a));
            employee.m_sentToGuest = 1;
            detail::CopyBits(employee.m_GuestOffSetPosX, point[0]);
            detail::CopyBits(employee.m_GuestOffSetPosY, point[1]);
            detail::CopyBits(employee.m_GuestOffSetPosZ, point[2]);
            employee.m_cantDoJob = 0;
            detail::Accept(employee, env);
            return true;
        }
    }
    employee.m_cantDoJob = 1;
    employee.m_RequiredFeedback = 0;
    employee.m_FeedbackStatus = static_cast<int32_t>(static_cast<uint32_t>(employee.m_FeedbackStatus) & ~kSeverityBit);
    detail::Refuse(employee, env);
    return true;
}

// ---- SecurityGuard DirectToObject ----------------------------------------------------------------------------------------------------
// Like the representative (approachable guest, walkable point (+-20, +-10) stored in `m_FineOffset*`) and then by tolerance class `m_toleranceClass`:
// 0 and 1 set the fine level to the class and send themselves MT_STATECHANGE 0x7d; 2 looks for a second point (+-13, +-8), stores it whether found or not
// and sends 0x7e; another class sends nothing. `m_offenderId` is the guest in all three. The messages go through the member's own Send, and
// `m_cantDoJob` is cleared after them. Refusal: bit 1u << 22 of `m_FeedbackStatus`.
template<class Env> bool SecurityDirectToObject(object::SecurityGuard& employee, uint32_t objectId, Env& env)
{
    object::Base* guest = env.Object(static_cast<int32_t>(objectId));
    if (guest && env.IsApproachable(guest)) {
        float point[3];
        detail::CopyPoint(point, guest->m_WorldXYZ);
        if (env.FindWalkablePointNear(point, 0x14, 10)) {
            detail::CopyBits(employee.m_FineOffsetX, point[0]);
            detail::CopyBits(employee.m_FineOffsetY, point[1]);
            detail::CopyBits(employee.m_FineOffsetY_7f0, point[2]);
            const int32_t id = detail::Id(employee);
            if (employee.m_toleranceClass == 0 || employee.m_toleranceClass == 1) {
                employee.m_offenderId = static_cast<int32_t>(objectId);
                employee.m_FineLevel = employee.m_toleranceClass;
                env.Send(detail::Make(id, id, kMsgStateChange, 1, kSecurityArrest));
            } else if (employee.m_toleranceClass == 2) {
                float second[3];
                detail::CopyPoint(second, guest->m_WorldXYZ);
                env.FindWalkablePointNear(second, 0xd, 8);
                detail::CopyBits(employee.m_FineOffsetX, second[0]);
                detail::CopyBits(employee.m_FineOffsetY, second[1]);
                detail::CopyBits(employee.m_FineOffsetY_7f0, second[2]);
                employee.m_offenderId = static_cast<int32_t>(objectId);
                env.Send(detail::Make(id, id, kMsgStateChange, 1, kSecurityFight));
            }
            employee.m_cantDoJob = 0;
            return true;
        }
    }
    employee.m_RequiredFeedback = 0;
    employee.m_cantDoJob = 1;
    employee.m_FeedbackStatus = static_cast<int32_t>(static_cast<uint32_t>(employee.m_FeedbackStatus) & ~kSeverityBit);
    detail::Refuse(employee, env);
    return true;
}

} // namespace obl::staff::direct
