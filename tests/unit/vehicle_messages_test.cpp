#include "unit_test.h"
#include "byte_copy.h"
#include "game/objects/vehicle_messages.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
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
namespace life = obl::vehicle::lifecycle;
namespace msg = obl::vehicle::messages;
using namespace obl::world_data;
using enum obl::messages::MessageType;

struct Sent {
    Message message;
    bool delayed;
    float delay;
};

// Records every call in order; `log` is the readable trace the tests compare.
struct FakeEnv {
    std::vector<std::string> log;
    std::vector<Sent> sent;
    float time = 100.0f;
    float boardingLength = 1.25f, catchLength = 0.75f, animLength = 2.5f;
    bool fishingPoint = false, baseAnswer = true;
    GridCell cells[8]{};
    uint32_t blockedDuringPost = 0xffffffffu;
    float servePosition[3] = {11, 22, 33};
    std::vector<float> guestPositions;
    std::vector<int32_t> guestsMoved;
    int32_t animType = -1;
    const float* animWorld = nullptr;

    bool BaseReceive(Vehicle&, const Message& m)
    {
        log.push_back("base " + std::to_string(m.type));
        return baseAnswer;
    }
    void Post(const Message& m)
    {
        log.push_back("post");
        blockedDuringPost = cells[0].m_flags;
        sent.push_back({m, false, 0});
    }
    void PostDelayed(const Message& m, float delay)
    {
        log.push_back("delayed");
        sent.push_back({m, true, delay});
    }
    float WorldTime() { return time; }
    void NeighbourCells(uint32_t x, uint32_t y, GridCell* out[8])
    {
        log.push_back("cells " + std::to_string(x) + "," + std::to_string(y));
        for (int i = 0; i < 8; ++i) out[i] = &cells[i];
    }
    void TurnToDirection(Vehicle&, int32_t direction) { log.push_back("turn " + std::to_string(direction)); }
    void MoveToNextCircuitPoint(Vehicle&) { log.push_back("circuit"); }
    void PickRoamPoint(Vehicle&) { log.push_back("roam"); }
    bool PickFishingPoint(Vehicle&)
    {
        log.push_back("fishing point");
        return fishingPoint;
    }
    float PlayCatchAnimation(Vehicle&)
    {
        log.push_back("catch");
        return catchLength;
    }
    float PlayBoardingAnimation(Vehicle&, float start, bool boarding, bool pair)
    {
        log.push_back(std::string(boarding ? "board" : "leave") + (pair ? " pair" : " single") + " at " +
                std::to_string(static_cast<int>(start)));
        return boardingLength;
    }
    void TeleportGuest(int32_t guest, const float p[3])
    {
        log.push_back("teleport");
        guestsMoved.push_back(guest);
        guestPositions.insert(guestPositions.end(), p, p + 3);
    }
    void SetGuestPosition(int32_t guest, float p[3])
    {
        log.push_back("set position");
        guestsMoved.push_back(guest);
        guestPositions.insert(guestPositions.end(), p, p + 3);
    }
    void OwnerServePosition(int32_t owner, float out[3])
    {
        log.push_back("serve position " + std::to_string(owner));
        std::memcpy(out, servePosition, sizeof(servePosition));
    }
    void PlaySound(const char* name, const float p[3])
    {
        log.push_back(std::string("sound ") + name + " z=" + std::to_string(static_cast<int>(p[2])));
    }
    void SetAnimType(void*, int32_t type) { animType = type; }
    float StartAnim(void*, const char* anim, float start, const float* world)
    {
        log.push_back(std::string("anim ") + anim + " at " + std::to_string(static_cast<int>(start)));
        animWorld = world;
        return animLength;
    }

    // OnEnterState only.
    uint32_t objectTile[2] = {5, 6};
    uint32_t circuit[4][2] = {{9, 6}, {5, 7}, {20, 20}, {1, 1}};
    uint32_t circuitCount = 4;
    int32_t freeRooms = 3;
    float guestAnimLength = 1.5f;
    bool pathCursor = false;

