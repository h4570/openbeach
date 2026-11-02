// Vehicle::OnEnterState branches vehicle_messages_test.cpp does not reach (src/game/objects/vehicle_messages.h): idle at a circuit dock,
// return, unload, depart, ride, wait and remove. The fake env is a trimmed copy of the one in vehicle_messages_test.cpp (that file stays
// unchanged); it records a readable trace of every call.

#include "unit_test.h"
#include "game/objects/vehicle_messages.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/messages/message_state.h"

#include <cstring>
#include <string>
#include <vector>

namespace {
using obl::map::GridCell;
using obl::messages::Message;
using obl::object::Vehicle;
using obl::tables::VehicleDef;
namespace life = obl::vehicle::lifecycle;
namespace msg = obl::vehicle::messages;
using namespace obl::world_data;
using obl::messages::Message;
using enum obl::messages::MessageType;
using namespace obl::tables;

struct Sent {
    Message message;
    bool delayed;
    float delay;
};

struct StateEnv {
    std::vector<std::string> log;
    std::vector<Sent> sent;
    float time = 100.0f;
    float boardingLength = 1.25f, animLength = 2.5f, guestAnimLength = 1.5f;
    GridCell cells[8]{};
    GridCell dock{}; // CellAt / WrappedCell
    uint32_t dockDuringPost = 0xffffffffu;
    int32_t animType = -1;
    uint32_t objectTile[2] = {5, 6};
    uint32_t circuit[4][2] = {{9, 6}, {5, 7}, {20, 20}, {1, 1}};
    int32_t freeRooms = 3;
    bool pathCursor = false;
    bool cursorPoint = false;
    float cursor[3] = {7, 8, 9};

    void Post(const Message& m)
    {
        log.push_back("post " + std::to_string(m.type));
        dockDuringPost = dock.m_flags;
        sent.push_back({m, false, 0});
    }
    void PostDelayed(const Message& m, float delay)
    {
        log.push_back("delayed " + std::to_string(m.type));
        sent.push_back({m, true, delay});
    }
    float WorldTime() { return time; }
    void NeighbourCells(uint32_t, uint32_t, GridCell* out[8])
    {
        for (int i = 0; i < 8; ++i) out[i] = &cells[i];
    }
    void TurnToDirection(Vehicle&, int32_t direction) { log.push_back("turn " + std::to_string(direction)); }
    void PickRoamPoint(Vehicle&) { log.push_back("roam"); }
    float PlayBoardingAnimation(Vehicle&, float, bool boarding, bool pair)
    {
        log.push_back(std::string(boarding ? "board" : "leave") + (pair ? " pair" : " single"));
        return boardingLength;
    }
    void TeleportGuest(int32_t guest, const float*) { log.push_back("teleport " + std::to_string(guest)); }
    void PlaySound(const char* name, const float p[3])
    {
        log.push_back(std::string("sound ") + name + " z=" + std::to_string(static_cast<int>(p[2])));
    }
    void SetAnimType(void*, int32_t type) { animType = type; }
    float StartAnim(void*, const char* anim, float, const float*)
    {
        log.push_back(std::string("anim ") + anim);
        return animLength;
    }
    void PlayIdleAnimation(Vehicle&) { log.push_back("idle anim"); }
    void PlayRideAnimation(Vehicle&, float) { log.push_back("ride anim"); }
    void MoveToTurnArea(Vehicle&) { log.push_back("turn area"); }
    void TurnTowardsPoint(Vehicle&, float x, float y)
    {
        log.push_back("towards " + std::to_string(static_cast<int>(x)) + "," + std::to_string(static_cast<int>(y)));
    }
    void BlockDepartLane(uint32_t, uint32_t, int32_t) { log.push_back("block depart"); }
    void UnblockDepartLane(uint32_t, uint32_t, int32_t) { log.push_back("unblock depart"); }
    void BlockReturnLane(uint32_t, uint32_t, int32_t) { log.push_back("block return"); }
    void UnblockReturnLane(uint32_t, uint32_t, int32_t) { log.push_back("unblock return"); }
    uint32_t CircuitPointCount(int32_t) { return 4; }
    void* CircuitPoints(int32_t) { return circuit; }
    void TilePosition(const Vehicle&, uint32_t out[2])
    {
        out[0] = objectTile[0];
        out[1] = objectTile[1];
    }
    void ObjectTile(const Vehicle&, uint32_t out[2])
    {
        out[0] = objectTile[0];
        out[1] = objectTile[1];
    }
    void WorldPointToTile(float x, float y, uint32_t out[2])
    {
        out[0] = static_cast<uint32_t>(static_cast<int32_t>(x));
        out[1] = static_cast<uint32_t>(static_cast<int32_t>(y));
    }
    void TileToPathPoint(uint32_t x, uint32_t y, float out[3])
    {
        out[0] = static_cast<float>(x) * 10;
        out[1] = static_cast<float>(y) * 10;
        out[2] = 0;
    }
    void TileToWorldPoint(uint32_t x, uint32_t y, float out[3])
    {
        out[0] = static_cast<float>(x) * 10;
        out[1] = static_cast<float>(y) * 10;
        out[2] = 0;
    }
    void Neighbour(uint32_t x, uint32_t y, int32_t, uint32_t out[2]) { out[0] = x + 1, out[1] = y; }
    GridCell* CellAt(uint32_t, uint32_t) { return &dock; }
    GridCell* WrappedCell(uint32_t, uint32_t) { return &dock; }
    void SetDirection(void*, int32_t direction) { log.push_back("direction " + std::to_string(direction)); }
    void StartLoop(const char* name, int32_t id) { log.push_back(std::string("loop ") + name + " " + std::to_string(id)); }
    void StopLoop(int32_t id) { log.push_back("stop loop " + std::to_string(id)); }
    void SetMoving(Vehicle&, bool moving) { log.push_back(moving ? "moving" : "not moving"); }
    bool HasPathCursor(const Vehicle&) { return pathCursor; }
    bool PathCursorPoint(const Vehicle&, float out[3])
    {
        if (cursorPoint) std::memcpy(out, cursor, sizeof(cursor));
        return cursorPoint;
    }
    int32_t FreeRooms() { return freeRooms; }
    void NoRoomsTicker() { log.push_back("no rooms"); }
    void TurnGuestAround(int32_t guest) { log.push_back("turn around " + std::to_string(guest)); }
    void MarkGuestForRemoval(int32_t guest) { log.push_back("remove " + std::to_string(guest)); }
    float StartGuestAnim(int32_t, const char*, float, const float*) { return guestAnimLength; }

