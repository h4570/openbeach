#pragma once

#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/map/path_data.h"
#include "core/vec2.h"

#include <cstdint>
#include <cstring>

// Mover steering and integration (group B of docs/architecture/objects.md): SteerSeek, SteerArrive,
// SteerFollow and IntegrateMotion. They are evaluated in double precision: stores to memory round to float, values that
// stay in registers stay unrounded. The comments mark which intermediates are kept ("ext") and which are stored as float; `double` stands
// in for the extended format (a single operation on float operands rounds identically in either, the chains can differ only at ~2^-53).
// Lengths come from the table square root (path::ApproxSqrt), reciprocals from the bit-trick estimate (vec2::ReciprocalEstimate)
// - neither is a real sqrt or division, and must not be "fixed".
namespace obl::moving {

using object::Mover;

inline float ToFloat(double value)
{
    return static_cast<float>(value);
}
inline double D(float value)
{
    return static_cast<double>(value);
}

// Shared tail of the three steering routines: the force is scaled down to m_maxForce when its length exceeds it. The scale is not a real
// division: d = float(length / maxForce) goes through the reciprocal estimate.
inline void ClampSteerForce(Mover& object)
{
    float* force = object.m_vSteerForce;
    const float lengthSquared = ToFloat(D(force[1]) * force[1] + D(force[0]) * force[0]);
    const float length = path::ApproxSqrt(lengthSquared);
    const float maxForce = object.m_maxForce;
    if (!(length > maxForce)) return; // also when unordered
    const float ratio = ToFloat(D(length) / maxForce);
    const float inverse = vec2::ReciprocalEstimate(ratio);
    const float x = force[0];
    const float y = force[1];
    force[0] = ToFloat(D(inverse) * x);
    force[1] = ToFloat(D(y) * inverse);
}

inline void SteerSeek(Mover& object)
{
    const float px = object.m_vPosition[0], py = object.m_vPosition[1];
    const float tx = object.m_vTarget[0], ty = object.m_vTarget[1];
    const float dx = ToFloat(D(tx) - px);
    const double dyExt = D(ty) - py;
    const float dy = ToFloat(dyExt);
    const float lengthSquared = ToFloat(dyExt * dyExt + D(dx) * dx); // dy^2 uses the unrounded difference
    const float length = path::ApproxSqrt(lengthSquared);
    if (!(length > 0.001f)) { // also when unordered
        object.m_vSteerForce[0] = object.m_vSteerForce[1] = 0.0f;
        return;
    }
    const float inverse = vec2::ReciprocalEstimate(length); // the second call of the reciprocal returns the same length
    const double directionX = D(inverse) * dx; // ext
    const float directionY = ToFloat(D(dy) * inverse); // stored
    const float maxVel = object.m_maxVel;
    const double desiredX = directionX * maxVel; // ext
    const float desiredY = ToFloat(D(directionY) * maxVel); // stored
    const float vx = object.m_vVelocity[0], vy = object.m_vVelocity[1];
    object.m_vSteerForce[0] = ToFloat(desiredX - vx);
    object.m_vSteerForce[1] = ToFloat(D(desiredY) - vy);
    ClampSteerForce(object);
}

// Arrive and Follow differ only in how the slowing distance is chosen; the speed they ask for is len / distance * maxVel (capped at maxVel).
inline void SteerToward(Mover& object, bool replaceZeroDistance)
{
    const float px = object.m_vPosition[0], py = object.m_vPosition[1];
    const float tx = object.m_vTarget[0], ty = object.m_vTarget[1];
    const float dx = ToFloat(D(tx) - px);
    const double dyExt = D(ty) - py;
    const float dy = ToFloat(dyExt);
    const float lengthSquared = ToFloat(dyExt * D(dy) + D(dx) * dx); // dy is multiplied by its rounded copy here
    const float length = path::ApproxSqrt(lengthSquared);
    const float maxVel = object.m_maxVel;
    double distance = static_cast<double>(static_cast<int32_t>(object.m_followDistance)); // a signed integer
    if (replaceZeroDistance && distance == 0.0) distance = D(maxVel) * 0.2f;
    double speed = (D(length) / distance) * maxVel;
    if (speed > D(maxVel)) speed = D(maxVel);
    const double scale = speed / length;
    const float vx = object.m_vVelocity[0], vy = object.m_vVelocity[1];
    const float desiredX = ToFloat(D(dx) * scale); // stored
    object.m_vSteerForce[0] = ToFloat(D(desiredX) - vx);
    object.m_vSteerForce[1] = ToFloat(scale * D(dy) - vy); // desired y stays unrounded
    ClampSteerForce(object);
}
inline void SteerArrive(Mover& object)
{
    SteerToward(object, true);
}
inline void SteerFollow(Mover& object)
{
    SteerToward(object, false);
}

// IntegrateMotion(dt): velocity += force / mass (the division is the reciprocal estimate), speed capped at m_maxVel with a real division
// by float(length / maxVel), then position += float(dt * velocity). A zero squared speed (or a length that is not positive) returns before
// the position moves.
inline void IntegrateMotion(Mover& object, float dt)
{
    const float inverseMass = vec2::ReciprocalEstimate(object.m_mass);
    const float accelerationX = ToFloat(D(inverseMass) * object.m_vSteerForce[0]);
    const float accelerationY = ToFloat(D(object.m_vSteerForce[1]) * inverseMass);
    float* velocity = object.m_vVelocity;
    const float oldVx = velocity[0], oldVy = velocity[1];
    velocity[0] = ToFloat(D(accelerationX) + oldVx);
    velocity[1] = ToFloat(D(accelerationY) + oldVy);

    const float speedSquared = ToFloat(D(velocity[1]) * velocity[1] + D(velocity[0]) * velocity[0]);
    uint32_t bits;
    std::memcpy(&bits, &speedSquared, sizeof(bits));
    if (bits == 0) return;
    const float speed = path::ApproxSqrt(speedSquared);
    if (!(speed > 0.0f)) return;
    const float maxVel = object.m_maxVel;
    if (!(maxVel >= speed)) { // also when unordered
        const float ratio = ToFloat(D(speed) / maxVel);
        const float newY = ToFloat(D(velocity[1]) / ratio);
        const float newX = ToFloat(D(velocity[0]) / ratio);
        velocity[0] = newX;
        velocity[1] = newY;
    }
    const vec2::Vec2 step = vec2::Multiply({velocity[0], velocity[1]}, dt);
    object.m_vPosition[0] = ToFloat(D(step.x) + object.m_vPosition[0]);
    object.m_vPosition[1] = ToFloat(D(step.y) + object.m_vPosition[1]);
}

} // namespace obl::moving
