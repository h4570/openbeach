#pragma once

#include "game/ui/sprite.h"
#include "core/float_to_int.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "script_types.h"
#include "game/objects/mover_access.h"
#include "game/objects/script_program_exec.h"

#include <cstdint>
#include <cstring>

// The life cycle and the script variables of Mover. Construct (Mover's constructor, after the base object's),
// Destruct (Mover's destructor, before the base object's), Teleport, GetValue and SetValue.
// Pure logic: whatever leaves the object goes through an `Env` that the game side maps onto the game modules
// (src/game/objects/mover_lifecycle.cpp) and the unit test onto a fake.
//
// Env:
//   Construct:  int Rand   the game RNG
//   Destruct:   void ReleaseCursor(path::RouteCursor*)                releasing the route cursor
//   Teleport:   void ScenerySetPosition(Mover&, const uint32_t xyz[3])   placing scenery in the grid (list of grid cells, terrain height)
//   GetValue:   object::Base* Object(int32_t id)                 object lookup by id          void ServePosition(Entity* building, float out[2])
//               ScriptValue Base(Mover&, key, data)             base object GetValue
//   SetValue:   int32_t ToInt(int32_t type, int32_t bits)              script-to-integer conversion       void Base(Mover&, key, data, type, bits)
namespace obl::moving::lifecycle {

using object::Mover;
using script::ScriptValue;
namespace ex = script_program::exec;

// ---- Construct / Destruct --------------------------------------------------------------------------------------------------------------

// The fields Mover's constructor sets. The ones not listed here (`m_GroupID`, the building walk-point fields, `m_buildingId`,
// `m_buildingTraversal`) keep the zero of their member initializers.
// The top speed is a draw of the game RNG: (rand % 40 - 20) + 35, i.e. 15..54 (signed division, a negative draw would stay negative).
template<class Env> void Construct(Mover& moving, Env& env)
{
    moving.m_moving = 0;
    moving.m_mass = 5.0f;
    for (float& word : moving.m_vPosition) word = 0.0f;
    for (float& word : moving.m_vVelocity) word = 0.0f;
    for (float& word : moving.m_vSteerForce) word = 0.0f;
    moving.m_maxForce = 15.0f;
    const int draw = env.Rand();
    moving.m_maxVel = static_cast<float>(static_cast<double>(draw % 40 - 20) + 35.0);
    for (float& word : moving.m_vTarget) word = 0.0f;
    for (float& word : moving.m_DestXYZ) word = 0.0f;
    moving.m_behaviourState = 1;
    moving.m_controlDirection = 1;
    moving.m_DestObjectID = 0;
    moving.m_BuildingLeavingID = 0;
    moving.m_pathCursor = nullptr;
    moving.m_lastTimeRemaining = 0.0f;
    moving.m_enteredTheSea = 0;
    moving.m_insideBuilding = 0;
    moving.m_moveToEntrance = 0;
    moving.m_followObject = 0;
    moving.m_followDistance = 0;
    moving.m_walkInBuilding = 0;
    moving.m_posOffset = 0;
    moving.m_vehiclePath = 0;
    moving.m_lastDir = 0;
    moving.m_animTimeDiv2 = 0.0f;
    moving.m_waitingForMoveImHere = 0;
    moving.m_messageReceived = 0.0f;
    moving.m_turnType = 0;
    moving.m_reachedFollowDistance = 0;
    moving.m_followId = -1;
    moving.m_targetTolerance = 1.5f;
}

// The cursor of a route in progress is given back (the field keeps the dead pointer).
template<class Env> void Destruct(Mover& moving, Env& env)
{
    if (moving.m_pathCursor) env.ReleaseCursor(moving.m_pathCursor);
}

// ---- Teleport --------------------------------------------------------------------------------------------------------------------------

// A float copied by value: a signalling NaN comes out quiet (bit 22 set); every other pattern is kept.
inline uint32_t QuietNaN(uint32_t bits)
{
    return (bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) ? bits | 0x00400000u : bits;
}

// Update the movement position first, quieting NaN coordinates. Pass the unmodified coordinate bits to scenery placement,
// which moves the object between grid cell lists, stores its base position and samples terrain height.
template<class Env> void Teleport(Mover& moving, uint32_t x, uint32_t y, uint32_t z, Env& env)
{
    SetMovePosition(moving, QuietNaN(x), QuietNaN(y));
    const uint32_t xyz[3] = {x, y, z};
    env.ScenerySetPosition(moving, xyz);
}

// ---- GetValue / SetValue ---------------------------------------------------------------------------------------------------------------

constexpr int32_t kKeyDestDir = 0x54, kKeyMoving = 0xef, kKeyDestObject = 0xf0, kKeyDestX = 0xf1, kKeyDestY = 0xf2;

// The numbers Mover answers itself; every other one (also anything below 0x54 or above 0xf2 - the range check is unsigned) is the base
// class's, with the arguments unchanged.
inline bool HandlesGet(int32_t key)
{
    return key == kKeyDestDir || (key >= kKeyMoving && key <= kKeyDestY);
}
inline bool HandlesSet(int32_t key)
{
    return key == kKeyDestObject;
}

// The building's entrance point (x or y) as an int: the field of a building with an entrance, otherwise the serve point.
template<class Env> ScriptValue DestinationCoordinate(const Mover& moving, int axis, int32_t key, intptr_t data, Env& env)
{
    const object::Base* object = env.Object(moving.m_DestObjectID);
    // A missing object reads as "not a building".
    if (!object || object->m_type != object::ObjectType::Building) return ScriptValue{key, static_cast<int32_t>(data)};
    const auto* building = static_cast<const object::Building*>(object);
    float value;
    if (building->m_Entrance) {
        value = building->m_EntrancePoint[axis];
    } else {
        float point[2] = {0, 0};
        env.ServePosition(const_cast<object::Base*>(object), point);
        value = point[axis];
    }
    return ScriptValue{ex::kTypeInt, FloatToIntWrap(static_cast<double>(value))};
}

// `Moving` is a byte (the rest of the word is whatever the caller's `data` held), `DestObject` the id of the target, `DestX/DestY` the target
// building's entrance coordinate truncated to int (not a building: the call's own number and data come back), `DestDir` the direction field of
// the target's sprite instance.
template<class Env> ScriptValue GetValue(const Mover& moving, int32_t key, intptr_t data, Env& env)
{
    switch (key) {
    case kKeyDestDir: {
        const object::Base* object = env.Object(moving.m_DestObjectID);
        // A missing object (or one without a sprite) is tolerated and reads as 0.
        const auto* sprite = object ? static_cast<const ui::SpriteInstance*>(object->m_sprite) : nullptr;
        return ScriptValue{ex::kTypeInt, sprite ? static_cast<int32_t>(sprite->m_direction) : 0};
    }
    case kKeyMoving: return ScriptValue{ex::kTypeByte, static_cast<int32_t>((static_cast<uint32_t>(data) & 0xffffff00u) | moving.m_moving)};
    case kKeyDestObject: return ScriptValue{ex::kTypeInt, moving.m_DestObjectID};
    case kKeyDestX: return DestinationCoordinate(moving, 0, key, data, env);
    case kKeyDestY: return DestinationCoordinate(moving, 1, key, data, env);
    default: return env.Base(const_cast<Mover&>(moving), key, data);
    }
}

// Only `DestObject` is written, from whatever kind of value the script holds (script-to-integer conversion); the rest is the base class's.
template<class Env> void SetValue(Mover& moving, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    if (key == kKeyDestObject)
        moving.m_DestObjectID = env.ToInt(type, bits);
    else
        env.Base(moving, key, data, type, bits);
}

} // namespace obl::moving::lifecycle
