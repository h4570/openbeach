#pragma once

#include "core/float_to_int.h"
#include "core/ascii_case.h"
#include "game/messages/message_state.h"
#include "game/objects/object_table.h"

#include <cstdint>
#include <cstring>
#include <optional>

// ReceiveMessage of a table, by message type (docs/architecture/objects.md). The
// answer is false only where a branch says so: every branch without an answer of its own says true.
//
// Env:
//   bool BaseReceive(Furniture&, const Message&)                 the static object ReceiveMessage
//   void SetWaiterPosition(int32_t waiter, const float xyz[3])     SetPosition of the waiter object
//   void ShowWaiter(int32_t waiter)                                Entity::m_visible of that object = 1
//   void StartWaiterAnim(int32_t waiter, const char* anim)         sets the waiter sprite animation by name (at the world time, a copy of the
//                                                                  waiter's position), then the sprite's m_animType = 0
//   void Post(const Message&)                                      global message handler Post
//   void PostDelayed(const Message&, float delay)                  global message handler PostDelayed
// MT_NEXT_MOVE_POINT additionally needs (the guest is the sender of the message, which sits at one of the chairs):
//   int32_t GuestActionState(int32_t guest)                        Guest::m_ActionState
//   void SetGuestActionState(int32_t guest, int32_t value)
//   void ShowGuest(int32_t guest)                                  Entity::m_visible = 1
//   void SetGuestPosition(int32_t guest, const float xyz[3])       SetPosition of the guest
//   void SetGuestDirection(int32_t guest, int32_t direction)       the guest's sprite direction, clamped to the current animation
//   void BuildingPosition(int32_t building, float out[2])          Entity::m_WorldXYZ x and y
//   void PostGuestAnimation(const Furniture&, int32_t guest, const char* animation)   MT_PLAYANIMATION, text kept alive (a static string)
//   void StartChairAnim(const Furniture&, uint32_t chair, const char* animation, int32_t animType)   starting the named sprite animation on the chair sprite
//                                                                  (the world game time, no position), then its m_animType
// MT_DO_NEXT_ACTION additionally needs int Rand (the game's rand, import) and StartWaiterAnim/PostGuestAnimation as above.
namespace obl::table {

using messages::Message;
using object::Furniture;

constexpr float kFinishActionDelay = 20.0f;

// AddRotatedOffset: `base` plus `offset` turned by the table's rotation. A rotation outside
// 0..3 gives false and the point stays as it was.
inline bool AddRotatedOffset(float out[2], const float base[2], const float offset[2], int32_t rotation)
{
    switch (rotation) {
    case 0:
        out[0] = base[0] + offset[0];
        out[1] = base[1] + offset[1];
        return true;
    case 1:
        out[0] = base[0] + offset[1];
        out[1] = base[1] - offset[0];
        return true;
    case 2:
        out[0] = base[0] - offset[0];
        out[1] = base[1] - offset[1];
        return true;
    case 3:
        out[0] = base[0] - offset[1];
        out[1] = base[1] + offset[0];
        return true;
    default: return false;
    }
}

template<class Env> void MoveWaiter(Furniture& table, const float point[2], Env& env)
{
    Message move{};
    move.sender = static_cast<int32_t>(table.m_id);
    move.receiver = table.m_WaiterID;
    move.type = messages::MT_MOVE2SEAPOINT;
    move.flags = FloatToIntWrap(point[0]);
    move.data = FloatToIntWrap(point[1]);
    env.Post(move);
}

// MT_MOVEIMHERE ignores the sender. All its exits return an unwritten stack byte.
// Env additionally supplies HideWaiter, SetWaiterDirection, PostWaiterAnimation and WaiterSpawnPoint.
template<class Env> void MoveImHere(Furniture& table, Env& env)
{
    const auto* definition = table.m_definition;
    const int32_t serving = table.m_WaiterServing;
    if (serving == -1) {
        env.PostWaiterAnimation(table, "d_stand_snooty", 1);
        table.m_waiterReady = 1;
        return;
    }
    float point[2]{};
    if (table.m_chairs[serving].m_waitingForWaiter) {
        ++table.m_WaiterPos;
        const int32_t count = definition->m_chairWalkPoints[serving][0];
        if (table.m_WaiterPos == count) {
            const int32_t direction = static_cast<int32_t>(static_cast<uint32_t>(definition->m_chairDirections[serving]) +
                                              (4u - static_cast<uint32_t>(table.m_rotation)) * 2u) %
                    8;
            env.SetWaiterDirection(table.m_WaiterID, direction);
            env.SetWaiterPosition(table.m_WaiterID, table.m_WorldXYZ);
            env.HideWaiter(table.m_WaiterID);
            table.m_drawWaiter = 1;
            env.PostWaiterAnimation(table, definition->m_waiterServeAnim.c_str(), 0);
            return;
        }
        const float* offset = table.m_WaiterPos == count - 1
                ? definition->m_servePoints[serving].data()
                : definition->m_walkPoints[definition->m_chairWalkPoints[serving][table.m_WaiterPos + 1]].data();
        AddRotatedOffset(point, table.m_WorldXYZ, offset, table.m_rotation);
        MoveWaiter(table, point, env);
        return;
    }
    --table.m_WaiterPos;
    if (table.m_WaiterPos != -1) {
        AddRotatedOffset(point, table.m_WorldXYZ,
                definition->m_walkPoints[definition->m_chairWalkPoints[serving][table.m_WaiterPos + 1]].data(), table.m_rotation);
        MoveWaiter(table, point, env);
        return;
    }
    for (uint32_t chair = 0; chair < definition->m_NumChairs; ++chair) {
        if (!table.m_chairs[chair].m_waitingForWaiter) continue;
        table.m_WaiterServing = static_cast<int32_t>(chair);
        table.m_WaiterPos = 0;
        AddRotatedOffset(
                point, table.m_WorldXYZ, definition->m_walkPoints[definition->m_chairWalkPoints[chair][1]].data(), table.m_rotation);
        env.StartWaiterAnim(table.m_WaiterID, definition->m_waiterWalkAnim.c_str());
        MoveWaiter(table, point, env);
        break;
    }
    // The selected sentinel is incremented twice (count+2), then count+1 is tested.
    // Even after selecting a waiting chair it sends a second move home and clears the serving index.
    env.WaiterSpawnPoint(table.m_ownerId, point);
    MoveWaiter(table, point, env);
    table.m_WaiterServing = -1;
    table.m_waiterReady = 0;
}

// MT_GUEST_SERVED (83), from the waiter that reached the chair it was serving: the chair stops waiting and its plate shows, the waiter is put at
// the chair's serve point (turned by the table's rotation), made visible and sent walking (MT_MOVE2SEAPOINT with the truncated point) with its
// walk animation, and the guest gets MT_FINISH_ACTION twenty seconds later.
template<class Env> void GuestServed(Furniture& table, Env& env)
{
    const tables::TableDef* definition = table.m_definition;
    const int32_t serving = table.m_WaiterServing;
    table.m_chairs[serving].m_waitingForWaiter = 0;
    table.m_pbDrawPlate[serving] = 1;
    table.m_WaiterPos = definition->m_chairWalkPoints[serving][0] - 1;
    float point[3] = {0, 0, 0};
    AddRotatedOffset(point, table.m_WorldXYZ, definition->m_servePoints[serving].data(), table.m_rotation);
    env.SetWaiterPosition(table.m_WaiterID, point);
    env.ShowWaiter(table.m_WaiterID);
    table.m_drawWaiter = 0;
    Message move{};
    move.sender = static_cast<int32_t>(table.m_id);
    move.receiver = table.m_WaiterID;
    move.type = messages::MT_MOVE2SEAPOINT;
    move.flags = FloatToIntWrap(point[0]);
    move.data = FloatToIntWrap(point[1]);
    env.Post(move);
    env.StartWaiterAnim(table.m_WaiterID, definition->m_waiterWalkAnim.c_str());
    Message finish{};
    finish.sender = static_cast<int32_t>(table.m_id);
    finish.receiver = table.m_chairs[table.m_WaiterServing].m_ChairOccupier;
    finish.type = messages::MT_FINISH_ACTION;
    finish.extra = 0x2e;
    env.PostDelayed(finish, kFinishActionDelay);
}

// ---- MT_NEXT_MOVE_POINT -----------------------------------------------------------------------------------------------------------------

constexpr const char* kFishBoatKey = "FISHBOAT1";
constexpr const char* kChairSitAnim = "chair_stand_sit";
constexpr float kFinishActionDelayFar = 30.0f;
constexpr int32_t kActionMarker = 0x2e; // extra of the guest messages (the receiver may read it)
constexpr int32_t kGuestStateLeaving = 0x1c;
// The table of that key is not placed by its own position: its points are fixed offsets from the building's, one set per rotation
// (the same constants in all three message blocks).
constexpr float kFishBoatOffsets[4][2] = {{263.0f, -408.0f}, {-148.0f, -522.0f}, {-107.0f, -265.0f}, {-5.0f, -154.0f}};

// `offset` is only called for an ordinary table (the other one never reads its definition). A rotation outside
// 0..3 leaves the point as it was.
template<class Env, class Offset> void GuestPoint(const Furniture& table, float point[2], Offset&& offset, Env& env)
{
    if (ascii::EqualsNoCase(table.m_definition->m_key, kFishBoatKey)) {
        float base[2];
        env.BuildingPosition(table.m_ownerId, base);
        if (table.m_rotation >= 0 && table.m_rotation <= 3) {
            point[0] = base[0] + kFishBoatOffsets[table.m_rotation][0];
            point[1] = base[1] + kFishBoatOffsets[table.m_rotation][1];
        }
        return;
    }
    const float* pair = offset();
    AddRotatedOffset(point, table.m_WorldXYZ, pair, table.m_rotation);
}

// The guest turns the way the chair faces, the chair's direction taken round by the table's rotation (two of the eight steps per quarter).
template<class Env> void FaceChair(const Furniture& table, uint32_t index, int32_t guest, Env& env)
{
    const int32_t direction = static_cast<int32_t>(static_cast<uint32_t>(table.m_definition->m_chairDirections[index]) +
                                      (4u - static_cast<uint32_t>(table.m_rotation)) * 2u) %
            8;
    env.SetGuestDirection(guest, direction);
}

template<class Env> void PostGuestMove(Furniture& table, int32_t guest, const float point[2], Env& env)
{
    Message move{};
    move.sender = static_cast<int32_t>(table.m_id);
    move.receiver = guest;
    move.type = messages::MT_MOVE2SEAPOINT;
    move.flags = FloatToIntWrap(point[0]);
    move.data = FloatToIntWrap(point[1]);
    move.extra = kActionMarker;
    env.Post(move);
}

// The guest reached the chair (its position is the walk list's length + 1): sit it down, or, at a serve table, hand it over to the building.
template<class Env> void GuestAtChair(Furniture& table, uint32_t index, int32_t guest, Env& env)
{
    const tables::TableDef* definition = table.m_definition;
    object::TableChair& chair = table.m_chairs[index];
    if (env.GuestActionState(guest) == 0) {
        if (definition->m_serveTable) {
            Message board{};
            board.sender = static_cast<int32_t>(table.m_id);
            board.receiver = table.m_ownerId;
            board.type = messages::MT_BOARD_VEHICLE;
            board.data = guest;
            env.Post(board);
            return;
        }
        if (!definition->m_hasPlates) {
            Message finish{};
            finish.sender = static_cast<int32_t>(table.m_id);
            finish.receiver = guest;
            finish.type = messages::MT_FINISH_ACTION;
            finish.extra = kActionMarker;
            env.PostDelayed(finish, kFinishActionDelayFar);
        }
        float point[3] = {0, 0, 0};
        GuestPoint(table, point, [&] { return definition->m_chairPositions[index].data(); }, env);
        env.SetGuestPosition(guest, point);
        FaceChair(table, index, guest, env);
        env.PostGuestAnimation(table, guest, definition->m_SitDownAnim.c_str());
        chair.m_ChairSeated = 1;
        chair.m_waitingForWaiter = 1;
        if (table.m_chairSprites) env.StartChairAnim(table, index, kChairSitAnim, 2);
        return;
    }
    if (definition->m_serveTable) return;
    if (definition->m_hasPlates) table.m_pbDrawPlate[index] = 0;
    env.PostGuestAnimation(table, guest, definition->m_GetUpAnim.c_str());
}

// The guest is on its way along the chair's walk list (position 1..count): the next point, or, going back, the previous one; leaving from the
// first one releases the chair.
template<class Env> void GuestOnWay(Furniture& table, uint32_t index, int32_t guest, Env& env)
{
    const tables::TableDef* definition = table.m_definition;
    object::TableChair& chair = table.m_chairs[index];
    float point[2]{};
    if (env.GuestActionState(guest) == 0) {
        GuestPoint(
                table, point, [&] { return definition->m_walkPoints[definition->m_chairWalkPoints[index][chair.m_position]].data(); }, env);
        PostGuestMove(table, guest, point, env);
        ++chair.m_position;
        return;
    }
    if (chair.m_position <= 1) {
        for (uint32_t other = 0; other < definition->m_NumChairs; ++other) {
            object::TableChair& seat = table.m_chairs[other];
            if (seat.m_ChairOccupier != guest) continue;
            env.ShowGuest(guest);
            seat.m_ChairOccupier = -1;
            seat.m_ChairSeated = 0;
            ++table.m_GuestsRemoved;
            break;
        }
        if (!table.m_serveTable) {
            Message quality{};
            quality.sender = static_cast<int32_t>(table.m_id);
            quality.receiver = table.m_ownerId;
            quality.type = messages::MT_COMMOD_QUALITY;
            env.Post(quality);
        }
        env.SetGuestActionState(guest, 0);
        Message state{};
        state.sender = static_cast<int32_t>(table.m_id);
        state.receiver = guest;
        state.type = messages::MT_STATECHANGE;
        state.data = kGuestStateLeaving;
        env.Post(state);
        chair.m_position = 0;
        return;
    }
    GuestPoint(
            table, point, [&] { return definition->m_walkPoints[definition->m_chairWalkPoints[index][chair.m_position - 1]].data(); }, env);
    --chair.m_position;
    PostGuestMove(table, guest, point, env);
}

// MT_NEXT_MOVE_POINT (100) from a guest walking to, sitting at or leaving a chair of this table; every chair it occupies is handled in turn
// (the loop re-reads the guest's action state each time) and the result is always true.
template<class Env> void NextMovePoint(Furniture& table, const Message& message, Env& env)
{
    const tables::TableDef* definition = table.m_definition;
    const int32_t guest = message.sender;
    for (uint32_t index = 0; index < definition->m_NumChairs; ++index) {
        const object::TableChair& chair = table.m_chairs[index];
        if (chair.m_ChairOccupier != guest) continue;
        if (chair.m_position == definition->m_chairWalkPoints[index][0] + 1)
            GuestAtChair(table, index, guest, env);
        else
            GuestOnWay(table, index, guest, env);
    }
}

// ---- MT_DO_NEXT_ACTION ------------------------------------------------------------------------------------------------------------------

constexpr const char* kChairGetUpAnim = "chair_sit_stand";
constexpr const char* kChairStandAnim = "chair";
constexpr const char* kChairEatAnim = "chair_eat";
constexpr const char* kLeaveChairAnim = "WalkAnimation"; // sent to the guest as the text of MT_PLAYANIMATION

// Whatever the guest does at a chair with nothing else to do: one of the definition's sit-down animations, drawn by rand.
template<class Env> void PlayIdleAnimation(Furniture& table, uint32_t index, int32_t guest, Env& env)
{
    const tables::TableDef* definition = table.m_definition;
    const int32_t count = static_cast<int32_t>(definition->m_NumSatDownAnims);
    if (count <= 0) return; // no animations: nothing to pick (and no division by zero)
    const int32_t pick = env.Rand() % count;
    FaceChair(table, index, guest, env);
    env.PostGuestAnimation(table, guest, definition->m_satDownAnims[pick].c_str());
}

// The guest has finished what it was doing at its chair (MT_DO_NEXT_ACTION from the guest; the result is always true). Per chair it occupies:
// state 2 (done eating): gets up, or at a serve table just moves on to state 3; state 3: leaves the chair and walks to the first point of its
// walk list, then asks the table for the next move; state 4 and serve tables: nothing; any other state: the guest calls the waiter (the table
// with plates, a waiting chair and an idle waiter), waits for it, or plays an idle animation.
template<class Env> void DoNextAction(Furniture& table, const Message& message, Env& env)
{
    const tables::TableDef* definition = table.m_definition;
    const int32_t guest = message.sender;
    for (uint32_t index = 0; index < definition->m_NumChairs; ++index) {
        object::TableChair& chair = table.m_chairs[index];
        if (chair.m_ChairOccupier != guest) continue;
        const int32_t state = env.GuestActionState(guest);
        if (state == 2) {
            if (definition->m_serveTable) {
                env.SetGuestActionState(guest, 3);
                continue;
            }
            if (definition->m_hasPlates) table.m_pbDrawPlate[index] = 0;
            FaceChair(table, index, guest, env);
            env.PostGuestAnimation(table, guest, definition->m_GetUpAnim.c_str());
            if (table.m_chairSprites) env.StartChairAnim(table, index, kChairGetUpAnim, 2);
            continue;
        }
        if (state == 3) {
            float point[3] = {0, 0, 0};
            GuestPoint(
                    table, point,
                    [&] { return definition->m_walkPoints[definition->m_chairWalkPoints[index][chair.m_position - 1]].data(); }, env);
            env.SetGuestPosition(guest, point);
            env.PostGuestAnimation(table, guest, kLeaveChairAnim);
            --chair.m_position;
            chair.m_ChairSeated = 0;
            if (table.m_chairSprites) env.StartChairAnim(table, index, kChairStandAnim, 0);
            Message next{};
            next.sender = guest;
            next.receiver = static_cast<int32_t>(table.m_id);
            next.type = messages::MT_NEXT_MOVE_POINT;
            next.extra = kActionMarker;
            env.Post(next);
            continue;
        }
        if (state == 4 || definition->m_serveTable) continue;
        if (definition->m_hasPlates && chair.m_waitingForWaiter) {
            if (table.m_WaiterServing == -1 && table.m_waiterReady) {
                table.m_WaiterServing = static_cast<int32_t>(index);
                table.m_WaiterPos = 0;
                FaceChair(table, index, guest, env);
                env.PostGuestAnimation(table, guest, definition->m_callWaiterAnim.c_str());
                float waiterPoint[2]{};
                AddRotatedOffset(waiterPoint, table.m_WorldXYZ, definition->m_walkPoints[definition->m_chairWalkPoints[index][1]].data(),
                        table.m_rotation);
                env.StartWaiterAnim(table.m_WaiterID, definition->m_waiterWalkAnim.c_str());
                MoveWaiter(table, waiterPoint, env);
            } else {
                FaceChair(table, index, guest, env);
                env.PostGuestAnimation(table, guest, definition->m_waitWaiterAnim.c_str());
            }
        } else {
            PlayIdleAnimation(table, index, guest, env);
        }
        if (table.m_chairSprites) env.StartChairAnim(table, index, kChairEatAnim, 0);
    }
}

template<class Env> bool ReceiveMessage(Furniture& table, const Message& message, Env& env)
{
    switch (message.type) {
    case messages::MT_MOVEIMHERE: MoveImHere(table, env); return true;
    case messages::MT_NEXT_MOVE_POINT: NextMovePoint(table, message, env); return true;
    case messages::MT_DO_NEXT_ACTION: DoNextAction(table, message, env); return true;
    case messages::MT_IM_READY: // the waiter announces itself
        table.m_WaiterID = message.sender;
        return true;
    case messages::MT_CONSTRUCT:
    case messages::MT_NEED_REPAIR:
    case messages::MT_CONSTRUCTION_FINISHED: return true;
    case messages::MT_GUEST_SERVED: GuestServed(table, env); return true;
    default: return env.BaseReceive(table, message);
    }
}

} // namespace obl::table
