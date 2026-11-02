#pragma once

#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_vehicle.h"
#include <cstring>

namespace obl::vehicle {
inline bool HasState(uint32_t state)
{
    return state == 1 || (state >= 0x41 && state <= 0x4a);
}

template<class Env> void RestoreCircuit(object::Vehicle& vehicle, Env& env)
{
    if (vehicle.m_vehicleType != 0 && vehicle.m_vehicleType != 1) return;
    vehicle.m_circuitPointCount = env.CircuitPointCount(vehicle.m_vehicleType);
    vehicle.m_circuitPoints = env.CircuitPoints(vehicle.m_vehicleType);
}

// Save and Load: the class's field table (object::VisitFields, object_vehicle.h), the circuit restore (Load) and the base class.
// Env: the visitor of VisitFields, Circuit(Vehicle&), Base(Vehicle&).
template<class Env> void Fields(object::Vehicle& vehicle, Env& env)
{
    object::VisitFields(vehicle, env);
    env.Circuit(vehicle);
    env.Base(vehicle);
}
} // namespace obl::vehicle
