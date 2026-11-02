#pragma once

#include "core/char16.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_fields.h"

#include <cstdint>
#include <initializer_list>

// Vehicle (type 4): boats, the ferry and the other vehicles. The logic is vehicle_*.h. Names without Save labels are
// descriptive.
namespace obl::object {

struct Vehicle : object::Mover {
    Vehicle() = default;
    // Builds the vehicle of the given kind (vehicle_lifecycle.cpp).
    explicit Vehicle(const char* name);
    // Frees the cells it blocked, stops its loop sound and releases its sprites (vehicle_lifecycle.cpp).
    ~Vehicle() override;
    // The slots whose function differs from Mover's (the vehicle_*.cpp files).
    bool HasState(uint32_t state) override;
    bool OnEnterState(uint32_t state) override;
    bool Update() override;
    bool OnExitState() override;
    bool ReceiveMessage(messages::Message* message) override;
    void GetVariableText(const char* key, Char16* out) override;
    void Draw() override;
    bool IsSelectable() override;

    // The vehicle's own movement and animation helpers (vehicle_messages.cpp; logic in vehicle_helpers.h).
    void TurnToDirection(int32_t direction);
    void PlayTurnAnimation();
    void MoveToTurnArea();
    void MoveToNextCircuitPoint();
    void PickRoamPoint();
    bool PickFishingPoint();
    void TurnTowardsPoint(float x, float y);
    void PlayIdleAnimation();
    void PlayRideAnimation(float startTime);
    float PlayCatchAnimation();
    float PlayBoardingAnimation(float startTime, bool boarding, bool pair);
    void UpdateTurnAnimation(float now);
    // Appends a stop to the route; false when the route is full (vehicle.cpp).
    bool AddRouteStop(float x, float y, float z);
    // Messages and type tests that need no Env (definitions: vehicle_messages.h, vehicle_update.h).
    messages::Message StateChange(int32_t state) const;
    messages::Message MoveTo(int32_t type, uint32_t wayPoint) const;
    bool OnCircuit() const;
    bool CarriesCrowd() const;
    bool IsNamedBoat() const;

    static constexpr int kMaxGuests = 300;
    static constexpr int kMaxWayPoints = 64;