    std::string Trace() const
    {
        std::string out;
        for (const std::string& line : log) out += (out.empty() ? "" : "; ") + line;
        return out;
    }
    bool Blocked(const GridCell& cell) const { return (cell.m_flags & life::kBlockedFlag) != 0; }
};

void MakeVehicle(Vehicle& object, int32_t type, uint8_t moveType = 0, obl::tables::VehicleDef* def = nullptr)
{
    object.m_id = 7;
    object.m_ownerId = 3;
    object.m_vehicleType = type;
    object.m_numWayPts = 4;
    object.m_UnloadPoint = 2;
    object.m_currentWayPoint = 1;
    object.m_unloadDir = 1;
    for (int i = 0; i < 4; ++i) {
        object.m_WayPoints[i][0] = 100.9f + static_cast<float>(i);
        object.m_WayPoints[i][1] = 200.9f + static_cast<float>(i);
    }
    if (def) {
        def->m_MoveType = moveType;
        object.m_definition = def;
    }
}

bool Is(const Sent& s, int32_t receiver, int32_t type, int32_t flags, int32_t data)
{
    const Message& m = s.message;
    return m.sender == 7 && m.receiver == receiver && m.type == type && m.flags == flags && m.data == data;
}

std::string Num(int32_t value)
{
    return std::to_string(value);
}

} // namespace

OBL_TEST_CASE("vehicle_states.idle_at_a_circuit_dock_blocks_the_dock_and_the_cells_its_direction_keeps")
{
    for (int32_t direction : {1, 5, 3, 7, 0}) {
        StateEnv env;
        Vehicle object{};
        MakeVehicle(object, life::Jetski);
        object.m_unloadDir = direction;
        object.m_playingDoorAnim = 1;
        OBL_CHECK(msg::OnEnterState(object, msg::StateIdle, env));
        OBL_CHECK_EQ(env.Trace(), std::string("idle anim"));
        OBL_CHECK_EQ(object.m_playingDoorAnim, 0);
        OBL_CHECK(env.Blocked(env.dock));
        const bool vertical = direction == 1 || direction == 5, horizontal = direction == 3 || direction == 7;
        OBL_CHECK_EQ(env.Blocked(env.cells[0]), vertical);
        OBL_CHECK_EQ(env.Blocked(env.cells[4]), vertical);
        OBL_CHECK_EQ(env.Blocked(env.cells[2]), horizontal);
        OBL_CHECK_EQ(env.Blocked(env.cells[6]), horizontal);
        OBL_CHECK(!env.Blocked(env.cells[1]));
    }
}

