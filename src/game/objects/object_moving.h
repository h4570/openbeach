#pragma once

#include "core/pointer_width.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_base.h"
#include "game/objects/object_fields.h"

#include <cstdint>

namespace obl::path {
struct RouteCursor;
}
namespace obl::script {
struct ScriptProgram;
}

// Mover and Person (docs/architecture/objects.md): the
// movement state every moving class shares (steering, destination, follow target, the route cursor of step 2, the building walk)
// and the three words guests and staff add. The logic is moving_*.h and person_methods.h. The field order and the 4-byte alignment give the sizes (0xf4, 0x100) that Guest and the
// staff layouts still extend.
namespace obl::object {

struct Mover : Base {
    Mover() = default;
    // Construction and teardown of the moving part (mover_lifecycle.cpp).
    explicit Mover(const char* name);
    ~Mover() override;
    // Moving objects override the shared object behavior for movement, routing and visibility.
    bool Update() override;
    bool ReceiveMessage(messages::Message* message) override;
    void Send(messages::Message message) override;
    void SendDelayed(messages::Message message, float delay) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    // Teleport updates movement position, base position and grid cell membership.
    virtual void Teleport(float x, float y, float z);

    // Whether the object is drawn in front of the building at its screen position (the GUI's selection asks).
    bool IsVisibleAgainstBuilding(int buildingId);

    float m_lastTimeRemaining = 0;
    uint8_t m_moving = 0;
    float m_mass = 0;
    float m_vPosition[2] = {};
    float m_vVelocity[2] = {};
    float m_vSteerForce[2] = {};
    float m_maxForce = 0;
    float m_maxVel = 0; // script variable WalkSpeed
    float m_vTarget[2] = {};
    float m_DestXYZ[3] = {};
    int32_t m_DestObjectID = 0;
    int32_t m_BuildingLeavingID = 0;
    uint32_t m_behaviourState = 0;
    int32_t m_GroupID = 0; // never written by the constructor
    uint8_t m_followObject = 0;
    int32_t m_followId = 0;
    uint32_t m_followDistance = 0;
    uint8_t m_reachedFollowDistance = 0;
    path::RouteCursor* m_pathCursor = nullptr; // owned, `new`-made (path_data.h); the Save label "on path" is its presence flag
    uint8_t m_walkInBuilding = 0;
    int32_t m_buildingId = 0; // never written by the constructor
    uint8_t m_buildingTraversal = 0; // never written by the constructor
    uint32_t m_numWalkPts = 0;
    uint32_t m_currWalkPt = 0;
    uint8_t m_enteredTheSea = 0;
    uint8_t m_insideBuilding = 0;
    uint8_t m_moveToEntrance = 0;
    uint8_t m_controlDirection = 0;
    uint8_t m_vehiclePath = 0;
    uint32_t m_posOffset = 0;
    uint32_t m_lastDir = 0;
    float m_animTimeDiv2 = 0;
    uint8_t m_waitingForMoveImHere = 0;
    float m_messageReceived = 0;
    uint32_t m_turnType = 0;
    float m_targetTolerance = 0;
};

// Sets the moving flag byte. Buildings call it on themselves too and write the same byte of their own record; the world hashes
// depend on that write, so it takes any object.
void SetMovingFlag(Base& object, bool moving);

// The human save only calls the moving object save: none of the three is saved or hashed.
struct Person : Mover {
    Person() = default;
    explicit Person(const char* name) : Mover(name) {}
    // The slots whose function differs from Mover's (person_methods.cpp).
    bool Update() override;
    bool ReceiveMessage(messages::Message* message) override;
    script::ScriptValue* GetValue(script::ScriptValue* out, int key, intptr_t data) override;
    void SetValue(int key, intptr_t data, int type, int bits) override;
    // Selects the object when the screen point hits its sprite.
    bool TrySelectAtScreenPoint(int32_t* screenPoint);

