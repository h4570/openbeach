#pragma once

#include "core/float_to_int.h"
#include "game/map/grid_math.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/mover_target.h"
#include "game/map/path_data.h"
#include "core/vec2.h"

#include <cstdint>
#include <cstring>

// Mover movement events (docs/architecture/objects.md): OnMoveTargetReached and Update. They extend the
// `Env` of mover_target.h (which Update drives) with:
//   float WorldDelta                                               World::m_gameDeltaTime
//   void Teleport(Mover&, float x, float y, float z)          the object's virtual Teleport
//   void SetSpriteDirection(void* sprite, float x, float y)          sprite direction from the movement vector of Entity::m_sprite
namespace obl::moving {

namespace tile_flags {
constexpr uint32_t kSea = 0x40000000; // deep enough to swim: a land route that steps on it reports MT_ENTERED_SEA
} // namespace tile_flags

constexpr float kEventDelay = 0.01f; // every delayed object message of OnMoveTargetReached
constexpr float kUpdateStep = 0.02f; // the fixed physics step of Update

// The object messages of OnMoveTargetReached carry the object's state in extra (all but the vehicle turn); a guest sends them through its
// own SendMessageDelayed slot (its script's OnSend runs first), everyone else through the delayed queue.
template<class Env> void SendSelfDelayed(object::Mover& object, const Message& message, Env& env)
{
    if (static_cast<uint8_t>(object.m_type) == kGuestType)
        env.SendDelayed(object, message, kEventDelay);
    else
        env.PostDelayed(message, kEventDelay);
}

template<class Env> void SendMoveImHere(object::Mover& object, Env& env)
{
    Message message = MakeSelfMessage(object, messages::MT_MOVEIMHERE);
    message.extra = object.m_state;
    // Unlike CompleteIfAtDestination, the two flags are cleared before the send.
    object.m_waitingForMoveImHere = 0;
    object.m_messageReceived = 0.0f;
    SendSelfDelayed(object, message, env);
}

// The x/y of a vector scaled by the game's reciprocal estimate of its table length (the "normalise" of Update's sprite facing and of the vehicle
// turn): x is the product kept in double precision and rounded by the store, y is rounded the same way.
inline vec2::Vec2 ApproxNormalise(float x, float y, double squared)
{
    const float length = path::ApproxSqrt(ToFloat(squared));
    const float inverse = vec2::ReciprocalEstimate(length);
    return {ToFloat(D(inverse) * x), ToFloat(D(y) * inverse)};
}

// ---- OnMoveTargetReached -----------------------------------------------------------------------------------------------------

// Called by Update when StepMotion reports the target reached.
// On a route: the cursor advances (path::Advance). Past the end (2) the route is released and nothing else happens. A sea vehicle with turn
// type 1 faces the new route tile in eight sectors and, when that differs from m_lastDir, stops and sends itself MT_VEHICLE_STOP_TURN with the
// new direction. The second-to-last tile (1) clears the lane offset and reports MT_LAST_POINT; any other tile that is sea (and not shallow) the
// first time reports MT_ENTERED_SEA with the object's truncated position.
// Without a route: inside a building the walk point index steps forward (or back for a traversal) and the walk ends after the last point (or
// below 0); a follower waits while the followed object still moves and then stops following; otherwise the move is over. Each end stops the
// object and reports MT_MOVEIMHERE.
template<class Env> void OnMoveTargetReached(object::Mover& object, Env& env)
{
    auto* cursor = object.m_pathCursor;
    if (cursor) {
        const int step = path::Advance(*cursor);
        if (step == 2) {
            env.ReleaseCursor(cursor);
            object.m_pathCursor = nullptr;
            return;
        }
        auto currentTile = [&] { return path::StepTile(*cursor); };
        if (object.m_vehiclePath && object.m_turnType == 1) {
            const uint32_t* tile = currentTile();
            const grid_math::PathPoint point = grid_math::TileToPathPoint(env.TileSize(), tile[0], tile[1]);
            const double dy = D(point.y) - object.m_vPosition[1];
            const float dx = ToFloat(D(point.x) - object.m_vPosition[0]);
            const vec2::Vec2 unit = ApproxNormalise(dx, ToFloat(dy), D(dx) * dx + dy * dy);
            const int direction = path::DirectionIndex(unit.x, unit.y, 8);
            if (object.m_lastDir != static_cast<uint32_t>(direction)) {
                object.m_moving = 0;
                Message message = MakeSelfMessage(object, messages::MT_VEHICLE_STOP_TURN);
                message.flags = direction;
                env.PostDelayed(message, kEventDelay);
            }
            object.m_lastDir = static_cast<uint32_t>(direction);
        }
        if (step == 1) {
            object.m_posOffset = 0;
            Message message = MakeSelfMessage(object, messages::MT_LAST_POINT);
            message.extra = object.m_state;
            SendSelfDelayed(object, message, env);
            return;
        }
        if (object.m_enteredTheSea) return;
        const uint32_t* tile = currentTile();
        const uint32_t flags = env.TileFlags(tile[0], tile[1]);
        if (!(flags & tile_flags::kSea) || (flags & tile_flags::kShallow)) return;
        object.m_enteredTheSea = 1;
        Message message = MakeSelfMessage(object, messages::MT_ENTERED_SEA);
        message.flags = FloatToIntWrap(D(object.m_WorldXYZ[0]));
        message.data = FloatToIntWrap(D(object.m_WorldXYZ[1]));
        message.extra = object.m_state;
        SendSelfDelayed(object, message, env);
        return;
    }

    if (object.m_walkInBuilding) {
        if (!object.m_buildingTraversal) {
            if (++object.m_currWalkPt != object.m_numWalkPts) return;
        } else if (static_cast<int32_t>(--object.m_currWalkPt) >= 0) {
            return;
        }
        object.m_lastTimeRemaining = 0.0f;
        object.m_moving = 0;
        object.m_walkInBuilding = 0;
    } else {
        object.m_lastTimeRemaining = 0.0f;
        if (object.m_followObject) {
            if (env.ObjectMoving(object.m_followId)) return;
            object.m_moving = 0;
            object.m_followObject = 0;
            object.m_followId = -1;
            object.m_behaviourState = 1;
        } else {
            object.m_moving = 0;
        }
    }
    object.m_vVelocity[0] = object.m_vVelocity[1] = 0.0f;
    object.m_vSteerForce[0] = object.m_vSteerForce[1] = 0.0f;
    SendMoveImHere(object, env);
}

// ---- Update ------------------------------------------------------------------------------------------------------------------

// A sea vehicle that smooth-turns (turn type 0, m_animTimeDiv2 > 0) asks for MT_VEHICLE_SMOOTH_TURN, synchronously, once the time left to the
// route's end tile at the current speed drops below m_animTimeDiv2 (a true division; no speed gives infinity or NaN, the latter counts as
// below), and clears the threshold.
template<class Env> void CheckSmoothTurn(object::Mover& object, Env& env)
{
    if (object.m_turnType != 0 || !object.m_vehiclePath || !(object.m_animTimeDiv2 > 0.0f) || !object.m_pathCursor) return;
    const path::Route* data = object.m_pathCursor->m_route.get();
    const grid_math::PathPoint end =
            grid_math::TileToPathPoint(env.TileSize(), static_cast<uint32_t>(data->m_stop[0]), static_cast<uint32_t>(data->m_stop[1]));
    const double dx = D(object.m_vPosition[0]) - end.x;
    const double dy = D(object.m_vPosition[1]) - end.y;
    const float distance = path::ApproxSqrt(ToFloat(dy * dy + dx * dx));
    const float vx = object.m_vVelocity[0], vy = object.m_vVelocity[1];
    const float speed = path::ApproxSqrt(ToFloat(D(vx) * vx + D(vy) * vy));
    if (D(distance) / speed >= object.m_animTimeDiv2) return;
    env.Post(MakeSelfMessage(object, messages::MT_VEHICLE_SMOOTH_TURN));
    object.m_animTimeDiv2 = 0.0f;
}

// Runs the movement of one frame in fixed 0.02 s steps: the world's frame time goes into m_lastTimeRemaining, and while that is above 0.02
// and the object still moves, one step is taken off and the target is refreshed, the physics stepped and, on arrival, OnMoveTargetReached
// called (which may stop the object or reset the remainder). The position is then clamped to the map (0..4096, 0..8192; below 0 includes NaN)
// and handed to the object's virtual Teleport with z = 0, and an object that controls its direction turns its sprite along the velocity.
// A stopped object does none of it. The first comparison uses the unrounded sum, the loop the stored float. Always returns true (the
// debug-break check, which clears Entity::m_debugBreak, never fails).
template<class Env> bool Update(object::Mover& object, Env& env)
{
    if (object.m_debugBreak) object.m_debugBreak = 0;
    if (!object.m_moving) return true;
    constexpr double kStep = static_cast<double>(kUpdateStep);
    const double sum = D(env.WorldDelta()) + object.m_lastTimeRemaining;
    object.m_lastTimeRemaining = ToFloat(sum);
    if (sum > kStep) {
        while (object.m_moving) {
            object.m_lastTimeRemaining = ToFloat(D(object.m_lastTimeRemaining) - kStep);
            UpdateMoveTarget(object, env);
            if (StepMotion(object, kUpdateStep, env)) OnMoveTargetReached(object, env);
            CheckSmoothTurn(object, env);
            if (!(D(object.m_lastTimeRemaining) > kStep)) break;
        }
    }
    float* position = object.m_vPosition;
    if (!(position[0] >= 0.0f)) position[0] = 0.0f;
    if (position[0] > 4096.0f) position[0] = 4096.0f;
    if (!(position[1] >= 0.0f)) position[1] = 0.0f;
    if (position[1] > 8192.0f) position[1] = 8192.0f;
    env.Teleport(object, position[0], position[1], 0.0f);
    if (object.m_moving && object.m_controlDirection) {
        const float vx = object.m_vVelocity[0], vy = object.m_vVelocity[1];
        const vec2::Vec2 facing = ApproxNormalise(vx, vy, D(vy) * vy + D(vx) * vx);
        env.SetSpriteDirection(object.m_sprite, facing.x, facing.y);
    }
    return true;
}

} // namespace obl::moving