OBL_TEST_CASE("vehicle_states.idle_off_circuit_touches_no_cells")
{
    StateEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Pedaloe);
    OBL_CHECK(msg::OnEnterState(object, msg::StateIdle, env));
    OBL_CHECK(!env.Blocked(env.dock));
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("vehicle_states.fishing_boat_returns_through_its_turn_area")
{
    StateEnv env;
    Vehicle object{};
    MakeVehicle(object, life::FishBoat);
    object.m_TurnArea[0] = 3, object.m_TurnArea[1] = 4;
    for (GridCell& cell : env.cells) cell.m_flags = life::kBlockedFlag;
    OBL_CHECK(msg::OnEnterState(object, msg::StateReturn, env));
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK(Is(env.sent[0], 7, MT_MOVE2SEAPOINT, 30, 40));
    OBL_CHECK_EQ(env.Trace(), "post " + Num(MT_MOVE2SEAPOINT) + "; towards 30,40");
    OBL_CHECK_EQ(object.m_moveToTurnPoint, 1);
    for (const GridCell& cell : env.cells) OBL_CHECK(env.Blocked(cell)); // unblocked for the move order, blocked again after

    // Already within 5 units of the turn area: no turn.
    StateEnv near;
    Vehicle close{};
    MakeVehicle(close, life::FishBoat);
    close.m_TurnArea[0] = 3, close.m_TurnArea[1] = 4;
    close.m_WorldXYZ[0] = 28.0f, close.m_WorldXYZ[1] = 37.0f;
    OBL_CHECK(msg::OnEnterState(close, msg::StateReturn, near));
    OBL_CHECK_EQ(near.Trace(), "post " + Num(MT_MOVE2SEAPOINT));
}

OBL_TEST_CASE("vehicle_states.crowd_vehicles_return_directly_to_the_unload_point")
{
    StateEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Ferry);
    OBL_CHECK(msg::OnEnterState(object, msg::StateReturn, env));
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK(Is(env.sent[0], 7, MT_MOVE_DIRECT_2POINT, 102, 202));
    OBL_CHECK(!env.Blocked(env.dock));
}

OBL_TEST_CASE("vehicle_states.circuit_vehicle_returns_with_the_lane_closed_and_takes_the_unload_point_as_roam_point")
{
    StateEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Jetski);
    OBL_CHECK(msg::OnEnterState(object, msg::StateReturn, env));
    OBL_CHECK_EQ(env.Trace(), "block return; post " + Num(MT_SWIM2SEAPOINT) + "; unblock return");
    OBL_CHECK(Is(env.sent[0], 7, MT_SWIM2SEAPOINT, 102, 202));
    OBL_CHECK(env.Blocked(env.dock));
    OBL_CHECK_EQ(object.m_CurrentRoamPoint[0], object.m_WayPoints[2][0]); // no path cursor: the unload point
    OBL_CHECK_EQ(object.m_CurrentRoamPoint[1], object.m_WayPoints[2][1]);
}

OBL_TEST_CASE("vehicle_states.pedalo_returns_from_a_free_cell_and_turns_to_the_path_point")
{
    StateEnv env;
    env.dock.m_flags = life::kBlockedFlag;
    env.cursorPoint = true;
    Vehicle object{};
    MakeVehicle(object, life::Pedaloe);
    OBL_CHECK(msg::OnEnterState(object, msg::StateReturn, env));
    OBL_CHECK_EQ(env.Trace(), "post " + Num(MT_SEAVEHICLE_MOVE2POINT) + "; towards 7,8");
    OBL_CHECK_EQ(env.dockDuringPost & life::kBlockedFlag, 0u); // its own cell must not block the path it asks for
    OBL_CHECK(env.Blocked(env.dock));
    OBL_CHECK_EQ(object.m_CurrentRoamPoint[2], 9.0f);
}

OBL_TEST_CASE("vehicle_states.ferry_unload_warns_about_full_hotels_and_hoots")
{
    StateEnv env;
    env.freeRooms = 0;
    Vehicle object{};
    MakeVehicle(object, life::Ferry);
    OBL_CHECK(msg::OnEnterState(object, msg::StateUnload, env));
    OBL_CHECK_EQ(env.Trace(), "no rooms; sound ferryhoot z=0; post " + Num(MT_STATECHANGE));
    OBL_CHECK(Is(env.sent[0], 7, MT_STATECHANGE, 0, msg::StateIdle)); // empty: straight back to idle
    OBL_CHECK_EQ(object.m_rideTime, 100.0f);
}