    void PlayIdleAnimation(Vehicle&) { log.push_back("idle anim"); }
    void PlayRideAnimation(Vehicle&, float start) { log.push_back("ride anim at " + std::to_string(static_cast<int>(start))); }
    void MoveToTurnArea(Vehicle&) { log.push_back("turn area"); }
    void TurnTowardsPoint(Vehicle&, float x, float y)
    {
        log.push_back("towards " + std::to_string(static_cast<int>(x)) + "," + std::to_string(static_cast<int>(y)));
    }
    void BlockDepartLane(uint32_t, uint32_t, int32_t) { log.push_back("block depart"); }
    void UnblockDepartLane(uint32_t, uint32_t, int32_t) { log.push_back("unblock depart"); }
    void BlockReturnLane(uint32_t, uint32_t, int32_t) { log.push_back("block return"); }
    void UnblockReturnLane(uint32_t, uint32_t, int32_t) { log.push_back("unblock return"); }
    uint32_t CircuitPointCount(int32_t) { return circuitCount; }
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
        out[0] = static_cast<uint32_t>(x);
        out[1] = static_cast<uint32_t>(y);
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
    void Neighbour(uint32_t x, uint32_t y, int32_t direction, uint32_t out[2])
    {
        log.push_back("neighbour " + std::to_string(direction));
        out[0] = x + 1;
        out[1] = y;
    }
    GridCell* CellAt(uint32_t, uint32_t) { return &cells[0]; }
    GridCell* WrappedCell(uint32_t, uint32_t) { return &cells[0]; }
    void SetDirection(void*, int32_t direction) { log.push_back("direction " + std::to_string(direction)); }
    void StartLoop(const char* name, int32_t id) { log.push_back(std::string("loop ") + name + " " + std::to_string(id)); }
    void StopLoop(int32_t id) { log.push_back("stop loop " + std::to_string(id)); }
    void SetMoving(Vehicle&, bool moving) { log.push_back(moving ? "moving" : "not moving"); }
    bool HasPathCursor(const Vehicle&) { return pathCursor; }
    bool PathCursorPoint(const Vehicle&, float[3]) { return false; }
    int32_t FreeRooms() { return freeRooms; }
    void NoRoomsTicker() { log.push_back("no rooms"); }
    void TurnGuestAround(int32_t guest) { log.push_back("turn around " + std::to_string(guest)); }
    void MarkGuestForRemoval(int32_t guest) { log.push_back("remove " + std::to_string(guest)); }
    float StartGuestAnim(int32_t guest, const char* anim, float start, const float*)
    {
        log.push_back("guest anim " + std::to_string(guest) + " " + anim + " at " + std::to_string(static_cast<int>(start)));
        return guestAnimLength;
    }
};

void MakeVehicle(Vehicle& object, int32_t type, int32_t state)
{
    object.m_id = 7;
    object.m_ownerId = 3;
    object.m_vehicleType = type;
    object.m_state = state;
    object.m_numWayPts = 4;
    object.m_UnloadPoint = 2;
    object.m_DestroyPoint = 0xff;
    for (int i = 0; i < 4; ++i) {
        object.m_WayPoints[i][0] = 100.9f + static_cast<float>(i);
        object.m_WayPoints[i][1] = -200.9f - static_cast<float>(i);
    }
}

Message In(int32_t type, int32_t sender = 50, int32_t flags = 0, int32_t data = 0)
{
    return msg::Make(sender, 7, type, flags, data);
}

bool Is(const Sent& s, int32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data)
{
    const Message& m = s.message;
    return m.sender == sender && m.receiver == receiver && m.type == type && m.flags == flags && m.data == data && m.extra == 0 &&
            m.floatData == 0;
}

uint8_t LowByte(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return static_cast<uint8_t>(bits);
}

constexpr uint8_t kSlot = 0x5c;
} // namespace

OBL_TEST_CASE("Vehicle messages: created answers 1, an unhandled type goes to the base, a smooth turn does nothing")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Ferry, 0x41);
    Vehicle before;
    obl::test::ByteCopy(before, object);
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_OBJECTCREATED), kSlot, env) == 1);
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_VEHICLE_SMOOTH_TURN), kSlot, env) == kSlot);
    OBL_CHECK(env.log.empty());
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_KILLOBJECT), kSlot, env) == 1);
    env.baseAnswer = false;
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_STATECHANGE), kSlot, env) == 0);
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_LEAVE_BUILDING), kSlot, env) == 0);
    OBL_CHECK((env.log == std::vector<std::string>{"base 0", "base 3", "base 107"}));
    OBL_CHECK(std::memcmp(&object, &before, sizeof(object)) == 0);
}

