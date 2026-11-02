#pragma once

#include "core/vec2.h"
#include "game/objects/vehicle_messages.h"

namespace obl::vehicle::helpers {

// The sum is not rounded to float; an unordered comparison also enters the step.
template<class Env> void UpdateTurnAnimation(object::Vehicle& vehicle, float now, Env& env)
{
    if (static_cast<double>(vehicle.m_turnAnimLength) + vehicle.m_turnAnimStartTime > now) return;
    int32_t direction = env.Direction(vehicle.m_sprite);
    if (vehicle.m_turnAnimStartTime > 0) {
        if (vehicle.m_turnCW)
            direction = std::bit_cast<int32_t>(static_cast<uint32_t>(direction) + 1u) % 8;
        else
            direction = direction == 0 ? 7 : std::bit_cast<int32_t>(static_cast<uint32_t>(direction) - 1u);
    }
    env.SetDirection(vehicle.m_sprite, direction);
    if (std::bit_cast<int32_t>(vehicle.m_numTurns) > 0) {
        vehicle.m_turnAnimStartTime = now;
        env.PlayTurnAnimation(vehicle);
        return;
    }
    env.SetMoving(vehicle, true);
    if (vehicle.m_state == 0x45) {
        const int32_t id = static_cast<int32_t>(vehicle.m_id);
        env.Post(messages::Make(id, id, obl::messages::MT_MOVE2SEAPOINT, FloatToIntWrap(vehicle.m_CurrentRoamPoint[0]),
                FloatToIntWrap(vehicle.m_CurrentRoamPoint[1])));
    }
    env.PlayRideAnimation(vehicle, now);
}

// Indices are the output order of the eight neighbouring cells, not sprite directions.
// Return lanes overlap departures but are shifted by one cell.
template<class Env> void Lane(uint32_t x, uint32_t y, int32_t direction, bool returning, bool blocked, Env& env)
{
    map::GridCell* cells[8];
    env.NeighbourCells(x, y, cells);
    constexpr int kDepart[4][4] = {{7, 6, 5, 4}, {0, 1, 7, 6}, {0, 1, 2, 3}, {2, 3, 4, 5}};
    constexpr int kReturn[4][4] = {{0, 7, 6, 5}, {2, 1, 0, 7}, {4, 3, 1, 2}, {3, 4, 5, 6}};
    if (direction != 1 && direction != 3 && direction != 5 && direction != 7) return;
    const int* indices = returning ? kReturn[direction / 2] : kDepart[direction / 2];
    for (int i = 0; i < 4; ++i) {
        uint32_t& flags = cells[indices[i]]->m_flags;
        if (blocked)
            flags |= lifecycle::kBlockedFlag;
        else
            flags &= ~lifecycle::kBlockedFlag;
    }
}

// . Only types 0, 1, 4 and 6 have animation names; the others play nothing.
// Null names for the other types (and a second jet-ski passenger) replace undefined stack contents.
template<class Env> void PlayTurnAnimation(object::Vehicle& vehicle, Env& env)
{
    const bool cw = vehicle.m_turnCW != 0;
    const char* body = nullptr;
    const char* driver = nullptr;
    const char* passenger = nullptr;
    switch (vehicle.m_vehicleType) {
    case lifecycle::Jetski:
        body = driver = cw ? "d_jetski_turn_r" : "d_jetski_turn_l";
        env.StartAnim(vehicle.m_vehicleSprite, cw ? "d_wake_turn_r" : "d_wake_turn_l", vehicle.m_turnAnimStartTime, vehicle.m_WorldXYZ);
        break;
    case lifecycle::SpeedBoat:
        body = cw ? "boat_turn_right" : "boat_turn_left";
        driver = cw ? "boat_driver_turn_right" : "boat_driver_turn_left";
        passenger = cw ? "boat_pass_turn_right" : "boat_pass_turn_left";
        break;
    case lifecycle::Pedaloe:
        body = cw ? "turn_r" : "turn_l";
        driver = cw ? "d_ped_turn_r_pass" : "d_ped_turn_l_pass";
        passenger = cw ? "d_ped_turn_r" : "d_ped_turn_l";
        break;
    case lifecycle::FishBoat:
        body = "boat_turn";
        driver = "fish_man1_turn";
        passenger = "fish_man2_turn";
        env.StartAnim(vehicle.m_auxSprite1, "rod01_turn", vehicle.m_turnAnimStartTime, vehicle.m_WorldXYZ);
        env.StartAnim(vehicle.m_auxSprite2, "rod02_turn", vehicle.m_turnAnimStartTime, vehicle.m_WorldXYZ);
        env.StartAnim(vehicle.m_auxSprite3, "fish_driver_turn", vehicle.m_turnAnimStartTime, vehicle.m_WorldXYZ);
        break;
    }
    vehicle.m_turnAnimStartTime = env.WorldTime();
    vehicle.m_turnAnimLength = env.StartAnim(vehicle.m_sprite, body, vehicle.m_turnAnimStartTime, vehicle.m_WorldXYZ);
    env.SetAnimType(vehicle.m_sprite, 2);
    const bool fishingCW = vehicle.m_vehicleType == lifecycle::FishBoat && cw;
    if (fishingCW) {
        env.SetAnimType(vehicle.m_sprite, 1);
        env.SetAnimType(vehicle.m_auxSprite1, 1);
        env.SetAnimType(vehicle.m_auxSprite2, 1);
        env.SetAnimType(vehicle.m_auxSprite3, 1);
    }
    if (vehicle.m_currCap != 0) {
        env.TeleportGuest(vehicle.m_Guests[0], vehicle.m_WorldXYZ);
        env.StartGuestAnim(vehicle.m_Guests[0], driver, vehicle.m_turnAnimStartTime, vehicle.m_WorldXYZ);
        env.SetGuestAnimType(vehicle.m_Guests[0], fishingCW ? 1 : 2);
        if (static_cast<uint32_t>(vehicle.m_currCap) > 1) {
            env.TeleportGuest(vehicle.m_Guests[1], vehicle.m_WorldXYZ);
            env.StartGuestAnim(vehicle.m_Guests[1], passenger, vehicle.m_turnAnimStartTime, vehicle.m_WorldXYZ);
            env.SetGuestAnimType(vehicle.m_Guests[1], fishingCW ? 1 : 2);
        }
    }
    --vehicle.m_numTurns;
}

template<class Env> void TurnToDirection(object::Vehicle& vehicle, int32_t direction, Env& env)
{
    const int32_t current = env.Direction(vehicle.m_sprite);
    if (current == direction) {
        vehicle.m_turnAnim = 1;
        vehicle.m_numTurns = 0;
        env.PlayRideAnimation(vehicle, env.WorldTime());
        return;
    }
    env.SetMoving(vehicle, false);
    vehicle.m_turnAnim = 1;
    const uint32_t distance = current < direction ? static_cast<uint32_t>(direction) - current : static_cast<uint32_t>(current) - direction;
    vehicle.m_numTurns = distance;
    vehicle.m_turnCW = current < direction;
    // The half-circle tie follows the numeric comparison; there is no modulo normalization.
    if (std::bit_cast<int32_t>(distance) > 4) {
        vehicle.m_turnCW = !vehicle.m_turnCW;
        vehicle.m_numTurns = 8u - distance;
    }
    vehicle.m_turnAnimStartTime = env.WorldTime();
    env.PlayTurnAnimation(vehicle);
}

template<class Env> void MoveToTurnArea(object::Vehicle& vehicle, Env& env)
{
    float target[3];
    env.TileToWorldPoint(vehicle.m_TurnArea[0], vehicle.m_TurnArea[1], target);
    // Both differences stay in double precision until the squared length is stored as a float.
    const double dx = static_cast<double>(vehicle.m_WorldXYZ[0]) - target[0];
    const double dy = static_cast<double>(vehicle.m_WorldXYZ[1]) - target[1];
    const float distance = path::ApproxSqrt(static_cast<float>(dx * dx + dy * dy));
    const bool near = !(std::fabs(distance) >= 200.0f);
    const int32_t id = static_cast<int32_t>(vehicle.m_id);
    env.Post(messages::Make(id, id, obl::messages::MT_MOVE2SEAPOINT, FloatToIntWrap(near ? target[0] : vehicle.m_WorldXYZ[0]),
            FloatToIntWrap(near ? target[1] : vehicle.m_WorldXYZ[1])));
    // Post may deliver immediately; the receiver must still see the old flag during delivery.
    vehicle.m_moveToTurnPoint = 1;
}

// ---- 3d-2: circuit, roaming, fishing and aiming helpers -------------------------------------------------------------------------------
//
// Env (beyond the above): `int Rand`, `bool PathIsStraight(const Vehicle&)` (the route straightness test of m_pathCursor),
// `GridCell* CellAt(x, y)`, `void Neighbour(x, y, direction, uint32_t out[2])`, `void TilePosition(const Vehicle&, uint32_t out[2])`,
// `void TileToWorldPoint(x, y, float out[3])`, `void TileToPathPoint(x, y, float out[3])`, `void TurnToDirection(Vehicle&, int32_t)`.

namespace detail {

constexpr uint32_t kWaterBits = 0x60000000; // cell flags a boat may enter (either of the two water kinds)

// The signed remainder.
inline int32_t Mod8(int32_t value)
{
    return value % 8;
}

// Water that is not blocked (bit 0x10).
inline bool Open(uint32_t flags)
{
    return (flags & kWaterBits) != 0 && !(flags & lifecycle::kBlockedFlag);
}
// The same, and inside the playable border.
inline bool Sailable(uint32_t x, uint32_t y, uint32_t flags)
{
    const auto sx = static_cast<int32_t>(x), sy = static_cast<int32_t>(y);
    return sx >= 2 && sx <= 0x7e && sy >= 2 && sy <= 0x7e && Open(flags);
}

template<class Env> void SetCurrentRoamPoint(object::Vehicle& vehicle, uint32_t x, uint32_t y, Env& env)
{
    float point[3];
    env.TileToWorldPoint(x, y, point);
    std::memcpy(vehicle.m_CurrentRoamPoint, point, sizeof(point));
}

// A move order to the current roam point (truncated into flags and data), or a turn when the boat does not face `direction` yet.
template<class Env> void MoveOrTurn(object::Vehicle& vehicle, int32_t direction, Env& env)
{
    if (direction == env.Direction(vehicle.m_sprite)) {
        const int32_t id = static_cast<int32_t>(vehicle.m_id);
        env.Post(messages::Make(id, id, obl::messages::MT_MOVE2SEAPOINT, FloatToIntWrap(vehicle.m_CurrentRoamPoint[0]),
                FloatToIntWrap(vehicle.m_CurrentRoamPoint[1])));
    } else {
        env.TurnToDirection(vehicle, direction);
    }
}

} // namespace detail

// . Jet ski and speed boat: advance to the next free point of the circuit (a point is taken while a boat blocks its cell),
// order the move and set the cruising speed from the shape of the path; no free point before the start again means "stop here" (state 0x42).
template<class Env> void MoveToNextCircuitPoint(object::Vehicle& vehicle, Env& env)
{
    const int32_t id = static_cast<int32_t>(vehicle.m_id);
    const auto points = [&]() { return static_cast<const uint32_t (*)[2]>(vehicle.m_circuitPoints); };
    const auto advance = [&]() {
        // Signed division: a zero count would fault.
        vehicle.m_currCircuitPoint = static_cast<uint32_t>(
                (static_cast<int32_t>(vehicle.m_currCircuitPoint) + 1) % static_cast<int32_t>(vehicle.m_circuitPointCount));
    };
    const auto stop = [&]() { env.Post(messages::Make(id, id, obl::messages::MT_STATECHANGE, 0, 0x42)); };
    advance();
    if (vehicle.m_currCircuitPoint == vehicle.m_startCircuitPoint) {
        stop();
    } else {
        for (;;) {
            const uint32_t* point = points()[vehicle.m_currCircuitPoint];
            if (!(env.CellAt(point[0], point[1])->m_flags & lifecycle::kBlockedFlag)) {
                float path[3];
                env.TileToPathPoint(point[0], point[1], path);
                env.Post(messages::Make(id, id, obl::messages::MT_SWIM2SEAPOINT, FloatToIntWrap(path[0]), FloatToIntWrap(path[1])));
                // Post may deliver at once and drop the cursor, so it is read here, not before.
                if (vehicle.m_pathCursor && vehicle.m_vehicleType != lifecycle::SpeedBoat)
                    vehicle.m_maxVel = env.PathIsStraight(vehicle) ? 200.0f : 100.0f;
                break;
            }
            advance();
            if (vehicle.m_currCircuitPoint == vehicle.m_startCircuitPoint) {
                stop();
                break;
            }
        }
    }
    const uint32_t* here = points()[vehicle.m_currCircuitPoint];
    detail::SetCurrentRoamPoint(vehicle, here[0], here[1], env);
}

// . Pedal boat: from a random start direction look for the first direction with more than five free tiles in a row (else the longest run),
// set the end of the run as the roam point and turn towards it (or sail when already facing it); nothing free -> state 0x42.
template<class Env> void PickRoamPoint(object::Vehicle& vehicle, Env& env)
{
    uint32_t tile[2];
    env.TilePosition(vehicle, tile);
    int32_t direction = detail::Mod8(env.Rand());
    int32_t best = 0, bestDirection = direction;
    uint32_t bestTile[2] = {tile[0], tile[1]}, last[2] = {0, 0};
    bool found = false;
    for (int attempt = 0; attempt < 8 && !found; ++attempt) {
        uint32_t cursor[2] = {tile[0], tile[1]};
        int32_t steps = 0;
        for (; steps < 8; ++steps) {
            env.Neighbour(cursor[0], cursor[1], direction, cursor);
            const uint32_t flags = env.CellAt(cursor[0], cursor[1])->m_flags;
            if (!detail::Sailable(cursor[0], cursor[1], flags)) break;
            last[0] = cursor[0];
            last[1] = cursor[1];
        }
        if (best < steps) {
            bestTile[0] = last[0];
            bestTile[1] = last[1];
            best = steps;
            bestDirection = direction;
            if (steps > 5) found = true;
        }
        direction = detail::Mod8(direction + 1);
    }
    const int32_t id = static_cast<int32_t>(vehicle.m_id);
    if (best == 0) {
        env.Post(messages::Make(id, id, obl::messages::MT_STATECHANGE, 0, 0x42));
        return;
    }
    env.SetMoving(vehicle, false);
    detail::SetCurrentRoamPoint(vehicle, bestTile[0], bestTile[1], env);
    detail::MoveOrTurn(vehicle, detail::Mod8(bestDirection + 1), env);
}

// . Fishing boat: free the cells around its berth, look up to 15 tiles along the departure direction (the first two need only a free tile,
// later ones also free neighbours); five or more tiles -> head there (the cells around the berth are blocked again) and answer true.
// Without a spot the berth cells stay free.
template<class Env> bool PickFishingPoint(object::Vehicle& vehicle, Env& env)
{
    map::GridCell* around[8];
    env.NeighbourCells(vehicle.m_CurrGridXY[0], vehicle.m_CurrGridXY[1], around);
    for (map::GridCell* cell : around) cell->m_flags &= ~lifecycle::kBlockedFlag;
    uint32_t tile[2];
    env.TilePosition(vehicle, tile);
    const int32_t direction = detail::Mod8(static_cast<int32_t>(static_cast<uint32_t>(vehicle.m_unloadDir) + 2u));
    uint32_t cursor[2] = {tile[0], tile[1]}, best[2] = {tile[0], tile[1]};
    int32_t count = 0;
    for (; count < 15; ++count) {
        env.Neighbour(cursor[0], cursor[1], detail::Mod8(direction + 7), cursor);
        const uint32_t flags = env.CellAt(cursor[0], cursor[1])->m_flags;
        if (!detail::Sailable(cursor[0], cursor[1], flags)) break;
        if (count > 1) {
            map::GridCell* ring[8];
            env.NeighbourCells(cursor[0], cursor[1], ring);
            bool open = true;
            for (map::GridCell* cell : ring) open &= detail::Open(cell->m_flags);
            if (!open) break;
        }
        best[0] = cursor[0];
        best[1] = cursor[1];
    }
    if (count <= 4) return false;
    env.SetMoving(vehicle, false);
    detail::SetCurrentRoamPoint(vehicle, best[0], best[1], env);
    detail::MoveOrTurn(vehicle, direction, env);
    env.NeighbourCells(vehicle.m_CurrGridXY[0], vehicle.m_CurrGridXY[1], around);
    for (map::GridCell* cell : around) cell->m_flags |= lifecycle::kBlockedFlag;
    return true;
}

// . Turn to the sector (of eight) pointing from the boat to (x, y). The target is moved by (1, 1) first; the arithmetic is fixed (tests pin the result):
// the x difference and the sum are stored as floats, the y difference stays extended for its product, the length is the table square root
// and the unit vector uses the reciprocal estimate (vec2::ReciprocalEstimate), never a division.
template<class Env> void TurnTowardsPoint(object::Vehicle& vehicle, float x, float y, Env& env)
{
    const float ty = static_cast<float>(static_cast<double>(y) + 1.0);
    const float dx = static_cast<float>((static_cast<double>(x) + 1.0) - static_cast<double>(vehicle.m_vPosition[0]));
    const double dyExtended = static_cast<double>(ty) - static_cast<double>(vehicle.m_vPosition[1]);
    const float dy = static_cast<float>(dyExtended);
    const float squared = static_cast<float>(dyExtended * dy + static_cast<double>(dx) * dx);
    const float inverse = vec2::ReciprocalEstimate(path::ApproxSqrt(squared));
    env.TurnToDirection(vehicle,
            path::DirectionIndex(
                    static_cast<float>(static_cast<double>(inverse) * dx), static_cast<float>(static_cast<double>(dy) * inverse), 8));
}

// ---- 3d-3: ride, idle, catch and boarding animations ----------------------------------------------------------------------------------
//
// Env (beyond the above): `WorldTime`, `Rand`, `StartAnim`, `SetAnimType`, `TeleportGuest`, `StartGuestAnim`, `SetGuestAnimType` as in
// PlayTurnAnimation, plus `void* GuestSprite(int32_t guest)`, `void* GuestAttachedSprite(int32_t guest)` (Guest::m_attachedSprite, may be
// null), `void SetDirection(void* sprite, int32_t)` (clamped to the animation's direction count), `int32_t Direction(void* sprite)`,
// `void SetGuestVisible(int32_t guest, bool)`, `void SetGuestPosition(int32_t guest, float position[3])` and `bool WharfWalkPoint(float out[3])`
// (the last walk point of the first "Wharf" building, z = 0; false when no wharf stands). Names that are not defined (types 2, 3, 5 in PlayRideAnimation and a second jet-ski
// passenger) are nullptr here, as in PlayTurnAnimation.

// Every part restarts its normal loop; the turn bookkeeping at 0x900..0x90c is cleared.
template<class Env> void PlayRide(object::Vehicle& vehicle, float startTime, Env& env)
{
    const char* body = nullptr;
    const char* guest0 = nullptr;
    const char* guest1 = nullptr;
    const char* wake = nullptr;
    const char* aux1 = nullptr;
    const char* aux2 = nullptr;
    const char* aux3 = nullptr;
    switch (vehicle.m_vehicleType) {
    case lifecycle::Jetski:
        body = guest0 = "d_jetski_normal";
        wake = "d_wake_normal";
        break;
    case lifecycle::SpeedBoat:
        body = "boat_fast";
        guest0 = "boat_driver_normal";
        guest1 = "boat_pass_normal";
        break;
    case lifecycle::Pedaloe:
        body = "ride";
        guest0 = "d_ped_ride_pass";
        guest1 = "d_ped_ride";
        break;
    case lifecycle::FishBoat:
        body = "boat_bob";
        guest0 = "fish_man1_fidget";
        guest1 = "fish_man2_fidget";
        aux1 = "rod01_bob";
        aux2 = "rod02_bob";
        aux3 = "fish_driver";
        break;
    }
    env.StartAnim(vehicle.m_sprite, body, startTime, vehicle.m_WorldXYZ);
    env.SetAnimType(vehicle.m_sprite, 0);
    if (vehicle.m_currCap != 0) {
        env.TeleportGuest(vehicle.m_Guests[0], vehicle.m_WorldXYZ);
        env.StartGuestAnim(vehicle.m_Guests[0], guest0, startTime, vehicle.m_WorldXYZ);
        env.SetGuestAnimType(vehicle.m_Guests[0], 0);
        if (static_cast<uint32_t>(vehicle.m_currCap) > 1) {
            env.TeleportGuest(vehicle.m_Guests[1], vehicle.m_WorldXYZ);
            env.StartGuestAnim(vehicle.m_Guests[1], guest1, startTime, vehicle.m_WorldXYZ);
            env.SetGuestAnimType(vehicle.m_Guests[1], 0);
        }
        // The wake belongs to the jet-ski's rider block: an empty jet-ski keeps its old wake animation.
        if (wake) {
            env.StartAnim(vehicle.m_vehicleSprite, wake, startTime, vehicle.m_WorldXYZ);
            env.SetAnimType(vehicle.m_vehicleSprite, 0);
        }
    }
    if (aux1) {
        env.StartAnim(vehicle.m_auxSprite1, aux1, startTime, vehicle.m_WorldXYZ);
        env.SetAnimType(vehicle.m_auxSprite1, 0);
    }
    if (aux2) {
        env.StartAnim(vehicle.m_auxSprite2, aux2, startTime, vehicle.m_WorldXYZ);
        env.SetAnimType(vehicle.m_auxSprite2, 0);
    }
    // The third part is chosen by its sprite pointer, not by its name.
    if (vehicle.m_auxSprite3) {
        env.StartAnim(vehicle.m_auxSprite3, aux3, startTime, vehicle.m_WorldXYZ);
        env.SetAnimType(vehicle.m_auxSprite3, 0);
    }
    vehicle.m_numTurns = 0;
    vehicle.m_turnAnimStartTime = 0;
    vehicle.m_turnAnimLength = 0;
    vehicle.m_turnAnim = 0;
    vehicle.m_turnCW = 0;
}

// . The loop of the waiting vehicle starts at the world time plus up to 0.99 s (one rand draw), so boats do not bob in step.
// The "animation type 0" of the rod/driver parts goes into the *hull* sprite, never into the part itself (kept: golden tests pin it).
template<class Env> void PlayIdle(object::Vehicle& vehicle, Env& env)
{
    const char* body = nullptr;
    const char* aux1 = nullptr;
    const char* aux2 = nullptr;
    const char* aux3 = nullptr;
    switch (vehicle.m_vehicleType) {
    case lifecycle::Jetski: body = "d_jetski_normal"; break;
    case lifecycle::SpeedBoat: body = "boat_normal"; break;
    case lifecycle::Pedaloe:
        // Started once at the exact world time, then again below with the random offset.
        env.StartAnim(vehicle.m_sprite, "ride", env.WorldTime(), vehicle.m_WorldXYZ);
        body = "ride";
        break;
    case lifecycle::FishBoat:
        body = "boat_bob";
        aux1 = "rod01_bob";
        aux2 = "rod02_bob";
        aux3 = "fish_driver";
        break;
    }
    float start = 0;
    if (body) {
        const float now = env.WorldTime();
        const int offset = env.Rand() % 100;
        // The product and the sum stay in double precision, one rounding to float.
        start = static_cast<float>(static_cast<double>(offset) * static_cast<double>(0.01f) + static_cast<double>(now));
        env.StartAnim(vehicle.m_sprite, body, start, vehicle.m_WorldXYZ);
        env.SetAnimType(vehicle.m_sprite, 0);
    }
    if (aux1) {
        env.StartAnim(vehicle.m_auxSprite1, aux1, start, vehicle.m_WorldXYZ);
        env.SetAnimType(vehicle.m_sprite, 0);
    }
    if (aux2) {
        env.StartAnim(vehicle.m_auxSprite2, aux2, start, vehicle.m_WorldXYZ);
        env.SetAnimType(vehicle.m_sprite, 0);
    }
    if (aux3) {
        env.StartAnim(vehicle.m_auxSprite3, aux3, start, vehicle.m_WorldXYZ);
        env.SetAnimType(vehicle.m_sprite, 0);
    }
}

// . The fishermen reel in a catch: variant "a" 4 times out of 5, otherwise "b". Returns the length of the first rod animation (0 without
// passengers). The second fisherman rolls only when the first got variant "a" and picks "b" on one in five. Rods are not placed in the world
// (null position) and have no animation type set.
template<class Env> float PlayCatch(object::Vehicle& vehicle, Env& env)
{
    const float now = env.WorldTime();
    float length = 0;
    if (vehicle.m_currCap == 0) return length;
    env.TeleportGuest(vehicle.m_Guests[0], vehicle.m_WorldXYZ);
    const bool firstB = env.Rand() % 5 == 0;
    env.StartGuestAnim(vehicle.m_Guests[0], firstB ? "fish_man1_catch_b" : "fish_man1_catch_a", now, vehicle.m_WorldXYZ);
    env.SetGuestAnimType(vehicle.m_Guests[0], 0);
    length = env.StartAnim(vehicle.m_auxSprite1, firstB ? "rod01_catch_b" : "rod01_catch_a", now, nullptr);
    if (static_cast<uint32_t>(vehicle.m_currCap) > 1) {
        const bool secondB = !firstB && env.Rand() % 5 == 0;
        env.TeleportGuest(vehicle.m_Guests[1], vehicle.m_WorldXYZ);
        env.StartGuestAnim(vehicle.m_Guests[1], secondB ? "fish_man2_catch_b" : "fish_man2_catch_a", now, vehicle.m_WorldXYZ);
        env.StartAnim(vehicle.m_auxSprite2, secondB ? "rod02_catch_b" : "rod02_catch_a", now, nullptr);
        env.SetGuestAnimType(vehicle.m_Guests[1], 0);
    }
    return length;
}

// Arguments: start time, boarding, pair. Mounting (`boarding`) or dismounting animation of the vehicle and its first two riders;
// returns the length of the last animation started on the hull or a rider. Animation type of hull and riders: 2 (play once) for the fishing boat
// and for boarding, 1 (countdown) for dismounting. The ferry (2) and the bus (5) do not animate the hull: the last passenger on the list is
// shown or hidden (ferry: placed at the wharf's last walk point on the way out) or walks through the door (bus, below).
// Bug fix: the first wharf used to be read without checking that one exists, so a ferry unloading after the player demolished
// the wharf read a stale object (crash or nondeterminism, found by the chaos bot); without a wharf the passenger steps off at the
// ferry's own position.
template<class Env> float PlayBoarding(object::Vehicle& vehicle, float startTime, bool boarding, bool pair, Env& env)
{
    const int32_t type = vehicle.m_vehicleType;
    const int32_t last = vehicle.m_currCap;
    if (type == lifecycle::Ferry) {
        const int32_t guest = vehicle.m_Guests[last - 1];
        if (!boarding) {
            float point[3];
            if (!env.WharfWalkPoint(point)) {
                point[0] = vehicle.m_WorldXYZ[0];
                point[1] = vehicle.m_WorldXYZ[1];
                point[2] = 0;
            }
            env.SetGuestPosition(guest, point);
        }
        env.SetGuestVisible(guest, !boarding);
        return 0.5f;
    }
    if (type == lifecycle::Coach) {
        const int32_t guest = vehicle.m_Guests[last - 1];
        env.TeleportGuest(guest, vehicle.m_WorldXYZ);
        env.SetGuestVisible(guest, true);
        void* sprite = env.GuestSprite(guest);
        env.SetDirection(sprite, env.Direction(vehicle.m_sprite));
        env.SetAnimType(sprite, 2);
        const char* name = boarding ? "d_walk_onbus" : "d_walk_offbus";
        if (void* bag = env.GuestAttachedSprite(guest)) {
            env.SetDirection(bag, env.Direction(vehicle.m_sprite));
            env.StartAnim(bag, "bags_offbus", startTime, vehicle.m_WorldXYZ);
            if (boarding) env.SetAnimType(bag, 1);
        } else {
            name = boarding ? "d_walk_onbus_nobag" : "d_walk_offbus_nobag";
        }
        return env.StartAnim(sprite, name, startTime, vehicle.m_WorldXYZ);
    }

    const char* body = nullptr;
    const char* guest0 = nullptr;
    const char* guest1 = nullptr;
    const char* aux1 = nullptr;
    const char* aux2 = nullptr;
    const char* aux3 = nullptr;
    int32_t animType = 2;
    switch (type) {
    case lifecycle::Jetski:
        body = "d_stand_mount_jetski";
        guest0 = "d_stand_mount_jetski";
        break;
    case lifecycle::SpeedBoat:
        body = "boat_mount";
        guest0 = "boat_driver_mount";
        if (pair) {
            guest1 = "boat_pass_mount";
            guest0 = "boat_driver_pass_mount";
        }
        break;
    case lifecycle::Pedaloe:
        body = "climb_across";
        guest0 = "d_ped_climb_across";
        if (pair) {
            guest1 = "d_ped_climb_on";
            body = "climb_on";
            guest0 = "d_ped_climb_on_pass";
        }
        break;
    case lifecycle::FishBoat:
        body = "boat_bob";
        if (pair) {
            guest0 = "fish_man1_fidget";
            guest1 = boarding ? "fish_man2_onboat" : "fish_man2_outboat";
        } else {
            guest0 = boarding ? "fish_man1_onboat" : "fish_man1_outboat";
        }
        aux1 = "rod01_bob";
        aux2 = "rod02_bob";
        aux3 = "fish_driver";
        break;
    }
    if (type != lifecycle::FishBoat) animType = boarding ? 2 : 1;

    float length = env.StartAnim(vehicle.m_sprite, body, startTime, vehicle.m_WorldXYZ);
    env.SetAnimType(vehicle.m_sprite, animType);
    // The riders are chosen by name alone: an empty vehicle still reaches for guest slot 0 and 1 when the name is set.
    if (guest0) {
        env.TeleportGuest(vehicle.m_Guests[0], vehicle.m_WorldXYZ);
        length = env.StartGuestAnim(vehicle.m_Guests[0], guest0, startTime, vehicle.m_WorldXYZ);
        env.SetGuestAnimType(vehicle.m_Guests[0], animType);
    }
    if (guest1) {
        env.TeleportGuest(vehicle.m_Guests[1], vehicle.m_WorldXYZ);
        length = env.StartGuestAnim(vehicle.m_Guests[1], guest1, startTime, vehicle.m_WorldXYZ);
        env.SetGuestAnimType(vehicle.m_Guests[1], animType);
    }
    if (aux1) env.StartAnim(vehicle.m_auxSprite1, aux1, startTime, nullptr);
    if (aux2) env.StartAnim(vehicle.m_auxSprite2, aux2, startTime, nullptr);
    if (aux3) env.StartAnim(vehicle.m_auxSprite3, aux3, startTime, nullptr);
    return length;
}

} // namespace obl::vehicle::helpers
