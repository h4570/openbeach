#pragma once

#include "core/float_to_int.h"
#include "game/map/grid_math.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/mover_access.h"
#include "game/objects/mover_physics.h"
#include "game/map/path_data.h"
#include "core/vec2.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// Mover target and step logic (group C of docs/architecture/objects.md): StepMotion, UpdateMoveTarget,
// SetDestinationFromMessage and CompleteIfAtDestination. Everything they reach outside the object goes through an
// `Env` (the game binds it in src/game/objects/mover.cpp, the unit tests fake it), so the branches can be driven without a game:
//   void Post(const Message&)                                      global message handler Post (synchronous delivery is the handler's decision)
//   void PostDelayed(const Message&, float delay)                  global message handler PostDelayed
//   void SendDelayed(Mover&, const Message&, float delay)   the object's own SendDelayed (a guest runs its script's OnSend first)
//   void ReleaseCursor(PathCursor*)                                releasing the route cursor (drops the route reference, may free cursor and data)
//   int Rand                                                     the game's rand
//   float TerrainHeight(int32_t x, int32_t y)                      the terrain height
//   bool ObjectMoving(int32_t id)                                  m_moving of the object with that id (the id must name a live object)
//   void ObjectPosition(int32_t id, float out[3])                  the object's base position (idem)
//   bool BuildingWalkPoint(int32_t building, uint32_t index, float out[2])   the walk point of the building with that id;
//                                                                  false when the id no longer names a building with that walk point
//   int32_t TileSize  /  uint32_t TileFlags(uint32_t x, uint32_t y)  /  void WorldToTile(float x, float y, float z, uint32_t out[2])
//   void PositionOffset(int variant, float out[2])                 the two static vectors / (variant 1 / 2)
// Float words that are only moved, not computed, are copied as raw bits so even a signalling NaN passes through unchanged.
namespace obl::moving {

using messages::Message;

constexpr uint8_t kGuestType = 3; // Entity::m_type: the only class that sends the object message through its vtable

// A message to the object itself: sender, receiver and type; the other words are zero.
inline Message MakeSelfMessage(const object::Mover& object, int type)
{
    Message message{};
    message.sender = static_cast<int>(object.m_id);
    message.receiver = static_cast<int>(object.m_id);
    message.type = type;
    return message;
}

inline uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// sqrt(dx^2 + dy^2) the way the three target tests of StepMotion measure the distance from position to target: both differences stay unrounded
// in double precision, the sum is rounded once to float and goes through the table square root.
inline float DistanceToTarget(const object::Mover& object)
{
    const double dx = D(object.m_vPosition[0]) - object.m_vTarget[0];
    const double dy = D(object.m_vPosition[1]) - object.m_vTarget[1];
    return path::ApproxSqrt(ToFloat(dy * dy + dx * dx));
}

// The object tells itself it has arrived: MT_MOVEIMHERE carrying its state (extra) after 0.01 s. Guests send it through their own
// SendMessageDelayed slot, which runs their script first; everything else goes through the handler's queue.
template<class Env> void SendArrivalMessage(object::Mover& object, Env& env)
{
    Message message = MakeSelfMessage(object, messages::MT_MOVEIMHERE);
    message.extra = object.m_state;
    constexpr float kDelay = 0.01f;
    if (static_cast<uint8_t>(object.m_type) == kGuestType)
        env.SendDelayed(object, message, kDelay);
    else
        env.PostDelayed(message, kDelay);
}

// ---- CompleteIfAtDestination -------------------------------------------------------------------------------------------------

// The destination counts as reached when both |dest - position| differences are below 0.1 (the double is 0.1f widened).
// An unordered compare counts as below here: a NaN difference passes the test.
// Reaching it stops the object, clears the velocity and force and tells the object itself (MT_MOVEIMHERE, carrying its state) after 0.01 s;
// guests do it through their own SendMessageDelayed slot instead of the queue.
template<class Env> bool CompleteIfAtDestination(object::Mover& object, Env& env)
{
    constexpr double kTolerance = static_cast<double>(0.1f);
    if (std::fabs(D(object.m_DestXYZ[0]) - object.m_WorldXYZ[0]) >= kTolerance) return false;
    if (std::fabs(D(object.m_DestXYZ[1]) - object.m_WorldXYZ[1]) >= kTolerance) return false;
    object.m_moving = 0;
    object.m_vVelocity[0] = object.m_vVelocity[1] = 0.0f;
    object.m_vSteerForce[0] = object.m_vSteerForce[1] = 0.0f;
    SendArrivalMessage(object, env);
    object.m_waitingForMoveImHere = 0;
    object.m_messageReceived = 0.0f;
    return true;
}

// ---- SetDestinationFromMessage -----------------------------------------------------------------------------------------------

// A move command's x/y (the message's flags/data words, read as unsigned and widened to 64 bits) become the destination and the
// behaviour goes to seek. The target is not touched here. The object's own position is copied to its move position first, an inside-building
// object remembers where it left from, and an open route is dropped. The height comes from the terrain at the truncated tile-less point:
// y is truncated from the unrounded integer, x from the float that was stored.
template<class Env> void SetDestinationFromMessage(object::Mover& object, const Message& message, Env& env)
{
    object.m_behaviourState = 1;
    SetMovePosition(object, Bits(object.m_WorldXYZ[0]), Bits(object.m_WorldXYZ[1]));
    if (object.m_insideBuilding) object.m_BuildingLeavingID = object.m_DestObjectID;
    if (object.m_pathCursor) {
        env.ReleaseCursor(object.m_pathCursor);
        object.m_pathCursor = nullptr;
    }
    const float x = ToFloat(static_cast<double>(static_cast<uint32_t>(message.flags)));
    const double yExact = static_cast<double>(static_cast<uint32_t>(message.data));
    object.m_DestXYZ[0] = x;
    object.m_DestXYZ[1] = ToFloat(yExact);
    const int32_t ty = FloatToIntWrap(yExact);
    const int32_t tx = FloatToIntWrap(D(x));
    object.m_DestXYZ[2] = env.TerrainHeight(tx, ty);
}

// ---- StepMotion --------------------------------------------------------------------------------------------------------------

// Behaviour 3 (wander) nudges the steering force by a random vector of two draws in -1.00..0.99 and keeps its length at m_maxForce.
// The force is only scaled down when its length is strictly above the limit (vector division with float(length / limit)).
template<class Env> void Wander(object::Mover& object, Env& env)
{
    constexpr double kScale = static_cast<double>(0.01f);
    const int drawX = env.Rand();
    const float noiseX = ToFloat(static_cast<double>(drawX % 200 - 100) * kScale);
    const int drawY = env.Rand();
    const float noiseY = ToFloat(static_cast<double>(drawY % 200 - 100) * kScale);
    float* force = object.m_vSteerForce;
    const vec2::Vec2 sum = vec2::Add({force[0], force[1]}, {noiseX, noiseY});
    force[0] = sum.x;
    force[1] = sum.y;
    const float length = path::ApproxSqrt(ToFloat(D(force[0]) * force[0] + D(force[1]) * force[1]));
    const float maxForce = object.m_maxForce;
    if (!(maxForce < length)) return; // also when unordered
    const vec2::Vec2 scaled = vec2::Divide({force[0], force[1]}, ToFloat(D(length) / maxForce));
    force[0] = scaled.x;
    force[1] = scaled.y;
}

// Chooses the steering by behaviour (1 seek, 2 arrive, 3 wander, 4 follow: seek on a route, follow off it; anything else keeps the force),
// integrates one step of `dt` and says whether the object has reached what it walks to:
// - behaviour 4 on a route: closer than 1.5 to the target point;
// - behaviour 4 off a route: closer than m_followDistance (a signed int); the first time it is, MT_REACHED_FOLLOW_DISTANCE goes to the object
//   itself and m_reachedFollowDistance is set; the step then only counts as arrival when the followed object has stopped;
// - otherwise: closer than m_targetTolerance.
// Every "closer" is `!(distance >= limit)`: an unordered compare (a NaN tolerance) counts as arrived. (The distance itself never is NaN: the table
// square root maps every bit pattern to a number.)
template<class Env> bool StepMotion(object::Mover& object, float dt, Env& env)
{
    switch (object.m_behaviourState) {
    case 1: SteerSeek(object); break;
    case 2: SteerArrive(object); break;
    case 3: Wander(object, env); break;
    case 4:
        if (object.m_pathCursor == nullptr)
            SteerFollow(object);
        else
            SteerSeek(object);
        break;
    default: break;
    }
    IntegrateMotion(object, dt);

    const float distance = DistanceToTarget(object);
    if (object.m_behaviourState != 4) return !(distance >= object.m_targetTolerance);
    if (object.m_pathCursor != nullptr) return !(distance >= 1.5f);
    if (!(D(distance) >= static_cast<double>(static_cast<int32_t>(object.m_followDistance)))) {
        if (!object.m_reachedFollowDistance) {
            env.Post(MakeSelfMessage(object, messages::MT_REACHED_FOLLOW_DISTANCE));
            object.m_reachedFollowDistance = 1;
        }
        return !env.ObjectMoving(object.m_followId);
    }
    return false;
}

// ---- UpdateMoveTarget --------------------------------------------------------------------------------------------------------

namespace tile_flags {
constexpr uint32_t kShallow = 0x10; // wading depth: land route units turn back, sea vehicles stop
constexpr uint32_t kScenery = 0x8000;
constexpr uint32_t kBlocked = 0x80000000u;
} // namespace tile_flags

// Without a route the target is the walk point of the building being crossed, the followed object's position (behaviour 4) or the destination
// already stored. Bug fix: the walk point count is taken when the walk starts, but the player may demolish the building meanwhile and object
// ids are reused, so an unchecked read took another object (a bench, say) as a building: garbage walk points, crash or nondeterminism (found by
// the chaos bot). A walk point that no longer exists keeps the stored destination; with one it is the current route tile, shifted by the object's lane offset (m_posOffset 0 none, 1 the +x vector, anything
// else the -x vector). Then the route is checked against the terrain:
// - the tile flags say it is no longer usable (shallow and not blocked, or scenery, or the route already invalid), the object's own tile is
//   blocked or the route invalid, it does not follow a vehicle path and the route is not of type 4: the route is invalidated and released and
//   the command that made it (type 0 pavement, 1 land, 2 sea point, 3 swim; anything else leaves the type 0) goes to the object again with the
//   destination truncated to ints;
// - a vehicle path that meets a shallow tile drops its route and sends the object MT_MOVE2SEAPOINT again after 3 s.
template<class Env> void UpdateMoveTarget(object::Mover& object, Env& env)
{
    auto* cursor = object.m_pathCursor;
    if (!cursor) {
        if (object.m_walkInBuilding) {
            float point[2];
            if (env.BuildingWalkPoint(object.m_buildingId, object.m_currWalkPt, point)) {
                std::memcpy(&object.m_DestXYZ[0], &point[0], sizeof(float));
                std::memcpy(&object.m_DestXYZ[1], &point[1], sizeof(float));
            }
        } else if (object.m_behaviourState == 4) {
            float position[3];
            env.ObjectPosition(object.m_followId, position);
            std::memcpy(object.m_DestXYZ, position, sizeof(position));
        }
        SetMoveTarget(object, Bits(object.m_DestXYZ[0]), Bits(object.m_DestXYZ[1]));
        return;
    }

    const path::Route* data = cursor->m_route.get();
    const uint32_t* stepTile = path::StepTile(*cursor);
    const uint32_t tileX = stepTile[0], tileY = stepTile[1];
    const grid_math::PathPoint point = grid_math::TileToPathPoint(env.TileSize(), tileX, tileY);
    float target[2];
    switch (object.m_posOffset) {
    case 0:
        target[0] = point.x;
        target[1] = point.y;
        break;
    default: {
        float offset[2];
        env.PositionOffset(object.m_posOffset == 1 ? 1 : 2, offset);
        target[0] = ToFloat(D(offset[0]) + point.x);
        target[1] = ToFloat(D(offset[1]) + point.y);
    }
    }
    SetMoveTarget(object, Bits(target[0]), Bits(target[1]));

    using namespace tile_flags;
    const uint32_t flags = env.TileFlags(tileX, tileY);
    const bool shallow = (flags & kShallow) != 0;
    const bool routeUnusable = (shallow && !(flags & kBlocked)) || (flags & kScenery) != 0 || data->m_valid == 0;
    if (routeUnusable) {
        uint32_t own[2];
        env.WorldToTile(object.m_WorldXYZ[0], object.m_WorldXYZ[1], object.m_WorldXYZ[2], own);
        const uint32_t ownFlags = env.TileFlags(own[0], own[1]);
        const bool ownBlocked = (ownFlags & kBlocked) != 0 || data->m_valid == 0;
        if (ownBlocked && !object.m_vehiclePath && data->m_pathType != 4) {
            // Releasing the only cursor discards the unusable route.
            const int32_t type = data->m_pathType;
            env.ReleaseCursor(cursor);
            object.m_pathCursor = nullptr;
            Message message = MakeSelfMessage(object, 0);
            message.flags = FloatToIntWrap(D(object.m_DestXYZ[0]));
            message.data = FloatToIntWrap(D(object.m_DestXYZ[1]));
            switch (static_cast<uint32_t>(type)) {
            case 0: message.type = messages::MT_PAVEMENT2POINT; break;
            case 1: message.type = messages::MT_MOVE2POINT; break;
            case 2: message.type = messages::MT_MOVE2SEAPOINT; break;
            case 3: message.type = messages::MT_SWIM2SEAPOINT; break;
            default: break;
            }
            env.Post(message);
        }
    }
    if (object.m_vehiclePath && shallow) {
        env.ReleaseCursor(object.m_pathCursor);
        object.m_pathCursor = nullptr;
        Message message = MakeSelfMessage(object, messages::MT_MOVE2SEAPOINT);
        message.flags = FloatToIntWrap(D(object.m_DestXYZ[0]));
        message.data = FloatToIntWrap(D(object.m_DestXYZ[1]));
        env.PostDelayed(message, 3.0f);
    }
}

} // namespace obl::moving
