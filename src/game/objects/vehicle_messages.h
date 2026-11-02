#pragma once

#include "core/float_to_int.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_vehicle.h"
#include "game/map/path_data.h"
#include "game/objects/vehicle_lifecycle.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// The messages and states of Vehicle. ReceiveMessage (the message),
// OnExitState (no arguments) and OnEnterState (the new state). Pure logic over an `Env`; the game side is vehicle_messages.cpp.
//
// Env:
//   bool BaseReceive(Vehicle&, const Message&)            the moving object's ReceiveMessage
//   void Post(const Message&) / void PostDelayed(const Message&, float delay)   Post / PostDelayed on g_MessageHandler
//   float WorldTime                                           world game time
//   void NeighbourCells(uint32_t x, uint32_t y, GridCell* out[8])   the eight neighbouring cells
//   void TurnToDirection(Vehicle&, int32_t direction)     the vehicle's TurnToDirection
//   void MoveToNextCircuitPoint(Vehicle&) (jet ski and speed boat: the next free point of the world's circuit)
//   void PickRoamPoint(Vehicle&) (pedal boat)
//   bool PickFishingPoint(Vehicle&)
//   float PlayCatchAnimation(Vehicle&), the length of the rod animation
//   float PlayBoardingAnimation(Vehicle&, float startTime, bool boarding, bool pair), the length of the animation
//   void TeleportGuest(int32_t guest, const float position[3])  the guest's Teleport, the position by value
//   void SetGuestPosition(int32_t guest, float position[3])     calls the guest's SetPosition with the position pointer
//   void OwnerServePosition(int32_t owner, float out[3])        the serve position of the owner building
//   void PlaySound(const char* name, const float position[3])   plays the named positional sound at full volume with flags 1 and no owner
//   void SetAnimType(void* sprite, int32_t type)                SpriteInstance::m_animType
//   float StartAnim(void* sprite, const char* anim, float startTime, const float* world)   starting the named sprite animation, the length
// OnEnterState adds:
//   void PlayIdleAnimation(Vehicle&)        void PlayRideAnimation(Vehicle&, float startTime)
//   void MoveToTurnArea(Vehicle&)        void TurnTowardsPoint(Vehicle&, float x, float y)
//   void BlockDepartLane / UnblockDepartLane / BlockReturnLane / UnblockReturnLane(uint32_t x, uint32_t y, int32_t direction)
// (all four are free functions)
//   uint32_t CircuitPointCount(int32_t type) / void* CircuitPoints(int32_t type)   the world's circuit point count / points
//   void TilePosition(const Vehicle&, uint32_t out[2])    the object's tile position
//   void ObjectTile(const Vehicle&, uint32_t out[2])      world-to-tile conversion of the object's position (as in vehicle_lifecycle.h)
//   void WorldPointToTile(float x, float y, uint32_t out[2])    world-to-tile conversion, z = 0
//   void TileToPathPoint(uint32_t x, uint32_t y, float out[3])  the tile path point      void TileToWorldPoint(...)   tile-to-world conversion, kind 4
//   void Neighbour(uint32_t x, uint32_t y, int32_t direction, uint32_t out[2])   the neighbouring tile
//   GridCell* CellAt(uint32_t x, uint32_t y)                    the wrapped tile index into the cell array
//   GridCell* WrappedCell(uint32_t x, uint32_t y)               the inline index (y % height) * width + x % width
//   void SetDirection(void* sprite, int32_t direction)          SpriteInstance::m_direction, clamped to the animation's direction count
//   void StartLoop(const char* name, int32_t id) / void StopLoop(int32_t id)   starts a full-volume object sound loop or stops the object's loops
//   void SetMoving(Vehicle&, bool)                        the moving object's SetMoving
//   bool HasPathCursor(const Vehicle&)                    m_pathCursor, asked after a Post (the move order is handled inside it)
//   bool PathCursorPoint(const Vehicle&, float out[3])    the world point of the path cursor's current step; false without a cursor
//   int32_t FreeRooms                                         capacity - guests over every HOTEL and every ACCOMBLK without a build or demolish job
//   void NoRoomsTicker                                        feedback::Feedback::AddKey(TICKER_MSG_NO_ROOMS, -2, 1)
//   void TurnGuestAround(int32_t guest)                         velocity and steer force zero, sprite direction + 4 (mod 8, clamped)
//   void MarkGuestForRemoval(int32_t guest)                     Entity::m_waitingForDeath = 1
//   float StartGuestAnim(int32_t guest, const char* anim, float startTime, const float* world)   SetAnimByName on the guest's sprite
namespace obl::vehicle::messages {

using lifecycle::kBlockedFlag;
using object::Vehicle;
using obl::messages::Message;

// Vehicle states (Entity::m_state); HasState accepts 1 and 0x41..0x4a. Names follow what OnEnterState does in each.
enum State : int32_t {
    StateCreated = 1,
    StateIdle = 0x41, // at the stop, waiting for guests
    StateReturn = 0x42, // heading for the unload point
    StateUnload = 0x43, // guests leave; leaving the state tells the owner MT_GUEST_SERVED
    StateDepart = 0x44, // the ride starts
    StateRide = 0x45, // roaming / fishing / riding until m_rideTime
    StateTurn = 0x46,
    StateWait = 0x47, // the coach idles with its engine running
    StateRemove = 0x48, // the destroy point was reached: the passengers are removed
    StateFall = 0x49, // the driver falls off the jet ski
    StateRemount = 0x4a, // ... and is back on it: the state number goes straight back to 0x44
};

// Every message sets five of its eight words; the other three (extra, floatData, time) are zero, and Post writes time itself.
inline Message Make(int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data)
{
    Message message{};
    message.sender = sender;
    message.receiver = receiver;
    message.type = type;
    message.flags = flags;
    message.data = data;
    return message;
}
namespace detail {

inline uint8_t LowByte(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<uint8_t>(bits);
}

// (current + 1) % count as a signed division; callers never pass a zero count (it would fault).
inline uint32_t NextWayPoint(const Vehicle& vehicle)
{
    return static_cast<uint32_t>((static_cast<int32_t>(vehicle.m_currentWayPoint) + 1) % static_cast<int32_t>(vehicle.m_numWayPts));
}

template<class Env> void SetBlocked(Vehicle& vehicle, Env& env, bool blocked)
{
    map::GridCell* around[8];
    env.NeighbourCells(vehicle.m_CurrGridXY[0], vehicle.m_CurrGridXY[1], around);
    for (map::GridCell* cell : around) {
        if (blocked)
            cell->m_flags |= kBlockedFlag;
        else
            cell->m_flags &= ~kBlockedFlag;
    }
}

// MT_MOVEIMHERE: the vehicle reached the point it was sent to.
template<class Env> void Arrived(Vehicle& vehicle, Env& env)
{
    const int32_t type = vehicle.m_vehicleType;
    switch (vehicle.m_state) {
    case StateDepart: {
        if (vehicle.OnCircuit()) {
            env.MoveToNextCircuitPoint(vehicle);
            return;
        }
        const uint32_t next = NextWayPoint(vehicle);
        vehicle.m_currentWayPoint = next;
        if (next == vehicle.m_UnloadPoint) {
            env.Post(vehicle.StateChange(StateReturn));
        } else if (next == vehicle.m_DestroyPoint) {
            env.Post(vehicle.StateChange(StateRemove));
        } else {
            const bool direct = type == lifecycle::Coach || type == lifecycle::Ferry;
            env.Post(vehicle.MoveTo(direct ? obl::messages::MT_MOVE_DIRECT_2POINT : obl::messages::MT_SWIM2SEAPOINT, next));
        }
        return;
    }
    case StateReturn:
        if (vehicle.m_moveToTurnPoint) {
            // The fishing boat leaves its turn area for the unload point: its own cells must not block the path it asks for.
            SetBlocked(vehicle, env, false);
            env.Post(vehicle.MoveTo(obl::messages::MT_MOVE2SEAPOINT, vehicle.m_UnloadPoint));
            SetBlocked(vehicle, env, true);
            vehicle.m_moveToTurnPoint = 0;
            env.TurnToDirection(vehicle, vehicle.m_unloadDir);
        } else if (type == lifecycle::Pedaloe || vehicle.OnCircuit()) {
            env.Post(vehicle.StateChange(StateTurn));
        } else {
            env.Post(vehicle.StateChange(StateUnload));
            vehicle.m_currentWayPoint = NextWayPoint(vehicle);
        }
        return;
    case StateRide:
        if (type == lifecycle::Pedaloe) env.PickRoamPoint(vehicle);
        if (vehicle.m_vehicleType == lifecycle::FishBoat) {
            if (!vehicle.m_fishing && env.PickFishingPoint(vehicle)) {
                vehicle.m_fishing = 1;
                return;
            }
            // Twice the catch animation (the float result doubled, stored as a float).
            const float delay = static_cast<float>(static_cast<double>(env.PlayCatchAnimation(vehicle)) * 2.0);
            env.PostDelayed(vehicle.StateChange(StateReturn), delay);
            vehicle.m_fishing = 0;
            return;
        }
        if (env.WorldTime() > vehicle.m_rideTime) env.Post(vehicle.StateChange(StateReturn));
        return;
    default: return;
    }
}

// The boarding or leaving animation of one guest and the delayed order to that guest; the result is the animation's length.
template<class Env> float Board(Vehicle& vehicle, Env& env, int32_t guest, bool pair)
{
    const float delay = env.PlayBoardingAnimation(vehicle, env.WorldTime(), true, pair);
    env.PostDelayed(Make(static_cast<int32_t>(vehicle.m_id), guest, obl::messages::MT_BOARD_VEHICLE, 0, 0), delay);
    return delay;
}

// After a guest left: the stop state when the vehicle is empty, else the next guest is told to leave once its animation ended
// (half a second later for the vehicles that seat one or two guests).
template<class Env> void NextLeaves(Vehicle& vehicle, Env& env, bool extraHalfSecond)
{
    if (vehicle.m_currCap == 0) {
        env.Post(vehicle.StateChange(StateIdle));
        return;
    }
    const float length = env.PlayBoardingAnimation(vehicle, env.WorldTime(), false, static_cast<uint32_t>(vehicle.m_currCap) > 1);
    const float delay = extraHalfSecond ? static_cast<float>(static_cast<double>(length) + 0.5) : length;
    env.PostDelayed(
            Make(static_cast<int32_t>(vehicle.m_id), vehicle.m_Guests[vehicle.m_currCap - 1], obl::messages::MT_LEAVE_VEHICLE, 0, 0),
            delay);
}

} // namespace detail

// OnExitState: leaving the unload state tells the owner that its guests are served. Always true.
template<class Env> bool OnExitState(Vehicle& vehicle, Env& env)
{
    if (vehicle.m_state == StateUnload)
        env.Post(Make(static_cast<int32_t>(vehicle.m_id), vehicle.m_ownerId, obl::messages::MT_GUEST_SERVED, 0, 0));
    return true;
}

// ReceiveMessage. The result byte: 1 for MT_OBJECTCREATED, the base's answer for a message the class
// does not handle, and otherwise the low byte of the argument slot, which is the message pointer unless the branch parked the length of an
// animation there (the boarding branches and the coach doors). No caller is known to rely on it; it is kept so that both paths answer alike.
// `untouched` is that low byte of the caller's message pointer.
template<class Env> uint8_t ReceiveMessage(Vehicle& vehicle, const Message& message, uint8_t untouched, Env& env)
{
    using namespace detail;
    const int32_t id = static_cast<int32_t>(vehicle.m_id);
    switch (message.type) {
    case obl::messages::MT_OBJECTCREATED: return 1;
    case obl::messages::MT_VEHICLE_STOP_TURN:
        if (!vehicle.OnCircuit()) env.TurnToDirection(vehicle, message.flags);
        return untouched;
    case obl::messages::MT_VEHICLE_SMOOTH_TURN: return untouched;
    case obl::messages::MT_MOVEIMHERE: Arrived(vehicle, env); return untouched;
    case obl::messages::MT_SERVE_GUEST: {
        if (vehicle.CarriesCrowd()) {
            vehicle.m_guestsToWaitFor += message.data;
            return untouched;
        }
        vehicle.m_Guests[vehicle.m_currCap] = message.data;
        ++vehicle.m_currCap;
        if (vehicle.m_waitForGuestToBoard) return untouched;
        const float delay = Board(vehicle, env, message.data, static_cast<uint32_t>(vehicle.m_currCap) > 1);
        vehicle.m_waitForGuestToBoard = 1;
        vehicle.m_rideTime = static_cast<float>(static_cast<double>(env.WorldTime()) + static_cast<double>(delay));
        return LowByte(delay);
    }
    case obl::messages::MT_COMMOD_QUALITY: env.Post(Make(id, vehicle.m_ownerId, obl::messages::MT_GUEST_SERVED, 0, 0)); return untouched;
    case obl::messages::MT_GUEST_ENTER_VEHICLE: {
        if (!vehicle.CarriesCrowd()) {
            if (vehicle.m_waitForGuestToBoard && static_cast<uint32_t>(vehicle.m_currCap) > 1) {
                // The second guest of a pair boards after the first.
                const float delay = Board(vehicle, env, vehicle.m_Guests[1], true);
                vehicle.m_waitForGuestToBoard = 0;
                vehicle.m_rideTime = static_cast<float>(static_cast<double>(env.WorldTime()) + static_cast<double>(delay));
                return LowByte(delay);
            }
            vehicle.m_waitForGuestToBoard = 0;
            vehicle.m_rideTime = env.WorldTime();
            return untouched;
        }
        vehicle.m_Guests[vehicle.m_currCap] = message.sender;
        ++vehicle.m_currCap;
        const float delay = env.PlayBoardingAnimation(vehicle, env.WorldTime(), true, true);
        vehicle.m_rideTime = env.WorldTime();
        if (vehicle.m_currCap == vehicle.m_guestsToWaitFor) env.PostDelayed(Make(id, id, obl::messages::MT_PLAYANIMATION, 0, 0), delay);
        return LowByte(delay);
    }
    case obl::messages::MT_GUEST_LEFT_VEHICLE: {
        vehicle.m_rideTime = env.WorldTime();
        --vehicle.m_currCap;
        const int32_t type = vehicle.m_vehicleType;
        if (type == lifecycle::Ferry) {
            NextLeaves(vehicle, env, false);
        } else if (type == lifecycle::Coach) {
            // The guest steps out beside the doors, which side depends on where the coach faces.
            float position[3] = {vehicle.m_WorldXYZ[0], vehicle.m_WorldXYZ[1], vehicle.m_WorldXYZ[2]};
            if (vehicle.m_unloadDir == 7) {
                position[0] -= 63.0f;
                position[1] += 14.0f;
            } else if (vehicle.m_unloadDir == 5) {
                position[0] += 22.0f;
                position[1] += 72.0f;
            }
            env.SetGuestPosition(message.sender, position);
            NextLeaves(vehicle, env, false);
        } else {
            float position[3];
            env.OwnerServePosition(vehicle.m_ownerId, position);
            env.TeleportGuest(vehicle.m_Guests[vehicle.m_currCap], position);
            NextLeaves(vehicle, env, true);
            if (vehicle.OnCircuit()) {
                vehicle.m_vVelocity[0] = vehicle.m_vVelocity[1] = 0;
                vehicle.m_vSteerForce[0] = vehicle.m_vSteerForce[1] = 0;
            }
        }
        env.Post(Make(message.sender, vehicle.m_ownerId, obl::messages::MT_GUEST_LEFT_VEHICLE, 0, id));
        return untouched;
    }
    case obl::messages::MT_PLAYANIMATION: {
        const Message depart = vehicle.StateChange(StateDepart);
        if (vehicle.m_vehicleType != lifecycle::Coach) {
            env.Post(depart);
            return untouched;
        }
        if (vehicle.m_playingDoorAnim) return untouched;
        // The coach closes its doors first and departs when the animation ends.
        const float position[3] = {vehicle.m_WorldXYZ[0], vehicle.m_WorldXYZ[1], 0.0f};
        env.PlaySound("doorhissopen", position);
        vehicle.m_playingDoorAnim = 1;
        env.SetAnimType(vehicle.m_doorSprite, 1);
        const float length = env.StartAnim(vehicle.m_doorSprite, "coach_doors", env.WorldTime(), vehicle.m_WorldXYZ);
        env.PostDelayed(depart, length);
        return LowByte(length);
    }
    default: return env.BaseReceive(vehicle, message) ? 1 : 0;
    }
}

namespace detail {

inline void Block(map::GridCell* cell)
{
    cell->m_flags |= kBlockedFlag;
}
inline void Unblock(map::GridCell* cell)
{
    cell->m_flags &= ~kBlockedFlag;
}

// The dock cell of a circuit vehicle and the two neighbours its unload direction keeps (the same cells the destructor gives back).
template<class Env, class Change> void DockCells(Env& env, const uint32_t tile[2], int32_t direction, Change&& change)
{
    map::GridCell* around[8];
    env.NeighbourCells(tile[0], tile[1], around);
    change(env.CellAt(tile[0], tile[1]));
    if (direction == 1 || direction == 5) {
        change(around[0]);
        change(around[4]);
    } else if (direction == 3 || direction == 7) {
        change(around[2]);
        change(around[6]);
    }
}

// m_CurrentRoamPoint: the tile the path cursor stands on, or the unload point when there is no path. Copied as raw words.
template<class Env> void SetRoamPoint(Vehicle& vehicle, Env& env)
{
    float point[3];
    if (env.PathCursorPoint(vehicle, point))
        std::memcpy(vehicle.m_CurrentRoamPoint, point, sizeof(point));
    else
        std::memcpy(vehicle.m_CurrentRoamPoint, vehicle.m_WayPoints[vehicle.m_UnloadPoint], sizeof(point));
}

// A sound at the vehicle, on the ground (z = 0).
template<class Env> void SoundHere(const Vehicle& vehicle, Env& env, const char* name)
{
    const float position[3] = {vehicle.m_WorldXYZ[0], vehicle.m_WorldXYZ[1], 0.0f};
    env.PlaySound(name, position);
}

inline const uint32_t (*Circuit(const Vehicle& vehicle))[2]
{
    return static_cast<const uint32_t (*)[2]>(vehicle.m_circuitPoints);
}

// State 1. A circuit vehicle takes the world's circuit of its type, starts at the circuit point nearest to it and unloads at the one before.
template<class Env> void EnterCreated(Vehicle& vehicle, Env& env)
{
    const int32_t type = vehicle.m_vehicleType;
    if (vehicle.OnCircuit()) {
        float best = path::ApproxSqrt(83886080.0f); // Initial search distance from the squared-distance limit
        vehicle.m_circuitPointCount = env.CircuitPointCount(type);
        vehicle.m_circuitPoints = env.CircuitPoints(vehicle.m_vehicleType);
        const auto* points = Circuit(vehicle);
        for (int32_t i = 0; i < static_cast<int32_t>(vehicle.m_circuitPointCount); ++i) {
            uint32_t tile[2];
            float here[3], there[3];
            env.TilePosition(vehicle, tile);
            env.TileToPathPoint(tile[0], tile[1], here);
            env.TileToPathPoint(points[i][0], points[i][1], there);
            // The differences and products stay doubles, the sum is stored as a float before the table square root.
            const double dx = static_cast<double>(there[0]) - static_cast<double>(here[0]);
            const double dy = static_cast<double>(there[1]) - static_cast<double>(here[1]);
            const float distance = path::ApproxSqrt(static_cast<float>(dx * dx + dy * dy));
            if (!(distance >= best)) {
                best = distance;
                vehicle.m_startCircuitPoint = static_cast<uint32_t>(i);
            }
        }
        // No bound check: a start at point 0 reads the entry before the table (tests pin it).
        const int32_t stop = static_cast<int32_t>(vehicle.m_startCircuitPoint) - 1;
        vehicle.m_UnloadPoint = 0;
        vehicle.m_stopCircuitPoint = static_cast<uint32_t>(stop);
        float point[3];
        env.TileToPathPoint(points[stop][0], points[stop][1], point);
        lifecycle::detail::CopyBits(vehicle.m_WayPoints[vehicle.m_UnloadPoint][0], point[0]);
        env.TileToPathPoint(points[stop][0], points[stop][1], point);
        lifecycle::detail::CopyBits(vehicle.m_WayPoints[vehicle.m_UnloadPoint][1], point[1]);
        env.SetDirection(vehicle.m_sprite, vehicle.m_unloadDir);
    }
    if (vehicle.m_vehicleType == lifecycle::FishBoat) {
        // The turn area: three tiles to the left of the unload direction.
        uint32_t tile[2];
        env.TilePosition(vehicle, tile);
        for (int step = 0; step < 3; ++step) {
            uint32_t next[2];
            env.Neighbour(tile[0], tile[1], (vehicle.m_unloadDir + 7) % 8, next);
            tile[0] = next[0];
            tile[1] = next[1];
        }
        vehicle.m_TurnArea[0] = tile[0];
        vehicle.m_TurnArea[1] = tile[1];
    }
    env.Post(Make(static_cast<int32_t>(vehicle.m_id), vehicle.m_ownerId, obl::messages::MT_VEHICLE_CREATED, 0, 0));
    env.Post(vehicle.StateChange(StateIdle));
}

// State 0x42: back to the unload point.
template<class Env> void EnterReturn(Vehicle& vehicle, Env& env)
{
    const int32_t id = static_cast<int32_t>(vehicle.m_id);
    if (vehicle.m_vehicleType == lifecycle::FishBoat) {
        // First to the turn area; MT_MOVEIMHERE continues from there (m_moveToTurnPoint).
        SetBlocked(vehicle, env, false);
        float target[3];
        env.TileToWorldPoint(vehicle.m_TurnArea[0], vehicle.m_TurnArea[1], target);
        env.Post(Make(id, id, obl::messages::MT_MOVE2SEAPOINT, FloatToIntWrap(target[0]), FloatToIntWrap(target[1])));
        const double dx = static_cast<double>(vehicle.m_WorldXYZ[0]) - static_cast<double>(target[0]);
        const double dy = static_cast<double>(vehicle.m_WorldXYZ[1]) - static_cast<double>(target[1]);
        const double dz = static_cast<double>(vehicle.m_WorldXYZ[2]) - static_cast<double>(target[2]);
        if (std::sqrt(dz * dz + dy * dy + dx * dx) >= 5.0) env.TurnTowardsPoint(vehicle, target[0], target[1]);
        vehicle.m_moveToTurnPoint = 1;
        SetBlocked(vehicle, env, true);
    } else if (vehicle.CarriesCrowd()) {
        env.Post(vehicle.MoveTo(obl::messages::MT_MOVE_DIRECT_2POINT, vehicle.m_UnloadPoint));
    } else {
        // The unload cell is the vehicle's own: it must not block the path asked for, and the lane beside it must.
        const float* unload = vehicle.m_WayPoints[vehicle.m_UnloadPoint];
        uint32_t tile[2];
        env.WorldPointToTile(unload[0], unload[1], tile);
        map::GridCell* cell = env.WrappedCell(tile[0], tile[1]);
        if (vehicle.OnCircuit()) env.BlockReturnLane(tile[0], tile[1], vehicle.m_unloadDir);
        if (vehicle.m_vehicleType == lifecycle::Pedaloe) {
            Unblock(cell);
            env.Post(vehicle.MoveTo(obl::messages::MT_SEAVEHICLE_MOVE2POINT, vehicle.m_UnloadPoint));
        } else {
            env.Post(vehicle.MoveTo(obl::messages::MT_SWIM2SEAPOINT, vehicle.m_UnloadPoint));
        }
        if (vehicle.OnCircuit()) env.UnblockReturnLane(tile[0], tile[1], vehicle.m_unloadDir);
        Block(cell);
    }
    const int32_t type = vehicle.m_vehicleType;
    if (type == lifecycle::Pedaloe || type == lifecycle::Jetski) {
        SetRoamPoint(vehicle, env);
        if (vehicle.m_vehicleType != lifecycle::Jetski)
            env.TurnTowardsPoint(vehicle, vehicle.m_CurrentRoamPoint[0], vehicle.m_CurrentRoamPoint[1]);
    }
}

// State 0x43: the guests leave, last in first out.
template<class Env> void EnterUnload(Vehicle& vehicle, Env& env)
{
    const int32_t id = static_cast<int32_t>(vehicle.m_id);
    if (vehicle.CarriesCrowd()) {
        if (env.FreeRooms() == 0) env.NoRoomsTicker();
        if (vehicle.m_vehicleType == lifecycle::Ferry) SoundHere(vehicle, env, "ferryhoot");
    }
    if (vehicle.m_vehicleType == lifecycle::Coach) {
        env.SetDirection(vehicle.m_sprite, vehicle.m_unloadDir);
        env.StopLoop(id);
        SoundHere(vehicle, env, "doorhissopen");
        env.SetAnimType(vehicle.m_doorSprite, 2);
        env.StartAnim(vehicle.m_doorSprite, "coach_doors", env.WorldTime(), vehicle.m_WorldXYZ);
    }
    if (vehicle.OnCircuit()) env.StopLoop(id);
    vehicle.m_rideTime = env.WorldTime();
    if (vehicle.m_currCap == 0) {
        env.Post(vehicle.StateChange(StateIdle));
        return;
    }
    env.TurnGuestAround(vehicle.m_Guests[vehicle.m_currCap - 1]);
    const float delay = env.PlayBoardingAnimation(vehicle, env.WorldTime(), false, static_cast<uint32_t>(vehicle.m_currCap) > 1);
    env.PostDelayed(Make(id, vehicle.m_Guests[vehicle.m_currCap - 1], obl::messages::MT_LEAVE_VEHICLE, 0, 0), delay);
}

// State 0x44: the ride starts.
template<class Env> void EnterDepart(Vehicle& vehicle, Env& env)
{
    const int32_t id = static_cast<int32_t>(vehicle.m_id);
    const int32_t type = vehicle.m_vehicleType;
    if (type == lifecycle::Ferry || type == lifecycle::Coach) {
        if (type == lifecycle::Coach) {
            env.SetDirection(vehicle.m_sprite, vehicle.m_unloadDir);
            SoundHere(vehicle, env, "coachrev");
            env.StartLoop("coachloop", id);
        }
        env.Post(vehicle.MoveTo(obl::messages::MT_MOVE_DIRECT_2POINT, vehicle.m_currentWayPoint));
        return;
    }
    env.PlayRideAnimation(vehicle, env.WorldTime());
    const uint8_t moveType = vehicle.m_definition->m_MoveType;
    if (moveType == 1) {
        // POINTS: out to the start of the circuit. The vehicle's own dock cells are freed for the path it asks for, the lane beside the
        // dock is closed while the path is found.
        const float* unload = vehicle.m_WayPoints[vehicle.m_UnloadPoint];
        uint32_t tile[2];
        env.WorldPointToTile(unload[0], unload[1], tile);
        if (vehicle.m_vehicleType == lifecycle::Jetski) env.StartLoop("newjetskirev", id);
        if (vehicle.m_vehicleType == lifecycle::SpeedBoat) env.StartLoop("newjetskirevlow", id);
        if (vehicle.OnCircuit()) DockCells(env, tile, vehicle.m_unloadDir, Unblock);
        env.BlockDepartLane(tile[0], tile[1], vehicle.m_unloadDir);
        map::GridCell* cell = env.WrappedCell(tile[0], tile[1]);
        Unblock(cell);
        const uint32_t* start = Circuit(vehicle)[vehicle.m_startCircuitPoint];
        float point[3];
        env.TileToPathPoint(start[0], start[1], point);
        const Message move = Make(id, id, obl::messages::MT_SWIM2SEAPOINT, FloatToIntWrap(point[0]), FloatToIntWrap(point[1]));
        vehicle.m_currCircuitPoint = vehicle.m_startCircuitPoint;
        env.Post(move);
        // The move order is handled inside the Post: no path cursor afterwards means no way out, so the guests are unloaded again.
        if (!env.HasPathCursor(vehicle)) {
            env.SetMoving(vehicle, false);
            env.Post(vehicle.StateChange(StateUnload));
        }
        if (vehicle.m_vehicleType == lifecycle::Pedaloe) Block(cell);
        env.UnblockDepartLane(tile[0], tile[1], vehicle.m_unloadDir);
        if (vehicle.m_vehicleType == lifecycle::FishBoat && vehicle.m_gotGrid) SetBlocked(vehicle, env, true);
    } else if (moveType == 2) {
        env.Post(vehicle.MoveTo(obl::messages::MT_MOVE2SEAPOINT, vehicle.m_currentWayPoint));
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(vehicle.m_currCap); ++i) env.TeleportGuest(vehicle.m_Guests[i], vehicle.m_WorldXYZ);
    SetRoamPoint(vehicle, env);
    if (vehicle.m_vehicleType == lifecycle::Pedaloe)
        env.TurnTowardsPoint(vehicle, vehicle.m_CurrentRoamPoint[0], vehicle.m_CurrentRoamPoint[1]);
}

} // namespace detail

// OnEnterState (the state just set; reached through the base's state change). Always true.
template<class Env> bool OnEnterState(Vehicle& vehicle, uint32_t state, Env& env)
{
    using namespace detail;
    const int32_t id = static_cast<int32_t>(vehicle.m_id);
    switch (state) {
    case StateCreated: EnterCreated(vehicle, env); break;
    case StateIdle:
        env.PlayIdleAnimation(vehicle);
        vehicle.m_playingDoorAnim = 0;
        if (vehicle.OnCircuit()) {
            // Back at the dock: its cells are the vehicle's again.
            const int32_t direction = vehicle.m_unloadDir;
            uint32_t tile[2];
            env.ObjectTile(vehicle, tile);
            DockCells(env, tile, direction, Block);
        }
        break;
    case StateReturn: EnterReturn(vehicle, env); break;
    case StateUnload: EnterUnload(vehicle, env); break;
    case StateDepart: EnterDepart(vehicle, env); break;
    case StateRide: {
        if (vehicle.m_vehicleType == lifecycle::Pedaloe) env.PickRoamPoint(vehicle);
        if (vehicle.m_vehicleType == lifecycle::FishBoat) env.MoveToTurnArea(vehicle);
        vehicle.m_UnloadPoint = 0;
        const double length = vehicle.m_vehicleType == lifecycle::FishBoat ? 7.0 : 30.0;
        vehicle.m_rideTime = static_cast<float>(static_cast<double>(env.WorldTime()) + length);
        break;
    }
    case StateTurn:
        vehicle.m_vVelocity[0] = vehicle.m_vVelocity[1] = -0.5f;
        env.TurnToDirection(vehicle, vehicle.m_unloadDir);
        break;
    case StateWait:
        if (vehicle.m_vehicleType == lifecycle::Coach) env.StartLoop("coachloop", id);
        break;
    case StateRemove:
        for (uint32_t i = 0; i < static_cast<uint32_t>(vehicle.m_currCap); ++i) env.MarkGuestForRemoval(vehicle.m_Guests[i]);
        if (vehicle.m_vehicleType == lifecycle::Coach) env.StopLoop(id);
        break;
    case StateFall: {
        // The driver falls off the jet ski; the ride goes on when the animation ends.
        env.StartAnim(vehicle.m_sprite, "d_jetski_fall_off", env.WorldTime(), vehicle.m_WorldXYZ);
        env.TeleportGuest(vehicle.m_Guests[0], vehicle.m_WorldXYZ);
        const float length = env.StartGuestAnim(vehicle.m_Guests[0], "d_jetski_fall_off", env.WorldTime(), vehicle.m_WorldXYZ);
        env.PostDelayed(vehicle.StateChange(StateRemount), length);
        break;
    }
    case StateRemount:
        env.PlayRideAnimation(vehicle, env.WorldTime());
        vehicle.m_state = StateDepart;
        break;
    default: break;
    }
    return true;
}

} // namespace obl::vehicle::messages

namespace obl::object {

inline obl::messages::Message Vehicle::StateChange(int32_t state) const
{
    const int32_t id = static_cast<int32_t>(m_id);
    return vehicle::messages::Make(id, id, obl::messages::MT_STATECHANGE, 0, state);
}

// A move order to a way point: the x and y of the point truncated into flags and data.
inline obl::messages::Message Vehicle::MoveTo(int32_t type, uint32_t wayPoint) const
{
    const float* point = m_WayPoints[static_cast<int32_t>(wayPoint)];
    const int32_t id = static_cast<int32_t>(m_id);
    return vehicle::messages::Make(id, id, type, FloatToIntWrap(point[0]), FloatToIntWrap(point[1]));
}

inline bool Vehicle::OnCircuit() const
{
    return m_vehicleType == vehicle::lifecycle::Jetski || m_vehicleType == vehicle::lifecycle::SpeedBoat;
}

inline bool Vehicle::CarriesCrowd() const
{
    return m_vehicleType == vehicle::lifecycle::Ferry || m_vehicleType == vehicle::lifecycle::Coach;
}

} // namespace obl::object