OBL_TEST_CASE("Vehicle messages: a stop turn turns every vehicle that is not on the circuit")
{
    for (int32_t type = 0; type <= 6; ++type) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, 0x41);
        OBL_CHECK(msg::ReceiveMessage(object, In(MT_VEHICLE_STOP_TURN, 50, 5), kSlot, env) == kSlot);
        if (type == life::Jetski || type == life::SpeedBoat)
            OBL_CHECK(env.log.empty());
        else
            OBL_CHECK((env.log == std::vector<std::string>{"turn 5"}));
    }
}

OBL_TEST_CASE("Vehicle messages: arriving on the way out takes the next way point, the unload point or the destroy point")
{
    // The next point is an ordinary one: a direct move for the coach and the ferry, a swim for the others; the coordinates are truncated.
    for (int32_t type : {life::Ferry, life::Coach, life::Pedaloe, life::FishBoat, life::Banana}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, msg::StateDepart);
        msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
        OBL_CHECK(object.m_currentWayPoint == 1);
        OBL_REQUIRE(env.sent.size() == 1);
        const bool direct = type == life::Ferry || type == life::Coach;
        OBL_CHECK(Is(env.sent[0], 7, 7, direct ? MT_MOVE_DIRECT_2POINT : MT_SWIM2SEAPOINT, 101, -201));
        OBL_CHECK(!env.sent[0].delayed);
    }
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Ferry, msg::StateDepart);
    object.m_currentWayPoint = 1;
    msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
    OBL_CHECK(object.m_currentWayPoint == 2);
    OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, msg::StateReturn));
    // The unload point wins when both are the same point; the index wraps.
    object.m_currentWayPoint = 3;
    object.m_UnloadPoint = 1;
    object.m_DestroyPoint = 0;
    msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
    OBL_CHECK(object.m_currentWayPoint == 0);
    OBL_CHECK(Is(env.sent[1], 7, 7, MT_STATECHANGE, 0, msg::StateRemove));
    object.m_currentWayPoint = 3;
    object.m_UnloadPoint = 0;
    msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
    OBL_CHECK(Is(env.sent[2], 7, 7, MT_STATECHANGE, 0, msg::StateReturn));
}

OBL_TEST_CASE("Vehicle messages: a circuit vehicle arriving on the way out asks for the next circuit point")
{
    for (int32_t type : {life::Jetski, life::SpeedBoat}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, msg::StateDepart);
        object.m_numWayPts = 0; // not read on this branch (a division by it would fault)
        OBL_CHECK(msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env) == kSlot);
        OBL_CHECK((env.log == std::vector<std::string>{"circuit"}));
        OBL_CHECK(object.m_currentWayPoint == 0);
    }
}

OBL_TEST_CASE("Vehicle messages: arriving back turns the small vehicles and unloads the others")
{
    for (int32_t type = 0; type <= 6; ++type) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, msg::StateReturn);
        object.m_currentWayPoint = 3;
        msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
        OBL_REQUIRE(env.sent.size() == 1);
        if (type == life::Pedaloe || type == life::Jetski || type == life::SpeedBoat) {
            OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, msg::StateTurn));
            OBL_CHECK(object.m_currentWayPoint == 3);
        } else {
            OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, msg::StateUnload));
            OBL_CHECK(object.m_currentWayPoint == 0);
        }
    }
}

OBL_TEST_CASE("Vehicle messages: a boat at its turn point frees its cells for the move order and blocks them again")
{
    FakeEnv env;
    for (GridCell& cell : env.cells) cell.m_flags = 0x60000010u;
    Vehicle object{};
    MakeVehicle(object, life::FishBoat, msg::StateReturn);
    object.m_moveToTurnPoint = 1;
    object.m_CurrGridXY[0] = 40;
    object.m_CurrGridXY[1] = 41;
    object.m_unloadDir = 6;
    msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
    OBL_CHECK((env.log == std::vector<std::string>{"cells 40,41", "post", "cells 40,41", "turn 6"}));
    OBL_CHECK(env.blockedDuringPost == 0x60000000u);
    for (const GridCell& cell : env.cells) OBL_CHECK(cell.m_flags == 0x60000010u);
    OBL_CHECK(Is(env.sent[0], 7, 7, MT_MOVE2SEAPOINT, 102, -202));
    OBL_CHECK(object.m_moveToTurnPoint == 0);
}