    const tables::GuestDef* m_definition = nullptr; // Guest: WorldParams guest record; the staff constructor does not set it
    script::ScriptProgram* m_script = nullptr; // logic of this object kind
    void* m_currentMessage = nullptr; // message handled right now; AlterPriceOpinion: current purchase record
};

// Performers derive from Mover but behave as Persons. The layouts agree up to the end of Mover, the only part
// the Person methods touch apart from its three words; call the methods qualified (`AsPerson(p).Person::Update()`)
// so the performer's own slots are not dispatched.
inline Person& AsPerson(Mover& object)
{
    return reinterpret_cast<Person&>(object);
}

// The moving object's save records, then Entity's (the caller). Visitor: Int/UInt/Float/Bool(key, field, hashed) and
// Route(key, cursor, hashed): the "on path" flag (1 while `cursor` is set) followed by the cursor's own records (path_data.h;
// the converter's `moving.path` group). The snapshot hashes the entries up to the route, not the time remaining.
template<SelfOf<Mover> Self, class Visitor> void VisitFields(Self& o, Visitor& v)
{
    v.Float("moving.last_time_remaining", o.m_lastTimeRemaining, Hashed::No);
    v.Bool("moving.moving", o.m_moving, Hashed::Yes);
    v.Float("moving.mass", o.m_mass, Hashed::Yes);
    v.Float("moving.position.x", o.m_vPosition[0], Hashed::Yes);
    v.Float("moving.position.y", o.m_vPosition[1], Hashed::Yes);
    v.Float("moving.velocity.x", o.m_vVelocity[0], Hashed::Yes);
    v.Float("moving.velocity.y", o.m_vVelocity[1], Hashed::Yes);
    v.Float("moving.steer_force.x", o.m_vSteerForce[0], Hashed::Yes);
    v.Float("moving.steer_force.y", o.m_vSteerForce[1], Hashed::Yes);
    v.Float("moving.max_force", o.m_maxForce, Hashed::Yes);
    v.Float("moving.max_velocity", o.m_maxVel, Hashed::Yes);
    v.Float("moving.target.x", o.m_vTarget[0], Hashed::Yes);
    v.Float("moving.target.y", o.m_vTarget[1], Hashed::Yes);
    v.Float("moving.destination.x", o.m_DestXYZ[0], Hashed::Yes);
    v.Float("moving.destination.y", o.m_DestXYZ[1], Hashed::Yes);
    v.Float("moving.destination.z", o.m_DestXYZ[2], Hashed::Yes);
    v.Int("moving.destination_object", o.m_DestObjectID, Hashed::Yes);
    v.Int("moving.leaving_building", o.m_BuildingLeavingID, Hashed::Yes);
    v.UInt("moving.behaviour", o.m_behaviourState, Hashed::Yes);
    v.Int("moving.group", o.m_GroupID, Hashed::No);
    v.Bool("moving.following", o.m_followObject, Hashed::No);
    v.Int("moving.follow_object", o.m_followId, Hashed::No);
    v.UInt("moving.follow_distance", o.m_followDistance, Hashed::No);
    v.Bool("moving.reached_follow_distance", o.m_reachedFollowDistance, Hashed::No);
    v.Route("moving.path", o.m_pathCursor, Hashed::Yes);
    v.Bool("moving.walk_in_building", o.m_walkInBuilding, Hashed::No);
    v.Int("moving.building", o.m_buildingId, Hashed::No);
    v.Bool("moving.building_traversal", o.m_buildingTraversal, Hashed::No);
    v.UInt("moving.walk_points", o.m_numWalkPts, Hashed::No);
    v.UInt("moving.current_walk_point", o.m_currWalkPt, Hashed::No);
    v.Bool("moving.entered_sea", o.m_enteredTheSea, Hashed::No);
    v.Bool("moving.inside_building", o.m_insideBuilding, Hashed::No);
    v.Bool("moving.move_to_entrance", o.m_moveToEntrance, Hashed::No);
    v.Bool("moving.control_direction", o.m_controlDirection, Hashed::No);
    v.Bool("moving.vehicle_path", o.m_vehiclePath, Hashed::No);
    v.UInt("moving.position_offset", o.m_posOffset, Hashed::No);
    v.UInt("moving.last_dir", o.m_lastDir, Hashed::No);
    v.Float("moving.anim_half_time", o.m_animTimeDiv2, Hashed::No);
    v.Bool("moving.waiting_for_arrival", o.m_waitingForMoveImHere, Hashed::No);
    v.Float("moving.message_received", o.m_messageReceived, Hashed::No);
    v.UInt("moving.turn_type", o.m_turnType, Hashed::No);
    v.Float("moving.target_tolerance", o.m_targetTolerance, Hashed::No);
}

} // namespace obl::object
