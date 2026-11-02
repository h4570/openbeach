#pragma once

#include "core/ascii_case.h"
#include "game/map/grid_cell.h"
#include "game/ui/sprite.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_vehicle.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// The life cycle of Vehicle. Construct (the vehicle key, returns this; Vehicle's constructor) and Destruct
// (Vehicle's destructor). Pure logic: whatever touches the game goes through an `Env` that the game side maps onto the game's services
// (vehicle_lifecycle.cpp) and the unit test onto a fake.
//
// Env:
//   Construct:  const tables::VehicleDef* Definition(const char* key)    the vehicle definition selected by key
//               void PlaceTowed(int32_t owner, const char* key)          place command: message {owner, -2, 0, 0, 4}, VehicleCreateParams
//                                                                        {key, 0, 0, 0, owner}; the rest of both records is zero
//               void* CreateSprite(const char* name)                     creates the named sprite instance from the global bank
//               void StartAnim(void* sprite, const char* anim, float startTime, const float* world)   starting the named sprite animation
//               float WorldTime                                        the world game time, the start time of the coach doors
//               int32_t PaletteCount(void* sprite) / void SetPaletteIndex(void* sprite, int32_t)   SpriteInstance -> Sprite::m_paletteCount / m_paletteIndex
//   Destruct:   void NeighbourCells(uint32_t x, uint32_t y, map::GridCell* out[8])   the eight neighbouring cells
//               map::GridCell* CellAt(uint32_t x, uint32_t y)         the wrapped tile index into the cell array
//               void ObjectTile(const Vehicle&, uint32_t out[2])   the tile under the vehicle's position
//               void StopLoop(int32_t objectId)                          stops sound loops owned by the object id
//               void ReleaseSprite(void*)                                releases the sprite through the global bank
namespace obl::vehicle::lifecycle {

using object::Vehicle;

// m_vehicleType. A key that matches none of the seven leaves the field at zero (Jetski), its member initializer.
enum Type : int32_t { Jetski = 0, SpeedBoat = 1, Ferry = 2, Banana = 3, Pedaloe = 4, Coach = 5, FishBoat = 6 };

constexpr uint32_t kBlockedFlag = 0x10; // GridCell::m_flags bit the vehicle's boat sets on the cells around it

namespace detail {

// Definition floats move as raw words, so a signalling NaN of the data would survive.
inline void CopyBits(float& to, const float& from)
{
    std::memcpy(&to, &from, sizeof(float));
}

// The key starts the definition's `m_Towed`; "NONE" in any case means nothing is towed.
inline bool HasTowed(const tables::VehicleDef& definition)
{
    return !ascii::EqualsNoCase(definition.m_Towed, "NONE");
}

} // namespace detail

// Vehicle's constructor runs it after Mover's (which draws once). The order is fixed (draws depend on it): definition, type (and the few
// fields a type sets), route counters, the towed vehicle, the physics from the definition, the sprites, then the zeroed state. Not written
// (they keep the zero of their member initializers):
// m_rideTime, m_ownerId, m_Guests, m_UnloadPoint, the turn area, the fishing point, the ferry fields and m_playingDoorAnim.
template<class Env> Vehicle* Construct(Vehicle& vehicle, const char* name, Env& env)
{
    const tables::VehicleDef* definition = env.Definition(name);
    vehicle.m_definition = definition;
    vehicle.m_waitingForDeath = 0;
    if (ascii::EqualsNoCase(name, "PEDALOE")) {
        vehicle.m_vehicleType = Pedaloe;
    } else if (ascii::EqualsNoCase(name, "JETSKIVEHICLE")) {
        vehicle.m_vehicleType = Jetski;
    } else if (ascii::EqualsNoCase(name, "BANANA")) {
        vehicle.m_vehicleType = Banana;
    } else if (ascii::EqualsNoCase(name, "SPDBOAT")) {
        vehicle.m_vehicleType = SpeedBoat;
        vehicle.m_targetTolerance = 12.0f;
    } else if (ascii::EqualsNoCase(name, "BOAT")) {
        vehicle.m_controlDirection = 0;
        vehicle.m_targetTolerance = 12.0f;
        vehicle.m_vehicleType = Ferry;
    } else if (ascii::EqualsNoCase(name, "BUS")) {
        vehicle.m_unloadDir = 5;
        vehicle.m_targetTolerance = 8.0f;
        vehicle.m_controlDirection = 0;
        vehicle.m_vehicleType = Coach;
    } else if (ascii::EqualsNoCase(name, "FISHBOAT")) {
        vehicle.m_vehicleType = FishBoat;
        vehicle.m_controlDirection = 0;
    }
    vehicle.m_numWayPts = 0;
    vehicle.m_currentWayPoint = 0;
    if (detail::HasTowed(*definition)) env.PlaceTowed(static_cast<int32_t>(vehicle.m_id), definition->m_Towed.c_str());
    detail::CopyBits(vehicle.m_maxForce, definition->m_Force);
    detail::CopyBits(vehicle.m_maxVel, definition->m_MaxSpeed);
    detail::CopyBits(vehicle.m_mass, definition->m_Mass);
    if (vehicle.m_vehicleType == Jetski) {
        void* wake = env.CreateSprite("jetski_sprite");
        vehicle.m_vehicleSprite = wake;
        env.StartAnim(wake, "d_wake_normal", 0.0f, vehicle.m_WorldXYZ);
        vehicle.m_targetTolerance = 10.0f;
    } else {
        vehicle.m_vehicleSprite = nullptr;
    }
    if (vehicle.m_vehicleType == FishBoat) {
        void* rod = env.CreateSprite("fishboat");
        vehicle.m_auxSprite1 = rod;
        env.StartAnim(rod, "rod01_bob", 0.0f, vehicle.m_WorldXYZ);
        rod = env.CreateSprite("fishboat");
        vehicle.m_auxSprite2 = rod;
        env.StartAnim(rod, "rod02_bob", 0.0f, vehicle.m_WorldXYZ);
        void* fisherman = env.CreateSprite("fishman");
        vehicle.m_auxSprite3 = fisherman;
        env.StartAnim(fisherman, "fish_driver", 0.0f, vehicle.m_WorldXYZ);
        // The last palette, but never past 0x2a.
        const int32_t palettes = env.PaletteCount(fisherman);
        env.SetPaletteIndex(fisherman, palettes < 0x2b ? palettes - 1 : 0x2a);
    } else {
        vehicle.m_auxSprite1 = nullptr;
        vehicle.m_auxSprite2 = nullptr;
        vehicle.m_auxSprite3 = nullptr;
    }
    vehicle.m_guestsToWaitFor = 0;
    vehicle.m_currCap = 0;
    vehicle.m_DestroyPoint = 0xff;
    vehicle.m_CurrentRoamPoint[0] = vehicle.m_CurrentRoamPoint[1] = vehicle.m_CurrentRoamPoint[2] = 0;
    vehicle.m_turnAnim = 0;
    vehicle.m_numTurns = 0;
    vehicle.m_turnCW = 0;
    vehicle.m_turnAnimStartTime = 0;
    vehicle.m_turnAnimLength = 0;
    vehicle.m_circuitPointCount = 0;
    vehicle.m_startCircuitPoint = 0;
    vehicle.m_stopCircuitPoint = 0;
    vehicle.m_currCircuitPoint = 0;
    vehicle.m_circuitPoints = nullptr;
    vehicle.m_waitForGuestToBoard = 0;
    vehicle.m_moveToTurnPoint = 0;
    vehicle.m_fishing = 0;
    vehicle.m_gotGrid = 0;
    vehicle.m_CurrGridXY[0] = 0xffffffffu;
    vehicle.m_CurrGridXY[1] = 0xffffffffu;
    if (vehicle.m_vehicleType == Coach) {
        void* doors = env.CreateSprite("Coach");
        vehicle.m_doorSprite = doors;
        env.StartAnim(doors, "coach_doors_closed", env.WorldTime(), vehicle.m_WorldXYZ);
    } else {
        vehicle.m_doorSprite = nullptr;
    }
    return &vehicle;
}

inline void ClearBlocked(map::GridCell* cell)
{
    cell->m_flags &= ~kBlockedFlag;
}

// The body of the destructor. A fishing boat gives back the eight cells around its current tile. A jetski or speed boat gives back its own tile (the
// one under its position, not its waypoint) and, by the unload direction, the two neighbours that
// the route kept blocked: directions 1 and 5 the neighbours 0 and 4, 3 and 7 the neighbours 2 and 6, the other directions none. The loop
// sound of the types 0, 1 and 5 stops and the sprites go back in their creation order; Mover's destructor follows.
template<class Env> void Destruct(Vehicle& vehicle, Env& env)
{
    map::GridCell* around[8];
    if (vehicle.m_vehicleType == FishBoat) {
        env.NeighbourCells(vehicle.m_CurrGridXY[0], vehicle.m_CurrGridXY[1], around);
        for (map::GridCell* cell : around) ClearBlocked(cell);
    }
    if (vehicle.m_vehicleType == SpeedBoat || vehicle.m_vehicleType == Jetski) {
        const uint32_t direction = vehicle.m_unloadDir;
        uint32_t tile[2];
        env.ObjectTile(vehicle, tile);
        env.NeighbourCells(tile[0], tile[1], around);
        ClearBlocked(env.CellAt(tile[0], tile[1]));
        if (direction == 1 || direction == 5) {
            ClearBlocked(around[0]);
            ClearBlocked(around[4]);
        } else if (direction == 3 || direction == 7) {
            ClearBlocked(around[2]);
            ClearBlocked(around[6]);
        }
    }
    const int32_t type = vehicle.m_vehicleType;
    if (type == Jetski || type == SpeedBoat || type == Coach) env.StopLoop(static_cast<int32_t>(vehicle.m_id));
    for (void* sprite : {vehicle.m_vehicleSprite, vehicle.m_doorSprite, vehicle.m_auxSprite1, vehicle.m_auxSprite2, vehicle.m_auxSprite3})
        if (sprite) env.ReleaseSprite(sprite);
}

} // namespace obl::vehicle::lifecycle