OBL_TEST_CASE("Vehicle messages: a ride ends when its time is over")
{
    for (int32_t type : {life::Jetski, life::SpeedBoat, life::Ferry, life::Banana, life::Pedaloe, life::Coach}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, msg::StateRide);
        object.m_rideTime = 100.0f; // not yet: the clock must be past it
        msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
        OBL_CHECK(env.sent.empty());
        object.m_rideTime = 99.5f;
        msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, msg::StateReturn));
        // The pedal boat picks a new roam point first, both times.
        if (type == life::Pedaloe)
            OBL_CHECK((env.log == std::vector<std::string>{"roam", "roam", "post"}));
        else
            OBL_CHECK((env.log == std::vector<std::string>{"post"}));
    }
}

OBL_TEST_CASE("Vehicle messages: the fishing boat alternates between a new fishing point and a catch")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::FishBoat, msg::StateRide);
    env.fishingPoint = true;
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env) == kSlot);
    OBL_CHECK(object.m_fishing == 1);
    OBL_CHECK(env.sent.empty());
    // At the fishing point: the catch, then back after twice its length; no new point is looked for.
    msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
    OBL_CHECK(object.m_fishing == 0);
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK(env.sent[0].delayed);
    OBL_CHECK(env.sent[0].delay == 1.5f);
    OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, msg::StateReturn));
    // No point found: the catch plays where the boat is.
    env.fishingPoint = false;
    msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env);
    OBL_CHECK(object.m_fishing == 0);
    OBL_CHECK(env.sent.size() == 2);
    OBL_CHECK((env.log == std::vector<std::string>{"fishing point", "catch", "delayed", "fishing point", "catch", "delayed"}));
}

OBL_TEST_CASE("Vehicle messages: arriving in another state does nothing")
{
    for (int32_t state : {1, 0x41, 0x43, 0x46, 0x47, 0x48, 0x49, 0x4a}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, life::Pedaloe, state);
        Vehicle before;
        obl::test::ByteCopy(before, object);
        OBL_CHECK(msg::ReceiveMessage(object, In(MT_MOVEIMHERE), kSlot, env) == kSlot);
        OBL_CHECK(env.log.empty());
        OBL_CHECK(std::memcmp(&object, &before, sizeof(object)) == 0);
    }
}

OBL_TEST_CASE("Vehicle messages: serve guest counts for the ferry and the coach and seats a guest elsewhere")
{
    for (int32_t type : {life::Ferry, life::Coach}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, 0x41);
        object.m_guestsToWaitFor = 2;
        OBL_CHECK(msg::ReceiveMessage(object, In(MT_SERVE_GUEST, 3, 0, 5), kSlot, env) == kSlot);
        OBL_CHECK(object.m_guestsToWaitFor == 7);
        OBL_CHECK(object.m_currCap == 0);
        OBL_CHECK(env.log.empty());
    }
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Jetski, 0x41);
    // The first guest boards at once (alone), the ride time is the end of its animation and the answer the low byte of the length.
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_SERVE_GUEST, 3, 0, 61), kSlot, env) == LowByte(1.25f));
    OBL_CHECK(object.m_Guests[0] == 61);
    OBL_CHECK(object.m_currCap == 1);
    OBL_CHECK(object.m_waitForGuestToBoard == 1);
    OBL_CHECK(object.m_rideTime == 101.25f);
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK(env.sent[0].delayed);
    OBL_CHECK(env.sent[0].delay == 1.25f);
    OBL_CHECK(Is(env.sent[0], 7, 61, MT_BOARD_VEHICLE, 0, 0));
    OBL_CHECK((env.log == std::vector<std::string>{"board single at 100", "delayed"}));
    // The second one only takes its seat while the first is boarding.
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_SERVE_GUEST, 3, 0, 62), kSlot, env) == kSlot);
    OBL_CHECK(object.m_Guests[1] == 62);
    OBL_CHECK(object.m_currCap == 2);
    OBL_CHECK(env.sent.size() == 1);
    // A guest that arrives with others already seated boards as a pair.
    object.m_waitForGuestToBoard = 0;
    msg::ReceiveMessage(object, In(MT_SERVE_GUEST, 3, 0, 63), kSlot, env);
    OBL_CHECK(env.log.back() == "delayed");
    OBL_CHECK(env.log[env.log.size() - 2] == "board pair at 100");
    OBL_CHECK(Is(env.sent[1], 7, 63, MT_BOARD_VEHICLE, 0, 0));
}

