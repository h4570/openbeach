#pragma once

#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_vehicle.h"
#include "game/map/path_data.h"
#include "core/ascii_case.h"
#include "core/vec2.h"
#include "game/objects/vehicle_messages.h"

#include <cmath>
#include <cstdint>

// Update (no arguments, answers true; docs/architecture/objects.md, "Update"). Pure logic over an `Env`; the game side is
// vehicle_messages.cpp. Env, beyond the methods of vehicle_messages.h (Post, PostDelayed, WorldTime, NeighbourCells, PlaySound, SetAnimType,
// StartAnim, SetDirection, Direction, TeleportGuest, GuestSprite):
//   bool BaseUpdate(Vehicle&)                          the Mover Update (false is the game's fatal error)
//   void FatalError(const Vehicle&)                    FatalError("Movement update of vehicle \"%s\" failed", name); does not return
//   void StepTurnAnimation(Vehicle&, float now): the turn animation of a boat that is turning
//   void TeleportSelf(Vehicle&, const float position[3])   the vehicle's Teleport (Mover's Teleport), position by value
//   void MapTile(const float world[3], uint32_t out[2])      the map view world-to-tile conversion
//   float DeltaTime                                        world frame time (the ferry's step per tick)
namespace obl::vehicle::update {

using object::Vehicle;
using obl::messages::Message;
namespace life = lifecycle;
namespace msg = messages;

namespace detail {

// The ferry's sideways step per tick: speed * dt along the compass direction. The second component is kept unrounded in double precision
// (only the first is stored as a float), so it stays a double here. A direction outside 0..7 leaves both undefined there; zero here.
struct Step {
    float x = 0;
    double y = 0;
};
inline Step FerryStep(float deltaTime, float maxVel, int32_t direction)
{
    const double w = static_cast<double>(deltaTime) * static_cast<double>(maxVel);
    Step step;
    switch (direction) {
    case 0:
        step.x = 0;
        step.y = -w;
        break;
    case 1:
        step.x = static_cast<float>(w * 0.5);
        step.y = w * -0.5;
        break;
    case 2:
        step.x = static_cast<float>(w);
        step.y = 0;
        break;
    case 3:
        step.x = static_cast<float>(w * 0.5);
        step.y = w * 0.5;
        break;
    case 4:
        step.x = 0;
        step.y = w;
        break;
    case 5:
        step.x = static_cast<float>(w * -0.5);
        step.y = w * 0.5;
        break;
    case 6:
        step.x = static_cast<float>(-w);
        step.y = 0;
        break;
    case 7:
        step.x = static_cast<float>(w * -0.5);
        step.y = w * -0.5;
        break;
    default: break;
    }
    return step;
}

// "from `before` to `after` the value crossed zero (or sits on it)", decided so that an unordered compare counts as below, so a NaN
// counts as "not above zero" in the second alternative.
inline bool Crossed(float before, float after)
{
    return (before >= 0.0f && !(after > 0.0f)) || (!(before > 0.0f) && after >= 0.0f);
}

// Heading of a jet ski or speed boat in sixteenths, from its velocity (the direction to the target when it is almost still).
inline int32_t Sector(const Vehicle& vehicle)
{
    float x = vehicle.m_vVelocity[0], y = vehicle.m_vVelocity[1];
    const float length = path::ApproxSqrt(static_cast<float>(static_cast<double>(x) * x + static_cast<double>(y) * y));
    // The constant is the float 0.0001 widened; the compare is false for NaN, which then takes the target.
    if (!(std::fabs(static_cast<double>(length)) >= static_cast<double>(1e-4f))) {
        x = static_cast<float>(static_cast<double>(vehicle.m_vTarget[0]) - static_cast<double>(vehicle.m_vPosition[0]));
        y = static_cast<float>(static_cast<double>(vehicle.m_vTarget[1]) - static_cast<double>(vehicle.m_vPosition[1]));
    }
    const float squared = static_cast<float>(static_cast<double>(x) * x + static_cast<double>(y) * y);
    const float inverse = vec2::ReciprocalEstimate(path::ApproxSqrt(squared));
    return path::DirectionIndex(
            static_cast<float>(static_cast<double>(inverse) * x), static_cast<float>(static_cast<double>(y) * inverse), 16);
}

// State 0x41: at the stop. The ferry and the coach (and an empty small boat) send the departure when nobody is left to wait for; a boat with guests
// leaves a second after its ride time.
template<class Env> void Idle(Vehicle& vehicle, float now, Env& env)
{
    const int32_t type = vehicle.m_vehicleType;
    const bool crowd = type == life::Ferry || type == life::Coach;
    if (vehicle.m_currCap == 0 || crowd) {
        if (vehicle.m_guestsToWaitFor != 0 || !crowd || vehicle.m_definition->m_MoveType != 1) return;
        const Message depart = vehicle.StateChange(msg::StateDepart);
        if (type == life::Coach) {
            if (vehicle.m_playingDoorAnim) return;
            const float position[3] = {vehicle.m_WorldXYZ[0], vehicle.m_WorldXYZ[1], 0.0f};
            env.PlaySound("doorhissopen", position);
            vehicle.m_playingDoorAnim = 1;
            env.SetAnimType(vehicle.m_doorSprite, 1);
            const float length = env.StartAnim(vehicle.m_doorSprite, "coach_doors", now, vehicle.m_WorldXYZ);
            env.PostDelayed(depart, length);
        } else {
            env.Post(depart);
        }
        return;
    }
    // The elapsed time is the difference of two floats compared with 1.0 as an extended value; false for NaN.
    if (!(static_cast<double>(now) - static_cast<double>(vehicle.m_rideTime) > 1.0) || vehicle.m_waitForGuestToBoard) return;
    env.Post(vehicle.StateChange(vehicle.m_definition->m_MoveType == 1 ? msg::StateDepart : msg::StateRide));
}

// States 0x42, 0x44, 0x45: a jet ski or speed boat turns its sprite towards where it is heading, then every guest follows the boat's facing;
// a boat in the middle of a turn steps that animation instead.
template<class Env> void Moving(Vehicle& vehicle, float now, Env& env)
{
    const int32_t type = vehicle.m_vehicleType;
    if (type == life::Jetski || type == life::SpeedBoat) {
        if (vehicle.m_moving && !vehicle.m_turnAnim) {
            const int32_t sector = Sector(vehicle);
            const bool even = sector % 2 == 0;
            const char* name =
                    type == life::Jetski ? (even ? "d_jetski_normal" : "d_jetski_normal_offset") : (even ? "boat_normal" : "boat_fast_rot");
            env.StartAnim(vehicle.m_sprite, name, 0.0f, vehicle.m_WorldXYZ);
            env.SetDirection(vehicle.m_sprite, sector / 2);
        }
        if (type == life::Jetski) env.SetDirection(vehicle.m_vehicleSprite, env.Direction(vehicle.m_sprite));
    }
    if (vehicle.m_turnAnim) {
        env.StepTurnAnimation(vehicle, now);
        return;
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(vehicle.m_currCap); ++i) {
        const int32_t guest = vehicle.m_Guests[i];
        env.TeleportGuest(guest, vehicle.m_WorldXYZ);
        const int32_t facing = env.Direction(vehicle.m_sprite);
        env.SetDirection(env.GuestSprite(guest), facing);
    }
}

// State 0x46 (turning at the unload point): the next way point once the turn is over.
template<class Env> void Turning(Vehicle& vehicle, float now, Env& env)
{
    if (vehicle.m_turnAnim) {
        env.StepTurnAnimation(vehicle, now);
        return;
    }
    env.Post(vehicle.StateChange(msg::StateUnload));
    vehicle.m_currentWayPoint = msg::detail::NextWayPoint(vehicle);
}

// State 0x47: the ferry slides its entry offset to zero; reaching it sends the ferry to the way point and on to 0x42. State 0x48 does the same to the exit
// offset but tests the entry offset (already zero by then, a kept quirk) and marks the ferry for removal.
template<class Env> void Entering(Vehicle& vehicle, Env& env)
{
    const Step step = FerryStep(env.DeltaTime(), vehicle.m_maxVel, static_cast<int32_t>(vehicle.m_ferryEntryDir));
    const float beforeX = vehicle.m_FerryEntryOffset[0], beforeY = vehicle.m_FerryEntryOffset[1];
    vehicle.m_FerryEntryOffset[0] = static_cast<float>(static_cast<double>(beforeX) - static_cast<double>(step.x));
    vehicle.m_FerryEntryOffset[1] = static_cast<float>(static_cast<double>(beforeY) - step.y);
    if (!Crossed(beforeX, vehicle.m_FerryEntryOffset[0]) || !Crossed(beforeY, vehicle.m_FerryEntryOffset[1])) return;
    env.TeleportSelf(vehicle, vehicle.m_WayPoints[0]);
    env.Post(vehicle.StateChange(msg::StateReturn));
}
template<class Env> void Leaving(Vehicle& vehicle, Env& env)
{
    const Step step = FerryStep(env.DeltaTime(), vehicle.m_maxVel, static_cast<int32_t>(vehicle.m_ferryExitDir));
    vehicle.m_FerryExitOffset[0] = static_cast<float>(static_cast<double>(vehicle.m_FerryExitOffset[0]) - static_cast<double>(step.x));
    vehicle.m_FerryExitOffset[1] = static_cast<float>(static_cast<double>(vehicle.m_FerryExitOffset[1]) - step.y);
    if (Crossed(vehicle.m_FerryEntryOffset[0], vehicle.m_FerryEntryOffset[0]) &&
            Crossed(vehicle.m_FerryEntryOffset[1], vehicle.m_FerryEntryOffset[1]))
        vehicle.m_waitingForDeath = 1;
}

} // namespace detail

template<class Env> bool Update(Vehicle& vehicle, Env& env)
{
    if (vehicle.m_vehicleType == life::FishBoat && vehicle.m_gotGrid) msg::detail::SetBlocked(vehicle, env, false);
    const float now = env.WorldTime();
    if (vehicle.m_debugBreak) vehicle.m_debugBreak = 0;
    if (!env.BaseUpdate(vehicle)) env.FatalError(vehicle);
    switch (vehicle.m_state) {
    case msg::StateIdle: detail::Idle(vehicle, now, env); break;
    case msg::StateReturn:
    case msg::StateDepart:
    case msg::StateRide: detail::Moving(vehicle, now, env); break;
    case msg::StateTurn: detail::Turning(vehicle, now, env); break;
    case msg::StateWait: detail::Entering(vehicle, env); break;
    case msg::StateRemove: detail::Leaving(vehicle, env); break;
    default: break;
    }
    if (vehicle.IsNamedBoat()) env.SetDirection(vehicle.m_sprite, 0);
    env.MapTile(vehicle.m_WorldXYZ, vehicle.m_CurrGridXY);
    if (vehicle.m_vehicleType == life::FishBoat) {
        msg::detail::SetBlocked(vehicle, env, true);
        vehicle.m_gotGrid = 1;
    }
    return true;
}

} // namespace obl::vehicle::update

namespace obl::object {

// "_stricmp(name, "BOAT") == 0": the object's name, not the definition key.
inline bool Vehicle::IsNamedBoat() const
{
    return ascii::EqualsNoCase(m_Name, "BOAT");
}

} // namespace obl::object
