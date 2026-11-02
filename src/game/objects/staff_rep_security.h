#pragma once

#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_guest.h"
#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"

#include <cstdint>
#include <cstring>

// The three helpers of the representative script and the two helpers of the security guard script (docs/architecture/objects.md).
// LookForGuests, ArrivedAtGuest, MoveToNextBuilding, LookForOffender and
// ChooseAction are all member functions; LookForOffender takes one argument (a point it never reads) and answers a bool,
// the others take nothing and answer nothing.
//
// Every function answers `false` where the input is not valid or the work would never end (no building to walk to: a division by zero or an
// endless draw loop; a missing object or sprite) and then nothing has been drawn, written or sent: the caller ends the game with fatal::UnhandledInput.
// The arithmetic is written in `double`.
//
// Env (everything outside the member):
//   int Rand()                                                        the game's rand()
//   void Post(const messages::Message&)                                 global message handler Post
//   void PostDelayed(const messages::Message&, float delay)             global message handler PostDelayed
//   object::Base* Object(int32_t id)                            object lookup by id
//   uint32_t ObjectsNear(int radius, object::Base* const*& list)  the objects near the member's own tile
//   uint32_t Buildings(object::Base* const*& list)              the list of buildings (class 2)
//   bool IsJetty(const object::Base* building)                  the building's behavior is Jetty (building_behavior.h)
//   bool IsApproachable(object::Base*)                          GuestIsApproachable
//   bool FindWalkablePointNear(float pos[3], int dx, int dy)          the member's FindWalkablePointNear(pos, dx, dy)
//   void PlayWalk()                                                   the member's virtual playAnimation("WALK_NORMAL", 0, -1, 1, 0)
//   void Position(object::Base*, float out[3])                  the object's position
//   void FaceTowards(ui::SpriteInstance*, const float target[3], const float from[3])   turning the sprite towards the target
//   void PushThought(uint32_t bits)                                   pushes a thought of the member
//   uint32_t CountGuests()                                            the number of guests
//   uint32_t CountBins()                                              the number of objects named "bin"
namespace obl::staff::repsec {

constexpr int32_t kMsgStateChange = 3; // MT_STATECHANGE
constexpr int32_t kMsgMove2Point = 5; // MT_MOVE2POINT: flags = x, data = y (truncated world coordinates)
constexpr int32_t kMsgPavement2Object = 9; // MT_PAVEMENT2OBJECT: data = the building
constexpr int32_t kMsgFinishAction = 0x38; // MT_FINISH_ACTION
constexpr int32_t kMsgPlayAnimation = 0x3c; // MT_PLAYANIMATION
constexpr int32_t kMsgLookAround = 0x54; // MT_LOOK_AROUND
constexpr int32_t kStateTalking = 0x1a; // the data of the MT_STATECHANGE sent to the guest (and the extra of the MT_PLAYANIMATION)
constexpr float kLookAgainDelay = 2.0f;

namespace detail {

inline messages::Message Make(int32_t sender, int32_t receiver, int32_t type, int32_t flags = 0, int32_t data = 0)
{
    messages::Message message{};
    message.sender = sender;
    message.receiver = receiver;
    message.type = type;
    message.flags = flags;
    message.data = data;
    return message;
}

inline int32_t Id(const object::Base& object)
{
    return static_cast<int32_t>(object.m_id);
}

// A raw 32-bit copy of a float (keeps a signalling NaN as it is).
inline void CopyBits(float& to, const float& from)
{
    std::memcpy(&to, &from, sizeof(float));
}
inline void CopyPoint(float (&to)[3], const float (&from)[3])
{
    std::memcpy(to, from, sizeof(to));
}

// A float copied by value turns a signalling NaN into a quiet one (bit 22).
inline void CopyThroughFpu(float& to, const float& from)
{
    uint32_t bits;
    std::memcpy(&bits, &from, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0) bits |= 0x00400000u;
    std::memcpy(&to, &bits, sizeof(bits));
}

// The three exits of both "look again" paths: the member asks itself to look around again in two seconds.
template<class Member, class Env> void LookAgain(const Member& employee, Env& env)
{
    env.PostDelayed(Make(Id(employee), Id(employee), kMsgLookAround), kLookAgainDelay);
}

// The random building of the two walking helpers: a draw `rand % count` redrawn while it is the building the member is bound for (`m_DestObjectID`,
// the destination field) or a JETTY. The draw loop has no bound of its own; when every building is excluded (or none exists: a division by zero) it declines, drawing nothing.
template<class Member, class Env> const object::Base* DrawBuilding(const Member& employee, Env& env)
{
    object::Base* const* list = nullptr;
    const uint32_t count = env.Buildings(list);
    if (!list || count == 0) return nullptr;
    auto eligible = [&](const object::Base* building) {
        return building && Id(*building) != employee.m_DestObjectID && !env.IsJetty(building);
    };
    bool any = false;
    for (uint32_t i = 0; i < count && !any; ++i) any = eligible(list[i]);
    if (!any) return nullptr;
    for (;;) {
        const object::Base* building = list[static_cast<uint32_t>(env.Rand()) % count];
        if (eligible(building)) return building;
    }
}

} // namespace detail

// ---- MoveToNextBuilding () -----------------------------------------------------------------------------------------------------
// Draws a building other than the one the representative is heading for and no JETTY, starts the walk animation and sends itself MT_PAVEMENT2OBJECT
// to the building.
template<class Env> bool MoveToNextBuilding(object::Rep& employee, Env& env)
{
    const object::Base* building = detail::DrawBuilding(employee, env);
    if (!building) return false;
    env.PlayWalk();
    env.Post(detail::Make(detail::Id(employee), detail::Id(employee), kMsgPavement2Object, 0, detail::Id(*building)));
    return true;
}

// ---- LookForGuests () ----------------------------------------------------------------------------------------------------------
// Takes a random object within two tiles (`rand % count`, no draw when there is none). The representative looks again in two seconds when the
// object is the guest it talked to last (`m_talking_id`), is not approachable, or no walkable point (+-20, +-10 around it) exists. Otherwise it
// remembers the guest, sends it MT_STATECHANGE 0x1a, starts the walk animation, sets `m_sentToGuest` with the point it found
// (`m_GuestOffSetPosX/Y/Z`) and walks to it (MT_MOVE2POINT, truncated x and y).
template<class Env> bool LookForGuests(object::Rep& employee, Env& env)
{
    object::Base* const* list = nullptr;
    const uint32_t count = env.ObjectsNear(2, list);
    if (count == 0) {
        detail::LookAgain(employee, env);
        return true;
    }
    object::Base* guest = list[static_cast<uint32_t>(env.Rand()) % count];
    if (employee.m_talking_id == detail::Id(*guest) || !env.IsApproachable(guest)) {
        detail::LookAgain(employee, env);
        return true;
    }
    float point[3];
    detail::CopyPoint(point, guest->m_WorldXYZ);
    if (!env.FindWalkablePointNear(point, 0x14, 10)) {
        detail::LookAgain(employee, env);
        return true;
    }
    const int32_t guestId = detail::Id(*guest);
    employee.m_talking_id = guestId;
    env.Post(detail::Make(detail::Id(employee), guestId, kMsgStateChange, 0, kStateTalking));
    env.PlayWalk();
    employee.m_sentToGuest = 1;
    detail::CopyBits(employee.m_GuestOffSetPosX, point[0]);
    detail::CopyBits(employee.m_GuestOffSetPosY, point[1]);
    detail::CopyBits(employee.m_GuestOffSetPosZ, point[2]);
    env.Post(detail::Make(detail::Id(employee), detail::Id(employee), kMsgMove2Point, FloatToIntWrap(static_cast<double>(point[0])),
            FloatToIntWrap(static_cast<double>(point[1]))));
    return true;
}

// ---- ArrivedAtGuest () ---------------------------------------------------------------------------------------------------------
// The representative faces the guest it talks to, sends itself MT_STATECHANGE 9 and, after a random-free talk time, MT_FINISH_ACTION:
// `float(double(uint32(max - min)) * m_timeWithGuests + double(int32(min)))` seconds (one rounding to float at the store). The guest gets
// MT_PLAYANIMATION with extra 0x1a, and `m_sentToGuest` is cleared. A missing guest or sprite is tolerated: answers false.
template<class Env> bool ArrivedAtGuest(object::Rep& employee, Env& env)
{
    object::Base* guest = env.Object(employee.m_talking_id);
    auto* sprite = static_cast<ui::SpriteInstance*>(employee.m_sprite);
    if (!guest || !sprite) return false;
    const float own[3] = {employee.m_WorldXYZ[0], employee.m_WorldXYZ[1], employee.m_WorldXYZ[2]};
    float there[3];
    env.Position(guest, there);
    env.FaceTowards(sprite, there, own);
    const int32_t id = detail::Id(employee);
    env.Post(detail::Make(id, id, kMsgStateChange, 0, kMsgPavement2Object));
    const double talk = static_cast<double>(static_cast<uint32_t>(employee.m_MaxTalkTime) - static_cast<uint32_t>(employee.m_MinTalkTime)) *
                    static_cast<double>(employee.m_timeWithGuests) +
            static_cast<double>(employee.m_MinTalkTime);
    env.PostDelayed(detail::Make(id, id, kMsgFinishAction), static_cast<float>(talk));
    messages::Message animate = detail::Make(id, employee.m_talking_id, kMsgPlayAnimation);
    animate.extra = kStateTalking;
    env.Post(animate);
    employee.m_sentToGuest = 0;
    return true;
}

// ---- LookForOffender () ---------------------------------------------------------------------------------------------------
// `m_doubleFine` is cleared. A guard that stands still (`m_moving` clear) first walks to a random building other than its destination and no JETTY
// (as MoveToNextBuilding, MT_PAVEMENT2OBJECT). Then the objects within six tiles are inspected in list order, each one resetting
// `m_severity` to 0: a guest that is approachable and whose offence state is 1 (severity 1; not the last offender) or 2 (severity 2; neither the
// last offender nor the fight partner `m_fightWithId`) or 3 (severity 2; not the last offender) becomes the offender: its id goes to `m_offenderId`,
// its position to `m_MisdemeanourPos` (x with a NaN quieted, y and z raw) and the answer is 1. No such guest: 0 (and the severity untouched when the
// list is empty).
template<class Env> bool LookForOffender(object::SecurityGuard& employee, Env& env, uint8_t& found)
{
    const object::Base* building = nullptr;
    if (!employee.m_moving) {
        building = detail::DrawBuilding(employee, env);
        if (!building) return false;
    }
    employee.m_doubleFine = 0;
    const int32_t id = detail::Id(employee);
    if (building) {
        env.PlayWalk();
        env.Post(detail::Make(id, id, kMsgPavement2Object, 0, detail::Id(*building)));
    }
    found = 0;
    object::Base* const* list = nullptr;
    const uint32_t count = env.ObjectsNear(6, list);
    for (uint32_t i = 0; i < count; ++i) {
        object::Base* object = list[i];
        employee.m_severity = 0;
        if (object->m_type != object::ObjectType::Guest || !env.IsApproachable(object)) continue;
        const int32_t offence = static_cast<object::Guest*>(object)->m_OffenceState;
        const int32_t objectId = detail::Id(*object);
        int32_t severity;
        if (offence == 1 && objectId != employee.m_offenderId)
            severity = 1;
        else if (offence == 2 && objectId != employee.m_offenderId && objectId != employee.m_fightWithId)
            severity = 2;
        else if (offence == 3 && objectId != employee.m_offenderId)
            severity = 2;
        else
            continue;
        employee.m_severity = severity;
        employee.m_offenderId = objectId;
        const object::Base* located = env.Object(objectId);
        if (!located) located = object; // cannot happen: the id comes from the object manager's own list
        detail::CopyThroughFpu(employee.m_MisdemeanourPos[0], located->m_WorldXYZ[0]);
        detail::CopyBits(employee.m_MisdemeanourPos[1], located->m_WorldXYZ[1]);
        detail::CopyBits(employee.m_MisdemeanourPos[2], located->m_WorldXYZ[2]);
        found = 1;
        return true;
    }
    return true;
}

// ---- ChooseAction () ------------------------------------------------------------------------------------------------------
// What the guard does about the offender `m_offenderId`: `m_action` starts at 0 and stays there when the offender is not approachable or no
// walkable point (+-20, +-10) exists next to it. Otherwise the point goes to `m_FineOffsetX/Y/Y_7f0` and, by the tolerance class `m_toleranceClass` and the
// severity `m_severity`: class 0: severity 2 -> fine level 0, action 1, else action 0; class 1: severity 2 -> fine level 1, action 1, else level 0,
// action 1; class 2: severity 2 -> action 2 with the point searched again within (+-13, +-8) and stored whatever the answer, else fine level 1, action 1;
// any other class leaves action and fine level alone. Then by the offence state of the guest: 1 (the guests outnumber the bins fifteen to one:
// no feedback is required any more, bit 2 of the status cleared) thinks 0x200 (action 2) or 0x40; 2 counts a fight (`m_numFights` > 5: no feedback
// required, bit 8 cleared) and thinks 0x800 (action 2) or 0x100; 3 counts a vandal (`m_numVandals` > 5, bit 4 cleared) and thinks 0x400 or 0x80.
// A missing offender object is tolerated: answers false.
template<class Env> bool ChooseAction(object::SecurityGuard& employee, Env& env)
{
    object::Base* guest = env.Object(employee.m_offenderId);
    if (!guest) return false;
    employee.m_action = 0;
    if (!env.IsApproachable(guest)) return true;
    float point[3];
    detail::CopyPoint(point, guest->m_WorldXYZ);
    if (!env.FindWalkablePointNear(point, 0x14, 10)) return true;
    detail::CopyBits(employee.m_FineOffsetX, point[0]);
    detail::CopyBits(employee.m_FineOffsetY, point[1]);
    detail::CopyBits(employee.m_FineOffsetY_7f0, point[2]);
    const int32_t severity = employee.m_severity;
    switch (employee.m_toleranceClass) {
    case 0:
        if (severity == 2) {
            employee.m_FineLevel = 0;
            employee.m_action = 1;
        } else {
            employee.m_action = 0;
        }
        break;
    case 1:
        employee.m_FineLevel = severity == 2 ? 1 : 0;
        employee.m_action = 1;
        break;
    case 2:
        if (severity == 2) {
            employee.m_action = 2;
            float again[3];
            detail::CopyPoint(again, guest->m_WorldXYZ);
            env.FindWalkablePointNear(again, 0xd, 8);
            detail::CopyBits(employee.m_FineOffsetX, again[0]);
            detail::CopyBits(employee.m_FineOffsetY, again[1]);
            detail::CopyBits(employee.m_FineOffsetY_7f0, again[2]);
        } else {
            employee.m_FineLevel = 1;
            employee.m_action = 1;
        }
        break;
    default: break;
    }
    const int32_t offence = static_cast<object::Guest*>(guest)->m_OffenceState;
    if (offence == 1) {
        if (env.CountGuests() > static_cast<uint32_t>(env.CountBins()) * 15u) {
            employee.m_RequiredFeedback = 0;
            employee.m_FeedbackStatus &= ~2;
        }
        env.PushThought(employee.m_action == 2 ? 0x200 : 0x40);
    } else if (offence == 2) {
        if (++employee.m_numFights > 5) {
            employee.m_RequiredFeedback = 0;
            employee.m_FeedbackStatus &= ~8;
        }
        env.PushThought(employee.m_action == 2 ? 0x800 : 0x100);
    } else if (offence == 3) {
        if (++employee.m_numVandals > 5) {
            employee.m_RequiredFeedback = 0;
            employee.m_FeedbackStatus &= ~4;
        }
        env.PushThought(employee.m_action == 2 ? 0x400 : 0x80);
    }
    return true;
}

} // namespace obl::staff::repsec