OBL_TEST_CASE("Vehicle messages: a guest that entered a small vehicle lets the second one board")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Pedaloe, 0x41);
    object.m_Guests[0] = 61;
    object.m_Guests[1] = 62;
    object.m_currCap = 2;
    object.m_waitForGuestToBoard = 1;
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_GUEST_ENTER_VEHICLE, 61), kSlot, env) == LowByte(1.25f));
    OBL_CHECK(object.m_waitForGuestToBoard == 0);
    OBL_CHECK(object.m_rideTime == 101.25f);
    OBL_CHECK(Is(env.sent[0], 7, 62, MT_BOARD_VEHICLE, 0, 0));
    OBL_CHECK(env.sent[0].delay == 1.25f);
    OBL_CHECK((env.log == std::vector<std::string>{"board pair at 100", "delayed"}));
    // The last one in: the ride time starts now.
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_GUEST_ENTER_VEHICLE, 62), kSlot, env) == kSlot);
    OBL_CHECK(object.m_rideTime == 100.0f);
    OBL_CHECK(env.sent.size() == 1);
    // A single guest: nothing more to board even while the flag is up.
    object.m_currCap = 1;
    object.m_waitForGuestToBoard = 1;
    object.m_rideTime = 0;
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_GUEST_ENTER_VEHICLE, 61), kSlot, env) == kSlot);
    OBL_CHECK(object.m_waitForGuestToBoard == 0);
    OBL_CHECK(object.m_rideTime == 100.0f);
    OBL_CHECK(env.sent.size() == 1);
}

OBL_TEST_CASE("Vehicle messages: the ferry and the coach collect their guests and leave when all are in")
{
    for (int32_t type : {life::Ferry, life::Coach}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, 0x41);
        object.m_guestsToWaitFor = 2;
        OBL_CHECK(msg::ReceiveMessage(object, In(MT_GUEST_ENTER_VEHICLE, 61), kSlot, env) == LowByte(1.25f));
        OBL_CHECK(object.m_Guests[0] == 61);
        OBL_CHECK(object.m_currCap == 1);
        OBL_CHECK(object.m_rideTime == 100.0f);
        OBL_CHECK(env.sent.empty());
        msg::ReceiveMessage(object, In(MT_GUEST_ENTER_VEHICLE, 62), kSlot, env);
        OBL_CHECK(object.m_Guests[1] == 62);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].delayed);
        OBL_CHECK(env.sent[0].delay == 1.25f);
        OBL_CHECK(Is(env.sent[0], 7, 7, MT_PLAYANIMATION, 0, 0));
        OBL_CHECK((env.log == std::vector<std::string>{"board pair at 100", "board pair at 100", "delayed"}));
    }
}

OBL_TEST_CASE("Vehicle messages: a guest leaving a small vehicle is put at the owner's serve position")
{
    for (int32_t type : {life::Jetski, life::SpeedBoat, life::Pedaloe, life::FishBoat, life::Banana}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, msg::StateUnload);
        object.m_Guests[0] = 61;
        object.m_Guests[1] = 62;
        object.m_currCap = 2;
        object.m_vVelocity[0] = object.m_vVelocity[1] = object.m_vSteerForce[0] = object.m_vSteerForce[1] = 4.0f;
        OBL_CHECK(msg::ReceiveMessage(object, In(MT_GUEST_LEFT_VEHICLE, 62), kSlot, env) == kSlot);
        OBL_CHECK(object.m_currCap == 1);
        OBL_CHECK(object.m_rideTime == 100.0f);
        OBL_CHECK((env.guestsMoved == std::vector<int32_t>{62}));
        OBL_CHECK((env.guestPositions == std::vector<float>{11, 22, 33}));
        // The next guest leaves half a second after the animation; the owner hears about the one that left.
        OBL_REQUIRE(env.sent.size() == 2);
        OBL_CHECK(env.sent[0].delayed);
        OBL_CHECK(env.sent[0].delay == 1.75f);
        OBL_CHECK(Is(env.sent[0], 7, 61, MT_LEAVE_VEHICLE, 0, 0));
        OBL_CHECK(!env.sent[1].delayed);
        OBL_CHECK(Is(env.sent[1], 62, 3, MT_GUEST_LEFT_VEHICLE, 0, 7));
        OBL_CHECK((env.log == std::vector<std::string>{"serve position 3", "teleport", "leave single at 100", "delayed", "post"}));
        const bool circuit = type == life::Jetski || type == life::SpeedBoat;
        OBL_CHECK(object.m_vVelocity[0] == (circuit ? 0.0f : 4.0f));
        OBL_CHECK(object.m_vSteerForce[1] == (circuit ? 0.0f : 4.0f));
        // The last one: the vehicle goes back to waiting.
        msg::ReceiveMessage(object, In(MT_GUEST_LEFT_VEHICLE, 61), kSlot, env);
        OBL_CHECK(object.m_currCap == 0);
        OBL_REQUIRE(env.sent.size() == 4);
        OBL_CHECK(Is(env.sent[2], 7, 7, MT_STATECHANGE, 0, msg::StateIdle));
        OBL_CHECK(Is(env.sent[3], 61, 3, MT_GUEST_LEFT_VEHICLE, 0, 7));
    }
}