OBL_TEST_CASE("vehicle_states.coach_unload_opens_the_doors")
{
    StateEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Coach);
    object.m_unloadDir = 6;
    OBL_CHECK(msg::OnEnterState(object, msg::StateUnload, env));
    OBL_CHECK_EQ(env.Trace(), "direction 6; stop loop 7; sound doorhissopen z=0; anim coach_doors; post " + Num(MT_STATECHANGE));
    OBL_CHECK_EQ(env.animType, 2);
}

OBL_TEST_CASE("vehicle_states.unload_sends_the_last_guest_out_first")
{
    StateEnv env;
    Vehicle object{};
    MakeVehicle(object, life::SpeedBoat);
    object.m_currCap = 2;
    object.m_Guests[0] = 40, object.m_Guests[1] = 41;
    OBL_CHECK(msg::OnEnterState(object, msg::StateUnload, env));
    OBL_CHECK_EQ(env.Trace(), "stop loop 7; turn around 41; leave pair; delayed " + Num(MT_LEAVE_VEHICLE));
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK(env.sent[0].delayed);
    OBL_CHECK_EQ(env.sent[0].delay, 1.25f);
    OBL_CHECK(Is(env.sent[0], 41, MT_LEAVE_VEHICLE, 0, 0));

    StateEnv single;
    object.m_currCap = 1;
    OBL_CHECK(msg::OnEnterState(object, msg::StateUnload, single));
    OBL_CHECK_EQ(single.Trace(), "stop loop 7; turn around 40; leave single; delayed " + Num(MT_LEAVE_VEHICLE));
}

OBL_TEST_CASE("vehicle_states.coach_and_ferry_depart_to_their_current_way_point")
{
    StateEnv env;
    Vehicle coach{};
    MakeVehicle(coach, life::Coach);
    coach.m_unloadDir = 2;
    OBL_CHECK(msg::OnEnterState(coach, msg::StateDepart, env));
    OBL_CHECK_EQ(env.Trace(), "direction 2; sound coachrev z=0; loop coachloop 7; post " + Num(MT_MOVE_DIRECT_2POINT));
    OBL_CHECK(Is(env.sent[0], 7, MT_MOVE_DIRECT_2POINT, 101, 201));

    StateEnv ferryEnv;
    Vehicle ferry{};
    MakeVehicle(ferry, life::Ferry);
    OBL_CHECK(msg::OnEnterState(ferry, msg::StateDepart, ferryEnv));
    OBL_CHECK_EQ(ferryEnv.Trace(), "post " + Num(MT_MOVE_DIRECT_2POINT));
}

OBL_TEST_CASE("vehicle_states.jet_ski_departs_to_the_circuit_start_and_unloads_when_no_path_is_found")
{
    StateEnv env;
    obl::tables::VehicleDef def{};
    Vehicle object{};
    MakeVehicle(object, life::Jetski, 1, &def);
    object.m_circuitPoints = env.circuit;
    object.m_startCircuitPoint = 1;
    object.m_currCap = 1;
    object.m_Guests[0] = 40;
    env.dock.m_flags = life::kBlockedFlag;
    for (GridCell& cell : env.cells) cell.m_flags = life::kBlockedFlag;
    OBL_CHECK(msg::OnEnterState(object, msg::StateDepart, env));
    OBL_CHECK_EQ(env.Trace(),
            "ride anim; loop newjetskirev 7; block depart; post " + Num(MT_SWIM2SEAPOINT) + "; not moving; post " + Num(MT_STATECHANGE) +
                    "; unblock depart; teleport 40");
    OBL_REQUIRE(env.sent.size() == 2);
    OBL_CHECK(Is(env.sent[0], 7, MT_SWIM2SEAPOINT, 50, 70)); // circuit point 1 = tile (5, 7)
    OBL_CHECK(Is(env.sent[1], 7, MT_STATECHANGE, 0, msg::StateUnload));
    OBL_CHECK_EQ(object.m_currCircuitPoint, 1u);
    OBL_CHECK_EQ(env.dockDuringPost & life::kBlockedFlag, 0u);
    OBL_CHECK(!env.Blocked(env.dock)); // the dock cells stay free while the vehicle is out
    OBL_CHECK(!env.Blocked(env.cells[0]));
    OBL_CHECK(!env.Blocked(env.cells[4]));
    OBL_CHECK(env.Blocked(env.cells[2]));
}