    const tables::VehicleDef* m_definition = nullptr; // the VEHICLE_<key> record (buildings read its m_Capacity)
    int32_t m_vehicleType = 0;
    void* m_vehicleSprite = nullptr;
    void* m_auxSprite1 = nullptr;
    void* m_auxSprite2 = nullptr;
    void* m_auxSprite3 = nullptr;
    int32_t m_Guests[kMaxGuests] = {};
    int32_t m_currCap = 0;
    float m_rideTime = 0;
    int32_t m_guestsToWaitFor = 0;
    int32_t m_ownerId = 0;
    uint8_t m_DestroyPoint = 0;
    uint8_t m_UnloadPoint = 0;
    int32_t m_unloadDir = 0;
    float m_WayPoints[kMaxWayPoints][3] = {};
    uint32_t m_numWayPts = 0;
    uint32_t m_currentWayPoint = 0;
    uint32_t m_TurnArea[2] = {};
    uint8_t m_moveToTurnPoint = 0;
    uint8_t m_fishing = 0;
    float m_CurrentRoamPoint[3] = {};
    float m_CurrentFishPoint[3] = {};
    uint8_t m_turnAnim = 0;
    uint8_t m_turnCW = 0;
    uint32_t m_numTurns = 0;
    float m_turnAnimStartTime = 0;
    float m_turnAnimLength = 0;
    uint32_t m_circuitPointCount = 0;
    uint32_t m_startCircuitPoint = 0;
    uint32_t m_stopCircuitPoint = 0;
    uint32_t m_currCircuitPoint = 0;
    void* m_circuitPoints = nullptr;
    uint8_t m_waitForGuestToBoard = 0;
    void* m_doorSprite = nullptr;
    uint8_t m_playingDoorAnim = 0;
    float m_FerryEntryPoint[3] = {};
    float m_FerryExitPoint[3] = {};
    float m_FerryEntryOffset[2] = {};
    float m_FerryExitOffset[2] = {};
    uint32_t m_ferryEntryDir = 0;
    uint32_t m_ferryExitDir = 0;
    uint8_t m_gotGrid = 0;
    uint32_t m_CurrGridXY[2] = {};
};

// The object manager's constructor of the type: builds a vehicle in `memory` (value-initialized; vehicle_lifecycle.cpp).

// The vehicle's own save records, then the circuit restore (Load) and Mover's (the caller). Visitor:
// Int/UInt/Float/Byte/Bool(key, field, hashed), Ints(key, int32_t*, count, hashed), Sprite(key, sprite) (the
// sprites present among vehicle, door and three auxiliary, their own records). `Self` is `Vehicle` or the const class.
template<SelfOf<Vehicle> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Ints("vehicle.guests", o.m_Guests, Vehicle::kMaxGuests, Hashed::No);
    v.Int("vehicle.guests_to_wait_for", o.m_guestsToWaitFor, Hashed::No);
    v.Int("vehicle.current_capacity", o.m_currCap, Hashed::Yes);
    v.Float("vehicle.ride_time", o.m_rideTime, Hashed::No);
    v.Int("vehicle.owner", o.m_ownerId, Hashed::Yes);
    v.Byte("vehicle.destroy_point", o.m_DestroyPoint, Hashed::No);
    v.Byte("vehicle.unload_point", o.m_UnloadPoint, Hashed::No);
    v.UInt("vehicle.unload_dir", o.m_unloadDir, Hashed::No);
    v.UInt("vehicle.turn_area.x", o.m_TurnArea[0], Hashed::No);
    v.UInt("vehicle.turn_area.y", o.m_TurnArea[1], Hashed::No);
    for (auto& point : o.m_WayPoints) {
        v.Float("vehicle.way_points[].x", point[0], Hashed::No);
        v.Float("vehicle.way_points[].y", point[1], Hashed::No);
        v.Float("vehicle.way_points[].z", point[2], Hashed::No);
    }
    v.UInt("vehicle.way_point_count", o.m_numWayPts, Hashed::Yes);
    v.UInt("vehicle.current_way_point", o.m_currentWayPoint, Hashed::Yes);
    v.Float("vehicle.roam_point.x", o.m_CurrentRoamPoint[0], Hashed::No);
    v.Float("vehicle.roam_point.y", o.m_CurrentRoamPoint[1], Hashed::No);
    v.Float("vehicle.roam_point.z", o.m_CurrentRoamPoint[2], Hashed::No);
    v.Float("vehicle.fish_point.x", o.m_CurrentFishPoint[0], Hashed::No);
    v.Float("vehicle.fish_point.y", o.m_CurrentFishPoint[1], Hashed::No);
    v.Float("vehicle.fish_point.z", o.m_CurrentFishPoint[2], Hashed::No);
    v.Bool("vehicle.move_to_turn_point", o.m_moveToTurnPoint, Hashed::No);
    v.Bool("vehicle.fishing", o.m_fishing, Hashed::No);
    v.Bool("vehicle.turn_anim", o.m_turnAnim, Hashed::No);
    v.Bool("vehicle.turn_cw", o.m_turnCW, Hashed::No);
    v.UInt("vehicle.turns", o.m_numTurns, Hashed::No);
    v.Float("vehicle.turn_anim_start_time", o.m_turnAnimStartTime, Hashed::No);
    v.Float("vehicle.turn_anim_length", o.m_turnAnimLength, Hashed::No);
    v.UInt("vehicle.start_circuit_point", o.m_startCircuitPoint, Hashed::No);
    v.UInt("vehicle.stop_circuit_point", o.m_stopCircuitPoint, Hashed::No);
    v.UInt("vehicle.current_circuit_point", o.m_currCircuitPoint, Hashed::No);
    for (void* sprite : {o.m_vehicleSprite, o.m_doorSprite, o.m_auxSprite1, o.m_auxSprite2, o.m_auxSprite3})
        if (sprite) v.Sprite("vehicle.sprites[]", sprite);
    v.UInt("vehicle.vehicle_type", o.m_vehicleType, Hashed::Yes);
    v.Bool("vehicle.wait_for_guest_to_board", o.m_waitForGuestToBoard, Hashed::No);
    v.Bool("vehicle.playing_door_anim", o.m_playingDoorAnim, Hashed::No);
    v.Float("vehicle.ferry_entry_point.x", o.m_FerryEntryPoint[0], Hashed::No);
    v.Float("vehicle.ferry_entry_point.y", o.m_FerryEntryPoint[1], Hashed::No);
    v.Float("vehicle.ferry_entry_point.z", o.m_FerryEntryPoint[2], Hashed::No);
    v.Float("vehicle.ferry_exit_point.x", o.m_FerryExitPoint[0], Hashed::No);
    v.Float("vehicle.ferry_exit_point.y", o.m_FerryExitPoint[1], Hashed::No);
    v.Float("vehicle.ferry_exit_point.z", o.m_FerryExitPoint[2], Hashed::No);
    v.Float("vehicle.ferry_entry_offset.x", o.m_FerryEntryOffset[0], Hashed::No);
    v.Float("vehicle.ferry_entry_offset.y", o.m_FerryEntryOffset[1], Hashed::No);
    v.Float("vehicle.ferry_exit_offset.x", o.m_FerryExitOffset[0], Hashed::No);
    v.Float("vehicle.ferry_exit_offset.y", o.m_FerryExitOffset[1], Hashed::No);
    v.UInt("vehicle.ferry_entry_dir", o.m_ferryEntryDir, Hashed::No);
    v.UInt("vehicle.ferry_exit_dir", o.m_ferryExitDir, Hashed::No);
    v.Bool("vehicle.got_grid", o.m_gotGrid, Hashed::No);
    v.UInt("vehicle.grid.x", o.m_CurrGridXY[0], Hashed::No);
    v.UInt("vehicle.grid.y", o.m_CurrGridXY[1], Hashed::No);
}

} // namespace obl::object