OBL_TEST_CASE("Vehicle messages: a guest leaving the coach steps out beside the doors, one leaving the ferry stays where it is")
{
    struct Row {
        int32_t direction;
        float x, y;
    };
    for (const Row& row : {Row{7, 437.0f, 314.0f}, Row{5, 522.0f, 372.0f}, Row{1, 500.0f, 300.0f}}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, life::Coach, msg::StateUnload);
        object.m_WorldXYZ[0] = 500.0f;
        object.m_WorldXYZ[1] = 300.0f;
        object.m_WorldXYZ[2] = 9.0f;
        object.m_unloadDir = row.direction;
        object.m_Guests[0] = 61;
        object.m_Guests[1] = 62;
        object.m_Guests[2] = 63;
        object.m_currCap = 3;
        msg::ReceiveMessage(object, In(MT_GUEST_LEFT_VEHICLE, 99), kSlot, env);
        // The sender is moved, not the guest of the list.
        OBL_CHECK((env.guestsMoved == std::vector<int32_t>{99}));
        OBL_CHECK((env.guestPositions == std::vector<float>{row.x, row.y, 9.0f}));
        OBL_CHECK((env.log == std::vector<std::string>{"set position", "leave pair at 100", "delayed", "post"}));
        OBL_CHECK(env.sent[0].delay == 1.25f);
        OBL_CHECK(Is(env.sent[0], 7, 62, MT_LEAVE_VEHICLE, 0, 0));
        OBL_CHECK(Is(env.sent[1], 99, 3, MT_GUEST_LEFT_VEHICLE, 0, 7));
    }
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Ferry, msg::StateUnload);
    object.m_Guests[0] = 61;
    object.m_currCap = 1;
    msg::ReceiveMessage(object, In(MT_GUEST_LEFT_VEHICLE, 61), kSlot, env);
    OBL_CHECK(env.guestsMoved.empty());
    OBL_CHECK((env.log == std::vector<std::string>{"post", "post"}));
    OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, msg::StateIdle));
}

OBL_TEST_CASE("Vehicle messages: the play animation message departs, the coach closes its doors first")
{
    for (int32_t type : {life::Jetski, life::Ferry, life::FishBoat}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, 0x41);
        OBL_CHECK(msg::ReceiveMessage(object, In(MT_PLAYANIMATION), kSlot, env) == kSlot);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(!env.sent[0].delayed);
        OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, msg::StateDepart));
    }
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Coach, 0x41);
    object.m_WorldXYZ[2] = 9.0f;
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_PLAYANIMATION), kSlot, env) == LowByte(2.5f));
    OBL_CHECK(object.m_playingDoorAnim == 1);
    OBL_CHECK(env.animType == 1);
    OBL_CHECK(env.animWorld == object.m_WorldXYZ);
    OBL_CHECK((env.log == std::vector<std::string>{"sound doorhissopen z=0", "anim coach_doors at 100", "delayed"}));
    OBL_CHECK(env.sent[0].delay == 2.5f);
    OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, msg::StateDepart));
    // While the doors move the message is ignored.
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_PLAYANIMATION), kSlot, env) == kSlot);
    OBL_CHECK(env.sent.size() == 1);
}