OBL_TEST_CASE("vehicle_states.speed_boat_with_a_path_rides_out")
{
    StateEnv env;
    env.pathCursor = true;
    obl::tables::VehicleDef def{};
    Vehicle object{};
    MakeVehicle(object, life::SpeedBoat, 1, &def);
    object.m_circuitPoints = env.circuit;
    OBL_CHECK(msg::OnEnterState(object, msg::StateDepart, env));
    OBL_CHECK_EQ(env.Trace(), "ride anim; loop newjetskirevlow 7; block depart; post " + Num(MT_SWIM2SEAPOINT) + "; unblock depart");
    OBL_CHECK(Is(env.sent[0], 7, MT_SWIM2SEAPOINT, 90, 60)); // start point 0 = tile (9, 6)
}

OBL_TEST_CASE("vehicle_states.pedalo_and_fishing_boat_depart_block_their_cells_again")
{
    StateEnv env;
    env.pathCursor = true;
    env.cursorPoint = true;
    obl::tables::VehicleDef def{};
    Vehicle pedalo{};
    MakeVehicle(pedalo, life::Pedaloe, 1, &def);
    pedalo.m_circuitPoints = env.circuit;
    OBL_CHECK(msg::OnEnterState(pedalo, msg::StateDepart, env));
    OBL_CHECK_EQ(env.Trace(), "ride anim; block depart; post " + Num(MT_SWIM2SEAPOINT) + "; unblock depart; towards 7,8");
    OBL_CHECK(env.Blocked(env.dock));

    StateEnv fishEnv;
    fishEnv.pathCursor = true;
    Vehicle fish{};
    MakeVehicle(fish, life::FishBoat, 1, &def);
    fish.m_circuitPoints = fishEnv.circuit;
    fish.m_gotGrid = 1;
    OBL_CHECK(msg::OnEnterState(fish, msg::StateDepart, fishEnv));
    OBL_CHECK(!fishEnv.Blocked(fishEnv.dock));
    for (const GridCell& cell : fishEnv.cells) OBL_CHECK(fishEnv.Blocked(cell));
}

OBL_TEST_CASE("vehicle_states.boundary_movers_depart_to_the_way_point_and_others_post_nothing")
{
    StateEnv env;
    obl::tables::VehicleDef def{};
    Vehicle banana{};
    MakeVehicle(banana, life::Banana, 2, &def);
    OBL_CHECK(msg::OnEnterState(banana, msg::StateDepart, env));
    OBL_CHECK_EQ(env.Trace(), "ride anim; post " + Num(MT_MOVE2SEAPOINT));
    OBL_CHECK(Is(env.sent[0], 7, MT_MOVE2SEAPOINT, 101, 201));

    StateEnv none;
    Vehicle still{};
    MakeVehicle(still, life::Banana, 0, &def);
    OBL_CHECK(msg::OnEnterState(still, msg::StateDepart, none));
    OBL_CHECK_EQ(none.Trace(), std::string("ride anim"));
}

OBL_TEST_CASE("vehicle_states.ride_time_and_roaming")
{
    StateEnv env;
    Vehicle pedalo{};
    MakeVehicle(pedalo, life::Pedaloe);
    OBL_CHECK(msg::OnEnterState(pedalo, msg::StateRide, env));
    OBL_CHECK_EQ(env.Trace(), std::string("roam"));
    OBL_CHECK_EQ(pedalo.m_rideTime, 130.0f);
    OBL_CHECK_EQ(pedalo.m_UnloadPoint, 0u);

    StateEnv fishEnv;
    Vehicle fish{};
    MakeVehicle(fish, life::FishBoat);
    OBL_CHECK(msg::OnEnterState(fish, msg::StateRide, fishEnv));
    OBL_CHECK_EQ(fishEnv.Trace(), std::string("turn area"));
    OBL_CHECK_EQ(fish.m_rideTime, 107.0f);
}

OBL_TEST_CASE("vehicle_states.wait_and_remove")
{
    StateEnv env;
    Vehicle coach{};
    MakeVehicle(coach, life::Coach);
    OBL_CHECK(msg::OnEnterState(coach, msg::StateWait, env));
    coach.m_currCap = 2;
    coach.m_Guests[0] = 40, coach.m_Guests[1] = 41;
    OBL_CHECK(msg::OnEnterState(coach, msg::StateRemove, env));
    OBL_CHECK_EQ(env.Trace(), std::string("loop coachloop 7; remove 40; remove 41; stop loop 7"));

    StateEnv other;
    Vehicle ferry{};
    MakeVehicle(ferry, life::Ferry);
    OBL_CHECK(msg::OnEnterState(ferry, msg::StateWait, other));
    OBL_CHECK(msg::OnEnterState(ferry, msg::StateRemove, other));
    OBL_CHECK(other.log.empty());
}