OBL_TEST_CASE("Vehicle messages: the quality question and leaving the unload state tell the owner its guests are served")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Pedaloe, msg::StateUnload);
    OBL_CHECK(msg::ReceiveMessage(object, In(MT_COMMOD_QUALITY), kSlot, env) == kSlot);
    OBL_CHECK(msg::OnExitState(object, env));
    OBL_REQUIRE(env.sent.size() == 2);
    for (const Sent& sent : env.sent) {
        OBL_CHECK(!sent.delayed);
        OBL_CHECK(Is(sent, 7, 3, MT_GUEST_SERVED, 0, 0));
    }
    for (int32_t state : {1, 0x41, 0x42, 0x44, 0x45, 0x46, 0x48}) {
        object.m_state = state;
        OBL_CHECK(msg::OnExitState(object, env));
    }
    OBL_CHECK(env.sent.size() == 2);
}

OBL_TEST_CASE("Vehicle OnEnterState: a created ferry announces itself to the owner and goes idle")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Ferry, 1);
    OBL_CHECK(msg::OnEnterState(object, 1, env));
    OBL_CHECK((env.log == std::vector<std::string>{"post", "post"}));
    OBL_REQUIRE(env.sent.size() == 2);
    OBL_CHECK(Is(env.sent[0], 7, 3, MT_VEHICLE_CREATED, 0, 0));
    OBL_CHECK(Is(env.sent[1], 7, 7, MT_STATECHANGE, 0, 0x41));
    OBL_CHECK(object.m_circuitPoints == nullptr);
}

OBL_TEST_CASE("Vehicle OnEnterState: a created circuit vehicle starts at the nearest circuit point and unloads at the one before")
{
    for (int32_t type : {life::Jetski, life::SpeedBoat}) {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, type, 1);
        object.m_unloadDir = 3;
        OBL_CHECK(msg::OnEnterState(object, 1, env));
        OBL_CHECK(object.m_circuitPointCount == 4);
        OBL_CHECK(object.m_circuitPoints == static_cast<void*>(env.circuit));
        // From tile (5,6) the nearest point is {5,7}: index 1; the stop point is index 0, {9,6} -> path point (90,60).
        OBL_CHECK(object.m_startCircuitPoint == 1);
        OBL_CHECK(object.m_stopCircuitPoint == 0);
        OBL_CHECK(object.m_UnloadPoint == 0);
        OBL_CHECK(object.m_WayPoints[0][0] == 90.0f);
        OBL_CHECK(object.m_WayPoints[0][1] == 60.0f);
        OBL_CHECK(env.log.back() == "post");
        OBL_REQUIRE(env.sent.size() == 2);
        OBL_CHECK(Is(env.sent[0], 7, 3, MT_VEHICLE_CREATED, 0, 0));
        OBL_CHECK(Is(env.sent[1], 7, 7, MT_STATECHANGE, 0, 0x41));
        bool direction = false;
        for (const std::string& line : env.log) direction |= line == "direction 3";
        OBL_CHECK(direction);
    }
}

OBL_TEST_CASE("Vehicle OnEnterState: a created fishing boat finds its turn area three tiles to the left")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::FishBoat, 1);
    object.m_unloadDir = 3;
    OBL_CHECK(msg::OnEnterState(object, 1, env));
    OBL_CHECK((env.log == std::vector<std::string>{"neighbour 2", "neighbour 2", "neighbour 2", "post", "post"}));
    OBL_CHECK(object.m_TurnArea[0] == 8);
    OBL_CHECK(object.m_TurnArea[1] == 6);
    OBL_CHECK(object.m_circuitPoints == nullptr);
    OBL_CHECK(Is(env.sent[1], 7, 7, MT_STATECHANGE, 0, 0x41));
}

OBL_TEST_CASE("Vehicle OnEnterState: the turn state sets the velocity and turns to the unload direction")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::FishBoat, 0x46);
    object.m_unloadDir = 5;
    object.m_vVelocity[0] = 3;
    object.m_vVelocity[1] = 4;
    OBL_CHECK(msg::OnEnterState(object, 0x46, env));
    OBL_CHECK(object.m_vVelocity[0] == -0.5f);
    OBL_CHECK(object.m_vVelocity[1] == -0.5f);
    OBL_CHECK((env.log == std::vector<std::string>{"turn 5"}));
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("Vehicle OnEnterState: the jet ski driver falls off and the remount follows when the animation ends")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Jetski, 0x49);
    object.m_Guests[0] = 41;
    object.m_Guests[1] = 42;
    object.m_currCap = 1;
    object.m_WorldXYZ[0] = 1;
    OBL_CHECK(msg::OnEnterState(object, 0x49, env));
    OBL_CHECK((env.log ==
            std::vector<std::string>{"anim d_jetski_fall_off at 100", "teleport", "guest anim 41 d_jetski_fall_off at 100", "delayed"}));
    OBL_CHECK(env.animWorld == object.m_WorldXYZ);
    OBL_REQUIRE(env.guestsMoved.size() == 1);
    OBL_CHECK(env.guestsMoved[0] == 41);
    OBL_CHECK(env.guestPositions[0] == 1.0f);
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK(env.sent[0].delayed);
    OBL_CHECK(env.sent[0].delay == 1.5f);
    OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, 0x4a));
    OBL_CHECK(object.m_state == 0x49);
}

OBL_TEST_CASE("Vehicle OnEnterState: the remount plays the ride animation and sets the depart state number without a message")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Jetski, 0x4a);
    OBL_CHECK(msg::OnEnterState(object, 0x4a, env));
    OBL_CHECK((env.log == std::vector<std::string>{"ride anim at 100"}));
    OBL_CHECK(object.m_state == 0x44);
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("Vehicle OnEnterState: an unrecognized state does nothing")
{
    FakeEnv env;
    Vehicle object{};
    MakeVehicle(object, life::Ferry, 0x4b);
    Vehicle before;
    obl::test::ByteCopy(before, object);
    OBL_CHECK(msg::OnEnterState(object, 0x4b, env));
    OBL_CHECK(msg::OnEnterState(object, 0, env));
    OBL_CHECK(env.log.empty());
    OBL_CHECK(std::memcmp(&object, &before, sizeof(object)) == 0);
}

OBL_TEST_CASE("Vehicle OnEnterState: the ferry idles, heads for the unload point and departs for the next way point")
{
    {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, life::Ferry, 0x41);
        object.m_playingDoorAnim = 1;
        OBL_CHECK(msg::OnEnterState(object, 0x41, env));
        OBL_CHECK((env.log == std::vector<std::string>{"idle anim"}));
        OBL_CHECK(object.m_playingDoorAnim == 0);
    }
    {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, life::Ferry, 0x42);
        OBL_CHECK(msg::OnEnterState(object, 0x42, env));
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(Is(env.sent[0], 7, 7, MT_MOVE_DIRECT_2POINT, 102, -202));
    }
    {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, life::Ferry, 0x44);
        object.m_currentWayPoint = 1;
        OBL_CHECK(msg::OnEnterState(object, 0x44, env));
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(Is(env.sent[0], 7, 7, MT_MOVE_DIRECT_2POINT, 101, -201));
        OBL_CHECK(env.log.size() == 1);
    }
}

OBL_TEST_CASE("Vehicle OnEnterState: the ferry unloads its last guest first, hoots and complains when no rooms are free")
{
    {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, life::Ferry, 0x43);
        object.m_Guests[0] = 31;
        object.m_Guests[1] = 32;
        object.m_currCap = 2;
        object.m_rideTime = 5;
        OBL_CHECK(msg::OnEnterState(object, 0x43, env));
        OBL_CHECK((env.log == std::vector<std::string>{"sound ferryhoot z=0", "turn around 32", "leave pair at 100", "delayed"}));
        OBL_CHECK(object.m_rideTime == 100.0f);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].delayed);
        OBL_CHECK(env.sent[0].delay == 1.25f);
        OBL_CHECK(Is(env.sent[0], 7, 32, MT_LEAVE_VEHICLE, 0, 0));
    }
    {
        FakeEnv env;
        env.freeRooms = 0;
        Vehicle object{};
        MakeVehicle(object, life::Ferry, 0x43);
        object.m_Guests[0] = 31;
        object.m_currCap = 1;
        OBL_CHECK(msg::OnEnterState(object, 0x43, env));
        OBL_CHECK((env.log ==
                std::vector<std::string>{"no rooms", "sound ferryhoot z=0", "turn around 31", "leave single at 100", "delayed"}));
        OBL_CHECK(Is(env.sent[0], 7, 31, MT_LEAVE_VEHICLE, 0, 0));
    }
    {
        FakeEnv env;
        Vehicle object{};
        MakeVehicle(object, life::Ferry, 0x43);
        OBL_CHECK(msg::OnEnterState(object, 0x43, env));
        OBL_CHECK((env.log == std::vector<std::string>{"sound ferryhoot z=0", "post"}));
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(Is(env.sent[0], 7, 7, MT_STATECHANGE, 0, 0x41));
        OBL_CHECK(object.m_rideTime == 100.0f);
    }
}
