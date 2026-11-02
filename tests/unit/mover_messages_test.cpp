#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/mover_messages.h"
#include "game/objects/object_base.h"
#include "game/messages/message_state.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using namespace obl;
namespace {
using object::Mover;
using obl::messages::Message;

struct Holder {
    alignas(Mover) unsigned char bytes[sizeof(Mover)];
    Holder()
    {
        std::memset(bytes, 0, sizeof(bytes));
        obl::unit::StampVtable<Mover>(bytes);
    }
    Mover& Object() { return *reinterpret_cast<Mover*>(bytes); }
};

struct FakeEnv {
    struct Face {
        const object::Base* who;
        float from[3], to[3];
    };
    struct Sent {
        bool viaSlot;
        Message message;
        float delay;
    };
    object::Base* sender = nullptr;
    int32_t askedFor = -2;
    std::vector<Face> faces;
    uint32_t walkPoints = 0;
    int32_t askedBuilding = -2;
    float gameTime = 123.5f;
    std::vector<Sent> sent;
    // MT_FOLLOWOBJECT
    float servePosition[3] = {7.0f, 8.0f, 9.0f};
    float queueTailPosition[3] = {17.0f, 18.0f, 19.0f};
    std::vector<int32_t> askedIds;
    const object::Base* servedBuilding = nullptr;
    const object::Base* queuedBuilding = nullptr;
    float terrainHeight = 2.5f;
    std::vector<std::pair<int32_t, int32_t>> terrainAsked;
    std::vector<path::RouteCursor*> released;
    std::vector<std::array<float, 3>> mapped;
    struct LandCall {
        uint32_t sx, sy, tx, ty;
        bool foundIn;
        uint32_t option;
    };
    std::vector<LandCall> lands;
    path::RouteCursor* landResult = nullptr;
    std::vector<std::string> order;
    // MT_PATH_THROUGH_SCENERY[2OBJECT]
    std::vector<std::array<uint32_t, 2>> allowedAsked, blockedTiles;
    bool prepareResult = true;
    bool prepareMoves = false;
    std::vector<std::array<uint32_t, 4>> prepared, cleared;
    std::vector<uint32_t> masks;
    std::vector<LandCall> sceneryRoutes;
    path::RouteCursor* sceneryResult = nullptr;
    float approachPosition[3] = {27.0f, 28.0f, 29.0f};
    const object::Base* approachedScenery = nullptr;
    uint32_t tilePosition[2] = {3, 4};
    const object::Base* tileOf = nullptr;
    int randValue = 4;
    int randCalls = 0;
    // MT_MOVE2SEAPOINT / MT_SWIM2SEAPOINT / MT_SEAVEHICLE_MOVE2POINT: every tile is a sea tile unless listed (x, y, flags)
    uint32_t defaultFlags = 0x60000000u;
    std::vector<std::array<uint32_t, 3>> tileFlags;
    std::vector<std::array<uint32_t, 2>> flagsAsked, originsAsked;
    std::vector<LandCall> seaRoutes;
    path::RouteCursor* seaResult = nullptr;
    // MT_MOVE2POINT / MT_MOVE2OBJECT / MT_PAVEMENT2POINT / MT_PAVEMENT2OBJECT
    std::vector<int> allowedTypes;
    std::vector<LandCall> pavements;
    path::RouteCursor* pavementResult = nullptr;
    bool adjacent = false;
    std::vector<std::array<uint32_t, 4>> adjacentAsked;
    struct Posted {
        Message message;
        uint8_t waitingAtTheTime;
    };
    std::vector<Posted> posted;
    const Mover* watched = nullptr; // whose m_waitingForMoveImHere a post records

    object::Base* Object(int32_t id)
    {
        askedFor = id;
        askedIds.push_back(id);
        return sender;
    }
    float TerrainHeight(int32_t x, int32_t y)
    {
        terrainAsked.push_back({x, y});
        return terrainHeight;
    }
    uint32_t BuildingWalkPointCount(int32_t id)
    {
        askedBuilding = id;
        return walkPoints;
    }
    float GameTime() { return gameTime; }
    void ServePosition(const object::Base& building, float out[3])
    {
        servedBuilding = &building;
        std::memcpy(out, servePosition, sizeof(servePosition));
    }
    void QueueTailPosition(const object::Base& building, float out[3])
    {
        queuedBuilding = &building;
        std::memcpy(out, queueTailPosition, sizeof(queueTailPosition));
    }
    void ReleaseCursor(path::RouteCursor* cursor)
    {
        order.push_back("release");
        released.push_back(cursor);
    }
    void MapTile(const float (&world)[3], uint32_t out[2])
    {
        mapped.push_back({world[0], world[1], world[2]});
        out[0] = world[0] >= 0 ? static_cast<uint32_t>(world[0]) / 10 : 0; // false for NaN too
        out[1] = world[1] >= 0 ? static_cast<uint32_t>(world[1]) / 10 : 0;
    }
    path::RouteCursor* Land(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found, uint32_t option)
    {
        order.push_back("land");
        lands.push_back({sx, sy, tx, ty, *found, option});
        return landResult;
    }
    bool TileAllowed(uint32_t x, uint32_t y, int routeType)
    {
        order.push_back("allowed");
        allowedAsked.push_back({x, y});
        allowedTypes.push_back(routeType);
        for (const auto& tile : blockedTiles)
            if (tile[0] == x && tile[1] == y) return false;
        return true;
    }
    bool Prepare(uint32_t* start, uint32_t* stop, uint32_t mask)
    {
        order.push_back("prepare");
        prepared.push_back({start[0], start[1], stop[0], stop[1]});
        masks.push_back(mask);
        if (prepareMoves) {
            start[0] += 100;
            stop[1] += 200;
        }
        return prepareResult;
    }
    void Clear(uint32_t* start, uint32_t* stop, uint32_t mask)
    {
        order.push_back("clear");
        cleared.push_back({start[0], start[1], stop[0], stop[1]});
        masks.push_back(mask);
    }
    path::RouteCursor* Scenery(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found, uint32_t option)
    {
        order.push_back("scenery");
        sceneryRoutes.push_back({sx, sy, tx, ty, *found, option});
        return sceneryResult;
    }
    void ApproachPosition(const object::Base& scenery, float out[3])
    {
        approachedScenery = &scenery;
        std::memcpy(out, approachPosition, sizeof(approachPosition));
    }
    void TilePosition(const object::Base& object, uint32_t out[2])
    {
        tileOf = &object;
        out[0] = tilePosition[0];
        out[1] = tilePosition[1];
    }
    uint32_t TileFlags(uint32_t x, uint32_t y)
    {
        order.push_back("flags");
        flagsAsked.push_back({x, y});
        for (const auto& tile : tileFlags)
            if (tile[0] == x && tile[1] == y) return tile[2];
        return defaultFlags;
    }
    path::RouteCursor* Sea(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found)
    {
        order.push_back("sea");
        seaRoutes.push_back({sx, sy, tx, ty, *found, 0});
        return seaResult;
    }
    void TileOrigin(uint32_t x, uint32_t y, float out[2])
    {
        originsAsked.push_back({x, y});
        out[0] = static_cast<float>(x) * 16.0f;
        out[1] = static_cast<float>(y) * 32.0f;
    }
    path::RouteCursor* Pavement(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty, bool* found)
    {
        order.push_back("pavement");
        pavements.push_back({sx, sy, tx, ty, *found, 0});
        return pavementResult;
    }
    bool Adjacent(uint32_t sx, uint32_t sy, uint32_t tx, uint32_t ty)
    {
        order.push_back("adjacent");
        adjacentAsked.push_back({sx, sy, tx, ty});
        return adjacent;
    }
    void Post(const Message& m)
    {
        order.push_back("post");
        posted.push_back({m, watched ? watched->m_waitingForMoveImHere : uint8_t{0xff}});
    }
    int Rand()
    {
        order.push_back("rand");
        ++randCalls;
        return randValue;
    }
    void SendDelayed(Mover&, const Message& m, float delay) { sent.push_back({true, m, delay}); }
    void PostDelayed(const Message& m, float delay) { sent.push_back({false, m, delay}); }
    void FaceTowards(const object::Base& who, const float from[3], const float to[3])
    {
        Face face{&who, {}, {}};
        std::memcpy(face.from, from, sizeof(face.from));
        std::memcpy(face.to, to, sizeof(face.to));
        faces.push_back(face);
    }
    // entity::ReceiveMessage (the default path)
    std::vector<std::string> baseCalls;
    bool HasState(object::Base&, uint32_t)
    {
        baseCalls.push_back("has");
        return true;
    }
    bool ExitState(object::Base&)
    {
        baseCalls.push_back("exit");
        return true;
    }
    bool EnterState(object::Base&, uint32_t)
    {
        baseCalls.push_back("enter");
        return true;
    }
    void Fatal(entity::Fatal, const object::Base&, const Message&) { baseCalls.push_back("fatal"); }
};

Message Make(int type, int sender = 0)
{
    Message message{};
    message.type = type;
    message.sender = sender;
    return message;
}
} // namespace

OBL_TEST_CASE("moving_messages.handles_lists_exactly_the_ported_types")
{
    for (int type = -2; type < 120; ++type) {
        const bool expected = type == obl::messages::MT_MOVESTOP || type == obl::messages::MT_MOVE_CARRY_ON ||
                type == obl::messages::MT_STOP_AND_FACE_ME || type == obl::messages::MT_MOVE_INSIDE_BUILDING ||
                type == obl::messages::MT_FOLLOWOBJECT || type == obl::messages::MT_MOVE_DIRECT_2POINT ||
                type == obl::messages::MT_MOVE_DIRECT_2OBJECT || type == obl::messages::MT_PATH_THROUGH_SCENERY ||
                type == obl::messages::MT_PATH_THROUGH_SCENERY2OBJECT || type == obl::messages::MT_MOVE2SEAPOINT ||
                type == obl::messages::MT_SWIM2SEAPOINT || type == obl::messages::MT_SEAVEHICLE_MOVE2POINT ||
                type == obl::messages::MT_MOVE2POINT || type == obl::messages::MT_MOVE2OBJECT || type == obl::messages::MT_PAVEMENT2POINT ||
                type == obl::messages::MT_PAVEMENT2OBJECT;
        OBL_CHECK_EQ(moving::Handles(type), expected);
    }
}

OBL_TEST_CASE("moving_messages.stop_clears_moving_and_follow_and_returns_to_seek")
{
    Holder h;
    h.Object().m_moving = 1;
    h.Object().m_followObject = 1;
    h.Object().m_behaviourState = 4;
    h.Object().m_followId = 77;
    FakeEnv env;
    const auto result = moving::ReceiveMessage(h.Object(), Make(obl::messages::MT_MOVESTOP), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 0);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_followObject), 0);
    OBL_CHECK_EQ(h.Object().m_behaviourState, 1u);
    OBL_CHECK_EQ(h.Object().m_followId, 77); // MT_MOVESTOP keeps the follow id
}

OBL_TEST_CASE("moving_messages.carry_on_only_sets_moving")
{
    Holder h;
    h.Object().m_behaviourState = 3;
    h.Object().m_followObject = 1;
    FakeEnv env;
    const auto result = moving::ReceiveMessage(h.Object(), Make(obl::messages::MT_MOVE_CARRY_ON), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 1);
    OBL_CHECK_EQ(h.Object().m_behaviourState, 3u);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_followObject), 1);
}

OBL_TEST_CASE("moving_messages.face_me_stops_forgets_the_follow_target_and_turns_both_sprites")
{
    Holder h;
    Holder other;
    h.Object().m_moving = 1;
    h.Object().m_followId = 12;
    h.Object().m_WorldXYZ[0] = 1.0f;
    h.Object().m_WorldXYZ[1] = 2.0f;
    h.Object().m_WorldXYZ[2] = 3.0f;
    other.Object().m_WorldXYZ[0] = 10.0f;
    other.Object().m_WorldXYZ[1] = 20.0f;
    other.Object().m_WorldXYZ[2] = 30.0f;
    FakeEnv env;
    env.sender = &other.Object();
    const auto result = moving::ReceiveMessage(h.Object(), Make(obl::messages::MT_STOP_AND_FACE_ME, 4242), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(env.askedFor, 4242);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 0);
    OBL_CHECK_EQ(h.Object().m_followId, -1);
    OBL_REQUIRE(env.faces.size() == 2);
    // The sender's sprite first: (receiver position, sender position); then the receiver's: (sender position, receiver position).
    OBL_CHECK(env.faces[0].who == &other.Object());
    OBL_CHECK_EQ(env.faces[0].from[0], 1.0f);
    OBL_CHECK_EQ(env.faces[0].from[2], 3.0f);
    OBL_CHECK_EQ(env.faces[0].to[1], 20.0f);
    OBL_CHECK(env.faces[1].who == &h.Object());
    OBL_CHECK_EQ(env.faces[1].from[0], 10.0f);
    OBL_CHECK_EQ(env.faces[1].to[2], 3.0f);
}

OBL_TEST_CASE("moving_messages.face_me_keeps_an_unset_follow_id_and_survives_a_missing_sender")
{
    Holder h;
    h.Object().m_moving = 1;
    h.Object().m_followId = -1;
    FakeEnv env;
    const auto result = moving::ReceiveMessage(h.Object(), Make(obl::messages::MT_STOP_AND_FACE_ME, 9), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 0);
    OBL_CHECK(env.faces.empty());
}

OBL_TEST_CASE("moving_messages.positions_travel_as_raw_words")
{
    Holder h;
    Holder other;
    const uint32_t kNan = 0x7fa12345u; // a signalling NaN that a float load would quiet
    std::memcpy(&h.Object().m_WorldXYZ[0], &kNan, sizeof(kNan));
    FakeEnv env;
    env.sender = &other.Object();
    moving::ReceiveMessage(h.Object(), Make(obl::messages::MT_STOP_AND_FACE_ME), env);
    OBL_REQUIRE(env.faces.size() == 2);
    uint32_t bits;
    std::memcpy(&bits, &env.faces[0].from[0], sizeof(bits));
    OBL_CHECK_EQ(bits, kNan);
}

OBL_TEST_CASE("moving_messages.other_types_are_not_handled")
{
    Holder h;
    FakeEnv env;
    for (const int type :
            std::initializer_list<int>{obl::messages::MT_CANT_MOVE, obl::messages::MT_PATHBLOCKED, obl::messages::MT_KILLOBJECT, 99})
        OBL_CHECK(!moving::ReceiveMessage(h.Object(), Make(type), env).has_value());
}

OBL_TEST_CASE("moving_messages.default_path_ends_in_the_base_receiver")
{
    // The types without a case in the switch (7, 17..20, 22, 23, 25, 27), the ones outside it and the base types answer from the base object's ReceiveMessage.
    for (const int type : {0, 1, 2, 3, 4, 7, 17, 18, 19, 20, 22, 23, 25, 27, 30, 99}) {
        Holder h;
        FakeEnv env;
        Message message = Make(type);
        message.data = 8;
        const bool answered = moving::ReceiveWithDefault(h.Object(), message, env);
        OBL_CHECK_EQ(answered, type == obl::messages::MT_STATECHANGE || type == obl::messages::MT_SETOWNER);
        OBL_CHECK_EQ(env.baseCalls.size(), type == obl::messages::MT_STATECHANGE ? 3u : 0u);
        OBL_CHECK_EQ(env.randCalls, 0);
        OBL_CHECK(env.order.empty());
    }
}

OBL_TEST_CASE("moving_messages.families_do_not_reach_the_base_receiver")
{
    Holder h;
    FakeEnv env;
    OBL_CHECK(moving::ReceiveWithDefault(h.Object(), Make(obl::messages::MT_MOVESTOP), env));
    OBL_CHECK(env.baseCalls.empty());
}

// ---- MT_MOVE_INSIDE_BUILDING -----------------------------------------------------------------------------------------------------------

namespace {
Message Inside(int building, int flags)
{
    Message message = Make(obl::messages::MT_MOVE_INSIDE_BUILDING);
    message.data = building;
    message.flags = flags;
    return message;
}
} // namespace

OBL_TEST_CASE("moving_messages.inside_building_starts_a_forward_walk_at_point_zero")
{
    Holder h, building;
    building.Object().m_type = object::ObjectType::Building;
    FakeEnv env;
    env.sender = &building.Object();
    env.walkPoints = 5;
    const auto result = moving::ReceiveMessage(h.Object(), Inside(321, 0), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(env.askedBuilding, 321);
    OBL_CHECK_EQ(h.Object().m_buildingId, 321);
    OBL_CHECK_EQ(h.Object().m_numWalkPts, 5u);
    OBL_CHECK_EQ(h.Object().m_currWalkPt, 0u);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_buildingTraversal), 0);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 1);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_walkInBuilding), 1);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_waitingForMoveImHere), 1);
    OBL_CHECK_EQ(h.Object().m_messageReceived, 123.5f);
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("moving_messages.inside_building_walks_backwards_from_the_last_point_for_any_nonzero_flags")
{
    for (const int flags : {1, 2, -1, 0x7fffffff}) {
        Holder h, building;
        building.Object().m_type = object::ObjectType::Building;
        FakeEnv env;
        env.sender = &building.Object();
        env.walkPoints = 4;
        moving::ReceiveMessage(h.Object(), Inside(9, flags), env);
        OBL_CHECK_EQ(static_cast<int>(h.Object().m_buildingTraversal), 1);
        OBL_CHECK_EQ(h.Object().m_currWalkPt, 3u);
    }
}

OBL_TEST_CASE("moving_messages.inside_building_without_points_stops_the_object_and_reports_arrival")
{
    for (const bool guest : {false, true}) {
        Holder h;
        h.Object().m_id = 55;
        h.Object().m_state = 12;
        h.Object().m_type = guest ? object::ObjectType::Guest : object::ObjectType::Scenery;
        h.Object().m_vVelocity[0] = 3.0f;
        h.Object().m_vVelocity[1] = 4.0f;
        h.Object().m_vSteerForce[0] = 5.0f;
        h.Object().m_vSteerForce[1] = 6.0f;
        h.Object().m_moving = 1;
        h.Object().m_walkInBuilding = 0;
        FakeEnv env;
        env.walkPoints = 0;
        const auto result = moving::ReceiveMessage(h.Object(), Inside(77, 1), env);
        OBL_CHECK(result.has_value() && *result);
        OBL_CHECK_EQ(h.Object().m_buildingId, 77);
        OBL_CHECK_EQ(h.Object().m_numWalkPts, 0u);
        OBL_CHECK_EQ(h.Object().m_vVelocity[0], 0.0f);
        OBL_CHECK_EQ(h.Object().m_vVelocity[1], 0.0f);
        OBL_CHECK_EQ(h.Object().m_vSteerForce[0], 0.0f);
        OBL_CHECK_EQ(h.Object().m_vSteerForce[1], 0.0f);
        OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 1); // the walk never starts, nothing else is touched
        OBL_CHECK_EQ(static_cast<int>(h.Object().m_walkInBuilding), 0);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK_EQ(env.sent[0].viaSlot, guest);
        OBL_CHECK_EQ(env.sent[0].message.type, static_cast<int>(obl::messages::MT_MOVEIMHERE));
        OBL_CHECK_EQ(env.sent[0].message.sender, 55);
        OBL_CHECK_EQ(env.sent[0].message.receiver, 55);
        OBL_CHECK_EQ(env.sent[0].message.extra, 12);
        OBL_CHECK_EQ(env.sent[0].delay, 0.01f);
    }
}

OBL_TEST_CASE("moving_messages.inside_building_of_an_id_that_is_no_building_reports_arrival_without_asking_for_points")
{
    for (const bool exists : {false, true}) {
        Holder h, other;
        other.Object().m_type = object::ObjectType::Scenery;
        h.Object().m_id = 402;
        h.Object().m_type = object::ObjectType::Guest;
        h.Object().m_moving = 0;
        FakeEnv env;
        env.sender = exists ? &other.Object() : nullptr;
        env.walkPoints = 543574281; // what the garbage read gave in one run
        const auto result = moving::ReceiveMessage(h.Object(), Inside(18, 0), env);
        OBL_CHECK(result.has_value() && *result);
        OBL_CHECK_EQ(env.askedBuilding, -2);
        OBL_CHECK_EQ(h.Object().m_buildingId, 18);
        OBL_CHECK_EQ(h.Object().m_numWalkPts, 0u);
        OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 0);
        OBL_CHECK_EQ(static_cast<int>(h.Object().m_walkInBuilding), 0);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK_EQ(env.sent[0].message.type, static_cast<int>(obl::messages::MT_MOVEIMHERE));
    }
}

// ---- MT_FOLLOWOBJECT -------------------------------------------------------------------------------------------------------------------

namespace {
Message Follow(int target, int distance)
{
    Message message = Make(obl::messages::MT_FOLLOWOBJECT);
    message.data = target;
    message.flags = distance;
    return message;
}
} // namespace

OBL_TEST_CASE("moving_messages.follow_sets_the_follow_state_and_builds_a_land_route_between_the_two_tiles")
{
    Holder h;
    Holder target;
    auto& o = h.Object();
    o.m_WorldXYZ[0] = 120.0f;
    o.m_WorldXYZ[1] = 250.0f;
    o.m_WorldXYZ[2] = 3.0f;
    o.m_reachedFollowDistance = 1;
    o.m_behaviourState = 1;
    o.m_enteredTheSea = 1;
    o.m_followObject = 0;
    o.m_followId = -1;
    target.Object().m_WorldXYZ[0] = 480.0f;
    target.Object().m_WorldXYZ[1] = 90.0f;
    target.Object().m_WorldXYZ[2] = 5.0f;
    FakeEnv env;
    env.sender = &target.Object();
    int cursorMarker = 0;
    env.landResult = reinterpret_cast<path::RouteCursor*>(&cursorMarker);
    const auto result = moving::ReceiveMessage(o, Follow(1234, 6), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(env.askedFor, 1234);
    OBL_CHECK_EQ(static_cast<int>(o.m_reachedFollowDistance), 0);
    OBL_CHECK_EQ(o.m_behaviourState, 4u);
    OBL_CHECK_EQ(static_cast<int>(o.m_enteredTheSea), 0);
    OBL_CHECK_EQ(o.m_vPosition[0], 120.0f); // the move position follows the object's own
    OBL_CHECK_EQ(o.m_vPosition[1], 250.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[0], 480.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[1], 90.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[2], 5.0f);
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
    OBL_CHECK_EQ(o.m_followId, 1234);
    OBL_CHECK_EQ(static_cast<int>(o.m_followDistance), 6);
    OBL_CHECK_EQ(static_cast<int>(o.m_followObject), 1);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    OBL_CHECK_EQ(o.m_messageReceived, 123.5f);
    OBL_REQUIRE(env.lands.size() == 1);
    OBL_CHECK_EQ(env.lands[0].sx, 12u);
    OBL_CHECK_EQ(env.lands[0].sy, 25u);
    OBL_CHECK_EQ(env.lands[0].tx, 48u);
    OBL_CHECK_EQ(env.lands[0].ty, 9u);
    OBL_CHECK(env.lands[0].foundIn); // routeFound starts true
    OBL_CHECK_EQ(env.lands[0].option, 1u); // the option byte of this call site
    OBL_CHECK(o.m_pathCursor == env.landResult);
    OBL_CHECK(env.released.empty());
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("moving_messages.follow_stores_a_failed_route_as_null_and_keeps_the_distance_signed")
{
    Holder h;
    Holder target;
    int marker = 0;
    h.Object().m_pathCursor = reinterpret_cast<path::RouteCursor*>(&marker); // released first, then overwritten with the (null) result
    FakeEnv env;
    env.sender = &target.Object();
    env.landResult = nullptr;
    moving::ReceiveMessage(h.Object(), Follow(5, -20), env);
    OBL_CHECK(h.Object().m_pathCursor == nullptr);
    OBL_CHECK_EQ(static_cast<int>(h.Object().m_followDistance), -20);
    OBL_REQUIRE(env.released.size() == 1);
    OBL_CHECK(env.released[0] == reinterpret_cast<path::RouteCursor*>(&marker));
    OBL_REQUIRE(env.order.size() == 2);
    OBL_CHECK_EQ(env.order[0], std::string("release")); // the old route goes before the new one is built
    OBL_CHECK_EQ(env.order[1], std::string("land"));
}

OBL_TEST_CASE("moving_messages.follow_from_inside_a_building_steps_out_to_its_serve_position")
{
    Holder h;
    Holder target;
    auto& o = h.Object();
    o.m_insideBuilding = 1;
    o.m_DestObjectID = 808;
    o.m_BuildingLeavingID = 3;
    o.m_WorldXYZ[0] = 1.0f;
    o.m_WorldXYZ[1] = 2.0f;
    o.m_WorldXYZ[2] = 3.0f;
    FakeEnv env;
    env.sender = &target.Object();
    env.servePosition[0] = 300.0f;
    env.servePosition[1] = 410.0f;
    env.servePosition[2] = 0.5f;
    moving::ReceiveMessage(o, Follow(9, 0), env);
    OBL_REQUIRE(env.askedIds.size() == 2);
    OBL_CHECK_EQ(env.askedIds[0], 808); // the building first, then the followed object
    OBL_CHECK_EQ(env.askedIds[1], 9);
    OBL_CHECK(env.servedBuilding == &target.Object());
    OBL_CHECK_EQ(o.m_BuildingLeavingID, 808);
    OBL_CHECK_EQ(static_cast<int>(o.m_insideBuilding), 0);
    OBL_CHECK_EQ(o.m_WorldXYZ[0], 300.0f);
    OBL_CHECK_EQ(o.m_WorldXYZ[1], 410.0f);
    OBL_CHECK_EQ(o.m_WorldXYZ[2], 0.5f);
    OBL_CHECK_EQ(o.m_vPosition[0], 1.0f); // the move position was taken before the step out
    OBL_CHECK_EQ(o.m_vPosition[1], 2.0f);
    OBL_REQUIRE(env.mapped.size() == 2);
    OBL_CHECK_EQ(env.mapped[0][0], 300.0f); // the route starts at the serve position's tile
    OBL_CHECK_EQ(env.mapped[0][1], 410.0f);
}

OBL_TEST_CASE("moving_messages.follow_survives_a_missing_target_and_a_missing_building")
{
    Holder h;
    h.Object().m_insideBuilding = 1;
    h.Object().m_WorldXYZ[0] = 50.0f;
    h.Object().m_DestXYZ[0] = 9.0f;
    FakeEnv env;
    env.sender = nullptr;
    const auto result = moving::ReceiveMessage(h.Object(), Follow(77, 1), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(h.Object().m_WorldXYZ[0], 50.0f);
    OBL_CHECK_EQ(h.Object().m_DestXYZ[0], 0.0f);
    OBL_CHECK_EQ(h.Object().m_followId, 77);
}

OBL_TEST_CASE("moving_messages.follow_copies_positions_as_raw_words")
{
    Holder h;
    Holder target;
    const uint32_t kNan = 0x7fa12345u;
    std::memcpy(&target.Object().m_WorldXYZ[1], &kNan, sizeof(kNan));
    FakeEnv env;
    env.sender = &target.Object();
    moving::ReceiveMessage(h.Object(), Follow(2, 0), env);
    uint32_t bits;
    std::memcpy(&bits, &h.Object().m_DestXYZ[1], sizeof(bits));
    OBL_CHECK_EQ(bits, kNan);
}

// ---- MT_MOVE_DIRECT_2POINT / MT_MOVE_DIRECT_2OBJECT ------------------------------------------------------------------------------------------

namespace {
Message DirectPoint(int x, int y)
{
    Message message = Make(obl::messages::MT_MOVE_DIRECT_2POINT);
    message.flags = x;
    message.data = y;
    return message;
}
Message DirectObject(int id, int flags)
{
    Message message = Make(obl::messages::MT_MOVE_DIRECT_2OBJECT);
    message.data = id;
    message.flags = flags;
    return message;
}
} // namespace

OBL_TEST_CASE("moving_messages.direct_point_sets_the_destination_and_starts_a_walk_that_waits_for_the_arrival_message")
{
    Holder h;
    auto& o = h.Object();
    o.m_WorldXYZ[0] = 10.0f;
    o.m_WorldXYZ[1] = 20.0f;
    o.m_pathCursor = nullptr;
    FakeEnv env;
    const auto result = moving::ReceiveMessage(o, DirectPoint(300, 400), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(o.m_DestXYZ[0], 300.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[1], 400.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[2], 2.5f);
    OBL_CHECK_EQ(o.m_behaviourState, 1u);
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    OBL_CHECK_EQ(o.m_messageReceived, 123.5f);
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("moving_messages.direct_point_already_there_stops_and_reports_arrival")
{
    Holder h;
    auto& o = h.Object();
    o.m_id = 31;
    o.m_state = 9;
    o.m_WorldXYZ[0] = 300.05f;
    o.m_WorldXYZ[1] = 399.95f;
    o.m_moving = 1;
    o.m_vVelocity[0] = 4.0f;
    o.m_waitingForMoveImHere = 1;
    o.m_messageReceived = 5.0f;
    FakeEnv env;
    const auto result = moving::ReceiveMessage(o, DirectPoint(300, 400), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 0);
    OBL_CHECK_EQ(o.m_vVelocity[0], 0.0f);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 0);
    OBL_CHECK_EQ(o.m_messageReceived, 0.0f);
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK_EQ(env.sent[0].message.type, static_cast<int>(obl::messages::MT_MOVEIMHERE));
    OBL_CHECK_EQ(env.sent[0].message.extra, 9);
}

OBL_TEST_CASE("moving_messages.direct_object_already_at_the_old_destination_arrives_before_looking_at_the_target")
{
    Holder h;
    auto& o = h.Object();
    o.m_WorldXYZ[0] = 50.0f;
    o.m_WorldXYZ[1] = 60.0f;
    o.m_DestXYZ[0] = 50.05f;
    o.m_DestXYZ[1] = 60.0f;
    o.m_DestObjectID = 4;
    FakeEnv env;
    const auto result = moving::ReceiveMessage(o, DirectObject(1000, 1), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(o.m_DestObjectID, 4); // the message was not read
    OBL_CHECK(env.askedIds.empty());
    OBL_CHECK_EQ(env.sent.size(), 1u);
}

OBL_TEST_CASE("moving_messages.direct_object_picks_the_serve_queue_or_plain_position_by_the_target_class")
{
    struct Case {
        object::ObjectType type;
        int flags;
        float expected;
    };
    const Case cases[] = {{object::ObjectType::Building, 1, 7.0f}, {object::ObjectType::Building, -3, 7.0f},
            {object::ObjectType::Building, 0, 17.0f}, {object::ObjectType::Guest, 1, 4.0f}, {object::ObjectType::Scenery, 0, 4.0f}};
    for (const Case& c : cases) {
        Holder h;
        Holder target;
        target.Object().m_type = c.type;
        target.Object().m_WorldXYZ[0] = 4.0f;
        target.Object().m_WorldXYZ[1] = 5.0f;
        target.Object().m_WorldXYZ[2] = 6.0f;
        h.Object().m_DestXYZ[0] = 900.0f; // far from the object, which is at the origin
        h.Object().m_moving = 0;
        FakeEnv env;
        env.sender = &target.Object();
        const auto result = moving::ReceiveMessage(h.Object(), DirectObject(55, c.flags), env);
        OBL_CHECK(result.has_value() && *result);
        OBL_CHECK_EQ(env.askedFor, 55);
        OBL_CHECK_EQ(h.Object().m_DestObjectID, 55);
        OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 1);
        OBL_CHECK_EQ(h.Object().m_DestXYZ[0], c.expected);
        const bool building = c.type == object::ObjectType::Building;
        OBL_CHECK((env.servedBuilding != nullptr) == (building && c.flags != 0));
        OBL_CHECK((env.queuedBuilding != nullptr) == (building && c.flags == 0));
        OBL_CHECK_EQ(static_cast<int>(h.Object().m_waitingForMoveImHere), 0); // untouched
    }
}

OBL_TEST_CASE("moving_messages.direct_object_without_a_usable_target_leaves_the_destination_at_the_origin")
{
    for (const int id : {-1, 12}) {
        Holder h;
        h.Object().m_DestXYZ[0] = 900.0f;
        h.Object().m_DestXYZ[1] = 800.0f;
        h.Object().m_DestXYZ[2] = 700.0f;
        FakeEnv env;
        env.sender = nullptr;
        const auto result = moving::ReceiveMessage(h.Object(), DirectObject(id, 1), env);
        OBL_CHECK(result.has_value() && *result);
        OBL_CHECK_EQ(h.Object().m_DestObjectID, id);
        OBL_CHECK_EQ(h.Object().m_DestXYZ[0], 0.0f);
        OBL_CHECK_EQ(h.Object().m_DestXYZ[2], 0.0f);
        OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 1);
        OBL_CHECK_EQ(env.askedFor == -2 ? 1 : 0, id == -1 ? 1 : 0); // -1 is never looked up
    }
}

// ---- MT_PATH_THROUGH_SCENERY / MT_PATH_THROUGH_SCENERY2OBJECT ---------------------------------------------------------------------------

namespace {
Message SceneryPoint(int x, int y)
{
    Message message = Make(obl::messages::MT_PATH_THROUGH_SCENERY);
    message.flags = x;
    message.data = y;
    return message;
}
Message SceneryObject(int id, int flags)
{
    Message message = Make(obl::messages::MT_PATH_THROUGH_SCENERY2OBJECT);
    message.data = id;
    message.flags = flags;
    return message;
}
// The fake MapTile divides by ten: the object at (10, 20) is on tile (1, 2), a destination of (300, 400) on (30, 40).
void PlaceAt(Mover& o, float x, float y)
{
    o.m_WorldXYZ[0] = x;
    o.m_WorldXYZ[1] = y;
}
} // namespace

OBL_TEST_CASE("moving_messages.scenery_point_routes_between_the_two_tiles_and_waits_for_the_arrival")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    path::RouteCursor cursor{};
    FakeEnv env;
    env.sceneryResult = &cursor;
    const auto result = moving::ReceiveMessage(o, SceneryPoint(300, 400), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(o.m_DestXYZ[0], 300.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[2], 2.5f);
    OBL_CHECK_EQ(o.m_behaviourState, 1u);
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
    // The start tile is asked first; both pass, so the route is built straight away and nothing is prepared or cleaned.
    OBL_REQUIRE(env.allowedAsked.size() == 2);
    OBL_CHECK_EQ(env.allowedAsked[0][0], 1u);
    OBL_CHECK_EQ(env.allowedAsked[0][1], 2u);
    OBL_CHECK_EQ(env.allowedAsked[1][0], 30u);
    OBL_CHECK_EQ(env.allowedAsked[1][1], 40u);
    OBL_REQUIRE(env.sceneryRoutes.size() == 1);
    OBL_CHECK_EQ(env.sceneryRoutes[0].sx, 1u);
    OBL_CHECK_EQ(env.sceneryRoutes[0].sy, 2u);
    OBL_CHECK_EQ(env.sceneryRoutes[0].tx, 30u);
    OBL_CHECK_EQ(env.sceneryRoutes[0].ty, 40u);
    OBL_CHECK(env.sceneryRoutes[0].foundIn);
    OBL_CHECK_EQ(env.sceneryRoutes[0].option, 0u);
    OBL_CHECK(env.prepared.empty() && env.cleared.empty());
    OBL_CHECK(o.m_pathCursor == &cursor);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    OBL_CHECK_EQ(o.m_messageReceived, 123.5f);
    OBL_CHECK_EQ(env.randCalls, 0);
}

OBL_TEST_CASE("moving_messages.scenery_point_already_there_reports_arrival_without_a_route")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 300.05f, 399.95f);
    o.m_state = 9;
    FakeEnv env;
    const auto result = moving::ReceiveMessage(o, SceneryPoint(300, 400), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 0);
    OBL_CHECK(env.allowedAsked.empty() && env.sceneryRoutes.empty());
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 0);
    OBL_REQUIRE(env.sent.size() == 1);
    OBL_CHECK_EQ(env.sent[0].message.type, static_cast<int>(obl::messages::MT_MOVEIMHERE));
}

OBL_TEST_CASE("moving_messages.scenery_point_on_the_same_tile_builds_nothing_but_still_waits")
{
    for (const bool blocked : {false, true}) {
        Holder h;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        path::RouteCursor* const sentinel = reinterpret_cast<path::RouteCursor*>(0x1234);
        FakeEnv env;
        if (blocked) env.blockedTiles.push_back({1, 2});
        // The destination (12, 25) is on tile (1, 2) as well. SetDestinationFromMessage already dropped nothing (no old route here).
        const auto result = moving::ReceiveMessage(o, SceneryPoint(12, 25), env);
        OBL_CHECK(result.has_value() && *result);
        OBL_CHECK(env.sceneryRoutes.empty() && env.prepared.empty() && env.cleared.empty());
        OBL_CHECK(o.m_pathCursor == nullptr);
        OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
        OBL_CHECK_EQ(env.allowedAsked.size(), blocked ? 1u : 2u); // a blocked start tile is not followed by the stop test
        (void)sentinel;
    }
}

OBL_TEST_CASE("moving_messages.scenery_point_with_a_blocked_end_prepares_routes_with_the_corrected_ends_and_always_cleans_up")
{
    struct Case {
        std::vector<std::array<uint32_t, 2>> blocked;
        bool prepareOk, moves;
        size_t askedTiles;
    };
    const Case cases[] = {
            {{{1, 2}}, true, false, 1}, {{{30, 40}}, true, true, 2}, {{{30, 40}}, false, false, 2}, {{{1, 2}, {30, 40}}, false, true, 1}};
    for (const Case& c : cases) {
        Holder h;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        path::RouteCursor cursor{};
        FakeEnv env;
        env.blockedTiles = c.blocked;
        env.prepareResult = c.prepareOk;
        env.prepareMoves = c.moves;
        env.sceneryResult = &cursor;
        const auto result = moving::ReceiveMessage(o, SceneryPoint(300, 400), env);
        OBL_CHECK(result.has_value() && *result);
        OBL_CHECK_EQ(env.allowedAsked.size(), c.askedTiles);
        OBL_REQUIRE(env.prepared.size() == 1);
        OBL_CHECK_EQ(env.prepared[0][0], 1u);
        OBL_CHECK_EQ(env.prepared[0][1], 2u);
        OBL_CHECK_EQ(env.prepared[0][2], 30u);
        OBL_CHECK_EQ(env.prepared[0][3], 40u);
        OBL_CHECK_EQ(env.masks[0], 0x80000000u);
        const uint32_t startX = c.moves ? 101u : 1u, stopY = c.moves ? 240u : 40u; // what the helper wrote into the arrays
        if (c.prepareOk) {
            OBL_REQUIRE(env.sceneryRoutes.size() == 1);
            OBL_CHECK_EQ(env.sceneryRoutes[0].sx, startX);
            OBL_CHECK_EQ(env.sceneryRoutes[0].ty, stopY);
            OBL_CHECK(o.m_pathCursor == &cursor);
        } else {
            OBL_CHECK(env.sceneryRoutes.empty());
            OBL_CHECK(o.m_pathCursor == nullptr);
        }
        OBL_REQUIRE(env.cleared.size() == 1); // also after a failed helper
        OBL_CHECK_EQ(env.cleared[0][0], startX);
        OBL_CHECK_EQ(env.cleared[0][3], stopY);
        OBL_CHECK_EQ(env.masks[1], 0x80000000u);
        OBL_CHECK_EQ(env.order.back(), std::string("clear"));
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    }
}

OBL_TEST_CASE("moving_messages.scenery_point_from_inside_a_building_starts_at_its_serve_position")
{
    Holder h;
    Holder building;
    building.Object().m_type = object::ObjectType::Building;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    o.m_insideBuilding = 1;
    o.m_DestObjectID = 77; // SetDestinationFromMessage turns the building it is inside into the one it leaves
    FakeEnv env;
    env.sender = &building.Object();
    env.servePosition[0] = 55.0f;
    env.servePosition[1] = 66.0f;
    const auto result = moving::ReceiveMessage(o, SceneryPoint(300, 400), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_CHECK_EQ(env.askedFor, 77);
    OBL_CHECK(env.servedBuilding == &building.Object());
    OBL_REQUIRE(env.sceneryRoutes.size() == 1);
    OBL_CHECK_EQ(env.sceneryRoutes[0].sx, 5u); // the serve position's tile, not the object's
    OBL_CHECK_EQ(env.sceneryRoutes[0].sy, 6u);
    OBL_CHECK_EQ(o.m_WorldXYZ[0], 10.0f); // the object itself does not move
    OBL_CHECK_EQ(static_cast<int>(o.m_insideBuilding), 1);
}

OBL_TEST_CASE("moving_messages.scenery_point_survives_a_vanished_building")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    o.m_insideBuilding = 1;
    o.m_DestObjectID = 77;
    FakeEnv env;
    env.sender = nullptr;
    const auto result = moving::ReceiveMessage(o, SceneryPoint(300, 400), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_REQUIRE(env.sceneryRoutes.size() == 1);
    OBL_CHECK_EQ(env.sceneryRoutes[0].sx, 1u); // the object's own tile stands in
}

OBL_TEST_CASE("moving_messages.scenery_object_begins_like_a_move_command_and_picks_the_target_spot_by_its_class")
{
    struct Case {
        object::ObjectType type;
        int flags;
        float expectedX; // the destination's x
    };
    const Case cases[] = {{object::ObjectType::Building, 1, 7.0f}, {object::ObjectType::Building, 0, 17.0f},
            {object::ObjectType::Scenery, 1, 27.0f}, {object::ObjectType::Scenery, 0, 27.0f}, {object::ObjectType::Guest, 1, 400.0f},
            {object::ObjectType::Vehicle, 0, 400.0f}};
    for (const Case& c : cases) {
        Holder h;
        Holder target;
        target.Object().m_type = c.type;
        target.Object().m_WorldXYZ[0] = 400.0f;
        target.Object().m_WorldXYZ[1] = 500.0f;
        target.Object().m_WorldXYZ[2] = 6.0f;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        o.m_DestXYZ[0] = 900.0f;
        o.m_moving = 0;
        o.m_behaviourState = 4;
        FakeEnv env;
        env.sender = &target.Object();
        env.sceneryResult = reinterpret_cast<path::RouteCursor*>(0x4321);
        const auto result = moving::ReceiveMessage(o, SceneryObject(55, c.flags), env);
        OBL_CHECK(result.has_value() && *result);
        OBL_CHECK_EQ(o.m_behaviourState, 1u);
        OBL_CHECK_EQ(o.m_DestObjectID, 55);
        OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
        OBL_CHECK_EQ(o.m_DestXYZ[0], c.expectedX);
        const bool building = c.type == object::ObjectType::Building, scenery = c.type == object::ObjectType::Scenery;
        OBL_CHECK((env.servedBuilding != nullptr) == (building && c.flags != 0));
        OBL_CHECK((env.queuedBuilding != nullptr) == (building && c.flags == 0));
        OBL_CHECK((env.approachedScenery != nullptr) == scenery);
        OBL_CHECK((env.tileOf != nullptr) == (!building && !scenery)); // anything else gives its own tile
        // The stop tile: the destination's for a building and a scenery, the object's own for the rest (3, 4 in the fake).
        OBL_REQUIRE(env.sceneryRoutes.size() == 1);
        OBL_CHECK(!env.sceneryRoutes[0].foundIn);
        if (building || scenery)
            OBL_CHECK_EQ(env.sceneryRoutes[0].tx, static_cast<uint32_t>(c.expectedX) / 10);
        else
            OBL_CHECK_EQ(env.sceneryRoutes[0].tx, 3u);
        OBL_CHECK(o.m_pathCursor == env.sceneryResult);
        OBL_CHECK_EQ(o.m_posOffset, 1u); // rand() 4 -> 4 % 2 + 1
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
        OBL_CHECK_EQ(o.m_messageReceived, 123.5f);
        OBL_CHECK_EQ(env.order.back(), std::string("rand"));
    }
}

OBL_TEST_CASE("moving_messages.scenery_object_draws_the_position_offset_even_without_a_route")
{
    for (const int rand : {4, 5, 0, 1}) {
        Holder h;
        Holder target;
        target.Object().m_type = object::ObjectType::Building;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        FakeEnv env;
        env.sender = &target.Object();
        env.randValue = rand;
        env.servePosition[0] = 12.0f;
        env.servePosition[1] = 25.0f; // the same tile as the object
        const auto result = moving::ReceiveMessage(o, SceneryObject(55, 1), env);
        OBL_CHECK(result.has_value() && *result);
        OBL_CHECK(env.sceneryRoutes.empty());
        OBL_CHECK_EQ(o.m_posOffset, static_cast<uint32_t>(rand % 2 + 1));
        OBL_CHECK_EQ(env.randCalls, 1);
    }
}

OBL_TEST_CASE("moving_messages.scenery_object_releases_the_old_route_and_remembers_the_building_it_leaves")
{
    Holder h;
    Holder building;
    building.Object().m_type = object::ObjectType::Building;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    path::RouteCursor oldRoute{};
    o.m_pathCursor = &oldRoute;
    o.m_insideBuilding = 1;
    o.m_DestObjectID = 31;
    FakeEnv env;
    env.sender = &building.Object();
    const auto result = moving::ReceiveMessage(o, SceneryObject(55, 1), env);
    OBL_CHECK(result.has_value() && *result);
    OBL_REQUIRE(env.released.size() == 1);
    OBL_CHECK(env.released[0] == &oldRoute);
    OBL_CHECK_EQ(o.m_BuildingLeavingID, 31);
    OBL_CHECK_EQ(o.m_DestObjectID, 55);
    OBL_CHECK_EQ(static_cast<int>(o.m_insideBuilding), 1); // not cleared here
    // The old route is released before the start tile is looked up: first the leaving building, then the target.
    OBL_REQUIRE(env.askedIds.size() == 2);
    OBL_CHECK_EQ(env.askedIds[0], 31);
    OBL_CHECK_EQ(env.askedIds[1], 55);
    OBL_CHECK_EQ(env.order.front(), std::string("release"));
}

OBL_TEST_CASE("moving_messages.scenery_object_without_a_usable_target_leaves_the_destination_at_the_origin")
{
    for (const int id : {-1, 12}) {
        Holder h;
        h.Object().m_DestXYZ[0] = 900.0f;
        FakeEnv env;
        env.sender = nullptr;
        const auto result = moving::ReceiveMessage(h.Object(), SceneryObject(id, 1), env);
        OBL_CHECK(result.has_value() && *result);
        OBL_CHECK_EQ(h.Object().m_DestObjectID, id);
        OBL_CHECK_EQ(h.Object().m_DestXYZ[0], 0.0f);
        OBL_CHECK_EQ(static_cast<int>(h.Object().m_moving), 1);
        OBL_CHECK(env.allowedAsked.empty() && env.sceneryRoutes.empty());
        OBL_CHECK_EQ(env.randCalls, 0);
        OBL_CHECK_EQ(env.askedFor == -2 ? 1 : 0, id == -1 ? 1 : 0);
    }
}

// ---- MT_MOVE2SEAPOINT / MT_SWIM2SEAPOINT / MT_SEAVEHICLE_MOVE2POINT ----------------------------------------------------------------------

namespace {
Message SeaWalk(int type, int x, int y)
{
    Message message = Make(type);
    message.flags = x;
    message.data = y;
    return message;
}
bool Answered(const std::optional<bool>& result, bool value)
{
    return result.has_value() && *result == value;
}
} // namespace

OBL_TEST_CASE("moving_messages.sea_land_walk_takes_behaviour_two_and_a_land_route_with_option_one")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    path::RouteCursor cursor{};
    FakeEnv env;
    env.landResult = &cursor;
    const auto result = moving::ReceiveMessage(o, SeaWalk(obl::messages::MT_MOVE2SEAPOINT, 300, 400), env);
    OBL_CHECK(
            Answered(result, false)); // the walk ends in the base object receive message routine, which has nothing to say about this type
    OBL_CHECK_EQ(o.m_behaviourState, 2u);
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
    OBL_REQUIRE(env.lands.size() == 1);
    OBL_CHECK_EQ(env.lands[0].sx, 1u);
    OBL_CHECK_EQ(env.lands[0].sy, 2u);
    OBL_CHECK_EQ(env.lands[0].tx, 30u);
    OBL_CHECK_EQ(env.lands[0].ty, 40u);
    OBL_CHECK(env.lands[0].foundIn);
    OBL_CHECK_EQ(env.lands[0].option, 1u);
    OBL_CHECK(env.seaRoutes.empty() && env.flagsAsked.empty() && env.prepared.empty() && env.cleared.empty());
    OBL_CHECK(o.m_pathCursor == &cursor);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    OBL_CHECK_EQ(o.m_messageReceived, 123.5f);
}

OBL_TEST_CASE("moving_messages.sea_land_walk_stores_a_failed_route_as_null_and_releases_the_old_one_first")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    path::RouteCursor old{};
    o.m_pathCursor = &old;
    FakeEnv env;
    const auto result = moving::ReceiveMessage(o, SeaWalk(obl::messages::MT_MOVE2SEAPOINT, 300, 400), env);
    OBL_CHECK(Answered(result, false));
    OBL_REQUIRE(env.released.size() == 1);
    OBL_CHECK(env.released[0] == &old);
    OBL_CHECK_EQ(env.order.front(), std::string("release"));
    OBL_CHECK(o.m_pathCursor == nullptr);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
}

OBL_TEST_CASE("moving_messages.sea_walks_already_there_report_arrival_without_a_route")
{
    for (const int type : {obl::messages::MT_MOVE2SEAPOINT, obl::messages::MT_SWIM2SEAPOINT, obl::messages::MT_SEAVEHICLE_MOVE2POINT}) {
        Holder h;
        auto& o = h.Object();
        PlaceAt(o, 300.05f, 399.95f);
        o.m_state = 9;
        FakeEnv env;
        const auto result = moving::ReceiveMessage(o, SeaWalk(type, 300, 400), env);
        OBL_CHECK(Answered(result, true));
        OBL_CHECK_EQ(static_cast<int>(o.m_moving), 0);
        OBL_CHECK_EQ(o.m_behaviourState, 1u); // only the move command's own seek, not the sea walk's behaviour
        OBL_CHECK(env.lands.empty() && env.seaRoutes.empty() && env.flagsAsked.empty());
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 0);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK_EQ(env.sent[0].message.type, static_cast<int>(obl::messages::MT_MOVEIMHERE));
    }
}

OBL_TEST_CASE("moving_messages.sea_walks_inside_a_building_start_at_the_serve_position_of_the_destination_object")
{
    for (const int type : {obl::messages::MT_MOVE2SEAPOINT, obl::messages::MT_SWIM2SEAPOINT}) {
        Holder h;
        Holder building;
        building.Object().m_type = object::ObjectType::Building;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        o.m_insideBuilding = 1;
        o.m_DestObjectID = 77;
        FakeEnv env;
        env.sender = &building.Object();
        env.servePosition[0] = 55.0f;
        env.servePosition[1] = 66.0f;
        const auto result = moving::ReceiveMessage(o, SeaWalk(type, 300, 400), env);
        OBL_CHECK(Answered(result, false));
        OBL_CHECK_EQ(env.askedFor, 77);
        OBL_CHECK(env.servedBuilding == &building.Object());
        const auto& route = type == obl::messages::MT_MOVE2SEAPOINT ? env.lands : env.seaRoutes;
        OBL_REQUIRE(route.size() == 1);
        OBL_CHECK_EQ(route[0].sx, 5u); // the tile of the serve position, not of the object
        OBL_CHECK_EQ(route[0].sy, 6u);
        OBL_CHECK_EQ(static_cast<int>(o.m_insideBuilding), 1); // the object stays inside
        OBL_CHECK_EQ(o.m_WorldXYZ[0], 10.0f);
    }
}

OBL_TEST_CASE("moving_messages.sea_walks_survive_a_vanished_building")
{
    for (const int type : {obl::messages::MT_MOVE2SEAPOINT, obl::messages::MT_SWIM2SEAPOINT}) {
        Holder h;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        o.m_insideBuilding = 1;
        o.m_DestObjectID = 77;
        FakeEnv env;
        env.sender = nullptr;
        const auto result = moving::ReceiveMessage(o, SeaWalk(type, 300, 400), env);
        OBL_CHECK(Answered(result, false));
        const auto& route = type == obl::messages::MT_MOVE2SEAPOINT ? env.lands : env.seaRoutes;
        OBL_REQUIRE(route.size() == 1);
        OBL_CHECK_EQ(route[0].sx, 1u); // the object's own tile stands in
    }
}

OBL_TEST_CASE("moving_messages.swim_routes_straight_away_between_two_sea_tiles")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    path::RouteCursor cursor{};
    FakeEnv env;
    env.seaResult = &cursor;
    // One sea bit is enough: the test is `flags & 0x60000000`.
    env.tileFlags.push_back({1, 2, 0x20000000u});
    env.tileFlags.push_back({30, 40, 0x40000000u});
    const auto result = moving::ReceiveMessage(o, SeaWalk(obl::messages::MT_SWIM2SEAPOINT, 300, 400), env);
    OBL_CHECK(Answered(result, false));
    OBL_CHECK_EQ(o.m_behaviourState, 1u); // not changed by the swim, unlike the land walk
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
    OBL_REQUIRE(env.flagsAsked.size() == 2);
    OBL_CHECK_EQ(env.flagsAsked[0][0], 1u);
    OBL_CHECK_EQ(env.flagsAsked[0][1], 2u);
    OBL_CHECK_EQ(env.flagsAsked[1][0], 30u);
    OBL_CHECK_EQ(env.flagsAsked[1][1], 40u);
    OBL_REQUIRE(env.seaRoutes.size() == 1);
    OBL_CHECK_EQ(env.seaRoutes[0].sx, 1u);
    OBL_CHECK_EQ(env.seaRoutes[0].sy, 2u);
    OBL_CHECK_EQ(env.seaRoutes[0].tx, 30u);
    OBL_CHECK_EQ(env.seaRoutes[0].ty, 40u);
    OBL_CHECK(env.seaRoutes[0].foundIn);
    OBL_CHECK(env.prepared.empty() && env.cleared.empty() && env.lands.empty());
    OBL_CHECK(o.m_pathCursor == &cursor);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    OBL_CHECK_EQ(o.m_messageReceived, 123.5f);
}

OBL_TEST_CASE("moving_messages.swim_on_the_same_tile_builds_nothing_but_still_waits")
{
    for (const bool sea : {true, false}) {
        Holder h;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        FakeEnv env;
        if (!sea) env.defaultFlags = 0;
        // The destination (12, 25) is on tile (1, 2) as well.
        const auto result = moving::ReceiveMessage(o, SeaWalk(obl::messages::MT_SWIM2SEAPOINT, 12, 25), env);
        OBL_CHECK(Answered(result, false));
        OBL_CHECK(env.seaRoutes.empty() && env.prepared.empty() && env.cleared.empty());
        OBL_CHECK(o.m_pathCursor == nullptr);
        OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
        OBL_CHECK_EQ(env.flagsAsked.size(), sea ? 2u : 1u); // a dry start tile is not followed by the stop test
    }
}

OBL_TEST_CASE("moving_messages.swim_with_a_dry_end_prepares_with_the_sea_mask_routes_the_corrected_ends_and_cleans_up_with_the_land_mask")
{
    struct Case {
        std::vector<std::array<uint32_t, 3>> flags;
        bool prepareOk, moves;
        size_t askedTiles;
    };
    const Case cases[] = {{{{1, 2, 0}}, true, false, 1}, {{{30, 40, 0x1fffffffu}}, true, true, 2}, {{{30, 40, 0}}, false, false, 2},
            {{{1, 2, 0}, {30, 40, 0}}, false, true, 1}};
    for (const Case& c : cases) {
        Holder h;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        path::RouteCursor cursor{};
        FakeEnv env;
        env.tileFlags = c.flags;
        env.prepareResult = c.prepareOk;
        env.prepareMoves = c.moves;
        env.seaResult = &cursor;
        const auto result = moving::ReceiveMessage(o, SeaWalk(obl::messages::MT_SWIM2SEAPOINT, 300, 400), env);
        OBL_CHECK(Answered(result, false));
        OBL_CHECK_EQ(env.flagsAsked.size(), c.askedTiles);
        OBL_REQUIRE(env.prepared.size() == 1);
        OBL_CHECK_EQ(env.prepared[0][0], 1u);
        OBL_CHECK_EQ(env.prepared[0][1], 2u);
        OBL_CHECK_EQ(env.prepared[0][2], 30u);
        OBL_CHECK_EQ(env.prepared[0][3], 40u);
        OBL_CHECK_EQ(env.masks[0], 0x60000000u);
        const uint32_t startX = c.moves ? 101u : 1u, stopY = c.moves ? 240u : 40u; // what the helper wrote into the arrays
        if (c.prepareOk) {
            OBL_REQUIRE(env.seaRoutes.size() == 1);
            OBL_CHECK_EQ(env.seaRoutes[0].sx, startX);
            OBL_CHECK_EQ(env.seaRoutes[0].ty, stopY);
            OBL_CHECK(o.m_pathCursor == &cursor);
        } else {
            OBL_CHECK(env.seaRoutes.empty());
            OBL_CHECK(o.m_pathCursor == nullptr);
        }
        OBL_REQUIRE(env.cleared.size() == 1); // also after a failed helper
        OBL_CHECK_EQ(env.cleared[0][0], startX);
        OBL_CHECK_EQ(env.cleared[0][3], stopY);
        OBL_CHECK_EQ(env.masks[1], 0x80000000u); // the cleanup names the land mask, whatever the walk
        OBL_CHECK_EQ(env.order.back(), std::string("clear"));
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    }
}

namespace {
// A three-step sea route whose cursor stands on step `current`.
struct SeaRoute {
    path::RouteCursor cursor{};
    explicit SeaRoute(int current)
    {
        cursor.m_route = std::make_shared<path::Route>(path::Route{.m_steps = {{3, 2}, {4, 2}, {5, 2}}});
        cursor.m_currentStep = current;
    }
};
} // namespace

OBL_TEST_CASE("moving_messages.vehicle_route_points_the_object_at_the_first_tile_and_sets_the_vehicle_state")
{
    Holder h;
    auto& o = h.Object();
    // SetDestinationFromMessage copies the object's position into m_vPosition, so the heading starts at the object: here straight above the
    // origin (48, 64) of tile (3, 2).
    PlaceAt(o, 48.0f, 0.0f);
    o.m_controlDirection = 1;
    o.m_posOffset = 5;
    o.m_turnType = 7;
    o.m_lastDir = 99;
    SeaRoute route(0);
    FakeEnv env;
    env.seaResult = &route.cursor;
    const auto result = moving::ReceiveMessage(o, SeaWalk(obl::messages::MT_SEAVEHICLE_MOVE2POINT, 300, 400), env);
    OBL_CHECK(Answered(result, false));
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
    OBL_REQUIRE(env.seaRoutes.size() == 1);
    OBL_CHECK_EQ(env.seaRoutes[0].sx, 4u);
    OBL_CHECK_EQ(env.seaRoutes[0].sy, 0u);
    OBL_CHECK_EQ(env.seaRoutes[0].tx, 30u);
    OBL_CHECK_EQ(env.seaRoutes[0].ty, 40u);
    OBL_CHECK(env.seaRoutes[0].foundIn);
    OBL_CHECK(env.flagsAsked.empty() && env.prepared.empty() && env.cleared.empty());
    OBL_CHECK(o.m_pathCursor == &route.cursor);
    OBL_REQUIRE(env.originsAsked.size() == 1);
    OBL_CHECK_EQ(env.originsAsked[0][0], 3u);
    OBL_CHECK_EQ(env.originsAsked[0][1], 2u);
    OBL_CHECK_EQ(o.m_lastDir, 4u); // the vector (0, +1): sector 4 of 8
    OBL_CHECK_EQ(static_cast<int>(o.m_vehiclePath), 1);
    OBL_CHECK_EQ(static_cast<int>(o.m_controlDirection), 0);
    OBL_CHECK_EQ(o.m_behaviourState, 1u);
    OBL_CHECK_EQ(o.m_posOffset, 0u);
    OBL_CHECK_EQ(o.m_turnType, 1u);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    OBL_CHECK_EQ(o.m_messageReceived, 123.5f);
}

OBL_TEST_CASE("moving_messages.vehicle_route_heading_follows_the_direction_to_the_cursor_step")
{
    struct Case {
        float x, y;
        uint32_t direction;
    };
    // Tile (4, 2), where the cursor stands, has the origin (64, 64). The vector from the object to it is a unit axis, so the sectors are the quarter turns.
    const Case cases[] = {{164.0f, 64.0f, 6u}, {-36.0f, 64.0f, 2u}, {64.0f, 164.0f, 0u}, {64.0f, -36.0f, 4u}};
    for (const Case& c : cases) {
        Holder h;
        auto& o = h.Object();
        PlaceAt(o, c.x, c.y);
        SeaRoute route(1);
        FakeEnv env;
        env.seaResult = &route.cursor;
        OBL_CHECK(Answered(moving::ReceiveMessage(o, SeaWalk(obl::messages::MT_SEAVEHICLE_MOVE2POINT, 300, 400), env), false));
        OBL_REQUIRE(env.originsAsked.size() == 1);
        OBL_CHECK_EQ(env.originsAsked[0][0], 4u); // the step the cursor stands on, not the first one
        OBL_CHECK_EQ(o.m_lastDir, c.direction);
    }
}

OBL_TEST_CASE("moving_messages.vehicle_without_a_route_clears_the_flag_and_answers_true_without_waiting")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    o.m_vehiclePath = 1;
    o.m_lastDir = 99;
    FakeEnv env;
    const auto result = moving::ReceiveMessage(o, SeaWalk(obl::messages::MT_SEAVEHICLE_MOVE2POINT, 300, 400), env);
    OBL_CHECK(Answered(result, true));
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
    OBL_CHECK(o.m_pathCursor == nullptr);
    OBL_CHECK_EQ(static_cast<int>(o.m_vehiclePath), 0);
    OBL_CHECK_EQ(o.m_lastDir, 99u);
    OBL_CHECK(env.originsAsked.empty());
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 0);
    OBL_CHECK_EQ(o.m_messageReceived, 0.0f);
}

OBL_TEST_CASE("moving_messages.vehicle_route_starts_at_the_objects_own_tile_even_inside_a_building")
{
    Holder h;
    Holder building;
    building.Object().m_type = object::ObjectType::Building;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    o.m_insideBuilding = 1;
    o.m_DestObjectID = 77;
    SeaRoute route(0);
    FakeEnv env;
    env.sender = &building.Object();
    env.seaResult = &route.cursor;
    OBL_CHECK(Answered(moving::ReceiveMessage(o, SeaWalk(obl::messages::MT_SEAVEHICLE_MOVE2POINT, 300, 400), env), false));
    OBL_CHECK(env.servedBuilding == nullptr);
    OBL_REQUIRE(env.seaRoutes.size() == 1);
    OBL_CHECK_EQ(env.seaRoutes[0].sx, 1u);
    OBL_CHECK_EQ(env.seaRoutes[0].sy, 2u);
}

// ---- MT_MOVE2POINT / MT_MOVE2OBJECT / MT_PAVEMENT2POINT / MT_PAVEMENT2OBJECT ------------------------------------------------------------

namespace {
constexpr uint32_t kLandTile = 0x80000000u;
constexpr uint32_t kPavementTile = 0x10000000u;

Message GroundPoint(int type, int x, int y)
{
    Message message = Make(type);
    message.flags = x;
    message.data = y;
    return message;
}
Message GroundObject(int type, int id, int flags)
{
    Message message = Make(type);
    message.data = id;
    message.flags = flags;
    return message;
}
} // namespace

OBL_TEST_CASE("moving_messages.land_point_tests_both_tiles_as_land_and_builds_a_land_route_with_option_zero")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    o.m_moving = 0;
    path::RouteCursor cursor{};
    FakeEnv env;
    env.landResult = &cursor;
    env.randValue = 5;
    const auto result = moving::ReceiveMessage(o, GroundPoint(obl::messages::MT_MOVE2POINT, 300, 400), env);
    OBL_CHECK(Answered(result, true));
    OBL_CHECK_EQ(o.m_behaviourState, 1u);
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
    OBL_REQUIRE(env.allowedAsked.size() == 2); // the start tile first
    OBL_CHECK_EQ(env.allowedAsked[0][0], 1u);
    OBL_CHECK_EQ(env.allowedAsked[0][1], 2u);
    OBL_CHECK_EQ(env.allowedAsked[1][0], 30u);
    OBL_CHECK_EQ(env.allowedAsked[1][1], 40u);
    OBL_CHECK_EQ(env.allowedTypes[0], 1);
    OBL_CHECK_EQ(env.allowedTypes[1], 1);
    OBL_REQUIRE(env.lands.size() == 1);
    OBL_CHECK_EQ(env.lands[0].tx, 30u);
    OBL_CHECK(env.lands[0].foundIn);
    OBL_CHECK_EQ(env.lands[0].option, 0u);
    OBL_CHECK(env.prepared.empty() && env.cleared.empty() && env.posted.empty());
    OBL_CHECK(o.m_pathCursor == &cursor);
    OBL_CHECK_EQ(o.m_posOffset, 2u); // rand() 5 -> 5 % 2 + 1
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    OBL_CHECK_EQ(o.m_messageReceived, 123.5f);
}

OBL_TEST_CASE("moving_messages.land_point_already_there_reports_arrival_and_draws_nothing")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 300.05f, 399.95f);
    FakeEnv env;
    OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundPoint(obl::messages::MT_MOVE2POINT, 300, 400), env), true));
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 0);
    OBL_CHECK(env.allowedAsked.empty() && env.lands.empty() && env.posted.empty());
    OBL_CHECK_EQ(env.randCalls, 0);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 0);
}

OBL_TEST_CASE("moving_messages.land_point_without_a_route_posts_cant_move_before_the_wait_state_is_set")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    o.m_id = 4242;
    o.m_state = 17;
    FakeEnv env;
    env.watched = &o;
    env.landResult = nullptr;
    OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundPoint(obl::messages::MT_MOVE2POINT, 300, 400), env), true));
    OBL_CHECK(o.m_pathCursor == nullptr);
    OBL_REQUIRE(env.posted.size() == 1);
    const Message& m = env.posted[0].message;
    OBL_CHECK_EQ(m.type, static_cast<int>(obl::messages::MT_CANT_MOVE));
    OBL_CHECK_EQ(m.sender, 4242);
    OBL_CHECK_EQ(m.receiver, 4242);
    OBL_CHECK_EQ(m.extra, 17);
    OBL_CHECK_EQ(m.flags, 0);
    OBL_CHECK_EQ(m.data, 0);
    OBL_CHECK_EQ(static_cast<int>(env.posted[0].waitingAtTheTime), 0); // posted first
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    OBL_CHECK_EQ(env.order.back(), std::string("post")); // the rand draw came before the post
}

OBL_TEST_CASE("moving_messages.land_point_with_a_blocked_end_prepares_routes_with_the_corrected_ends_and_always_cleans_up")
{
    struct Case {
        std::vector<std::array<uint32_t, 2>> blocked;
        bool prepareOk, moves;
        size_t askedTiles;
    };
    const Case cases[] = {{{{1, 2}}, true, false, 1}, {{{30, 40}}, true, true, 2}, {{{30, 40}}, false, false, 2}};
    for (const Case& c : cases) {
        Holder h;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        path::RouteCursor cursor{};
        FakeEnv env;
        env.blockedTiles = c.blocked;
        env.prepareResult = c.prepareOk;
        env.prepareMoves = c.moves;
        env.landResult = &cursor;
        OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundPoint(obl::messages::MT_MOVE2POINT, 300, 400), env), true));
        OBL_CHECK_EQ(env.allowedAsked.size(), c.askedTiles);
        OBL_REQUIRE(env.prepared.size() == 1);
        OBL_CHECK_EQ(env.masks[0], 0x80000000u);
        const uint32_t startX = c.moves ? 101u : 1u, stopY = c.moves ? 240u : 40u;
        if (c.prepareOk) {
            OBL_REQUIRE(env.lands.size() == 1);
            OBL_CHECK_EQ(env.lands[0].sx, startX);
            OBL_CHECK_EQ(env.lands[0].ty, stopY);
            OBL_CHECK(o.m_pathCursor == &cursor);
            OBL_CHECK(env.posted.empty());
        } else {
            OBL_CHECK(env.lands.empty());
            OBL_CHECK_EQ(env.posted.size(), 1u); // no route: MT_CANT_MOVE
        }
        OBL_REQUIRE(env.cleared.size() == 1);
        OBL_CHECK_EQ(env.masks[1], 0x80000000u);
    }
}

OBL_TEST_CASE("moving_messages.land_point_on_the_same_tile_builds_nothing_and_says_cant_move")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    FakeEnv env;
    OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundPoint(obl::messages::MT_MOVE2POINT, 12, 25), env), true));
    OBL_CHECK(env.lands.empty() && env.prepared.empty() && env.cleared.empty());
    OBL_CHECK_EQ(env.posted.size(), 1u);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
}

OBL_TEST_CASE("moving_messages.land_point_from_inside_a_building_starts_at_the_serve_position_of_the_leaving_building")
{
    Holder h;
    Holder building;
    building.Object().m_type = object::ObjectType::Building;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    o.m_insideBuilding = 1;
    o.m_DestObjectID = 77;
    FakeEnv env;
    env.sender = &building.Object();
    env.servePosition[0] = 55.0f;
    env.servePosition[1] = 66.0f;
    env.landResult = reinterpret_cast<path::RouteCursor*>(0x100);
    OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundPoint(obl::messages::MT_MOVE2POINT, 300, 400), env), true));
    OBL_CHECK_EQ(env.askedFor, 77);
    OBL_REQUIRE(env.lands.size() == 1);
    OBL_CHECK_EQ(env.lands[0].sx, 5u);
    OBL_CHECK_EQ(env.lands[0].sy, 6u);
    OBL_CHECK_EQ(o.m_BuildingLeavingID, 77);
}

OBL_TEST_CASE("moving_messages.land_object_picks_the_target_spot_by_class_and_routes_by_the_land_flags_of_both_tiles")
{
    struct Case {
        object::ObjectType type;
        int flags;
        float expectedX;
    };
    const Case cases[] = {{object::ObjectType::Building, 1, 7.0f}, {object::ObjectType::Building, 0, 17.0f},
            {object::ObjectType::Scenery, 1, 27.0f}, {object::ObjectType::Guest, 0, 400.0f}};
    for (const Case& c : cases) {
        Holder h;
        Holder target;
        target.Object().m_type = c.type;
        target.Object().m_WorldXYZ[0] = 400.0f;
        target.Object().m_WorldXYZ[1] = 500.0f;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        o.m_behaviourState = 4;
        o.m_moving = 0;
        path::RouteCursor cursor{};
        FakeEnv env;
        env.sender = &target.Object();
        env.defaultFlags = kLandTile;
        env.landResult = &cursor;
        env.randValue = 6;
        OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundObject(obl::messages::MT_MOVE2OBJECT, 55, c.flags), env), true));
        OBL_CHECK_EQ(o.m_behaviourState, 1u);
        OBL_CHECK_EQ(o.m_DestObjectID, 55);
        OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
        OBL_CHECK_EQ(o.m_DestXYZ[0], c.expectedX);
        const bool building = c.type == object::ObjectType::Building, scenery = c.type == object::ObjectType::Scenery;
        OBL_CHECK((env.servedBuilding != nullptr) == (building && c.flags != 0));
        OBL_CHECK((env.queuedBuilding != nullptr) == (building && c.flags == 0));
        OBL_CHECK((env.approachedScenery != nullptr) == scenery);
        OBL_CHECK((env.tileOf != nullptr) == (!building && !scenery));
        OBL_CHECK(env.allowedAsked.empty()); // the flags decide here, not Path::IsTileAllowed
        OBL_REQUIRE(env.lands.size() == 1);
        OBL_CHECK(!env.lands[0].foundIn);
        OBL_CHECK_EQ(env.lands[0].option, 0u);
        OBL_CHECK(env.prepared.empty() && env.cleared.empty() && env.posted.empty());
        OBL_CHECK(o.m_pathCursor == &cursor);
        OBL_CHECK_EQ(o.m_posOffset, 1u);
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
    }
}

OBL_TEST_CASE("moving_messages.land_object_without_a_route_posts_cant_move_after_the_wait_state_is_set")
{
    Holder h;
    Holder target;
    target.Object().m_type = object::ObjectType::Building;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    o.m_id = 31;
    o.m_state = 5;
    FakeEnv env;
    env.watched = &o;
    env.sender = &target.Object();
    env.defaultFlags = kLandTile;
    env.landResult = nullptr;
    OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundObject(obl::messages::MT_MOVE2OBJECT, 55, 1), env), true));
    OBL_REQUIRE(env.posted.size() == 1);
    OBL_CHECK_EQ(env.posted[0].message.type, static_cast<int>(obl::messages::MT_CANT_MOVE));
    OBL_CHECK_EQ(env.posted[0].message.extra, 5);
    OBL_CHECK_EQ(static_cast<int>(env.posted[0].waitingAtTheTime), 1); // posted last
    OBL_CHECK_EQ(env.order.back(), std::string("post"));
}

OBL_TEST_CASE("moving_messages.land_object_with_a_dry_end_goes_through_the_endpoint_helper")
{
    Holder h;
    Holder target;
    target.Object().m_type = object::ObjectType::Building;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    path::RouteCursor cursor{};
    FakeEnv env;
    env.sender = &target.Object();
    env.defaultFlags = kLandTile;
    env.tileFlags.push_back({0, 0, 0}); // the serve position (7, 8) is tile (0, 0): not land
    env.landResult = &cursor;
    OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundObject(obl::messages::MT_MOVE2OBJECT, 55, 1), env), true));
    OBL_REQUIRE(env.prepared.size() == 1);
    OBL_CHECK_EQ(env.masks[0], 0x80000000u);
    OBL_REQUIRE(env.lands.size() == 1);
    OBL_REQUIRE(env.cleared.size() == 1);
    const auto at = [&](const char* name) { return std::find(env.order.begin(), env.order.end(), name) - env.order.begin(); };
    OBL_CHECK(at("prepare") < at("land"));
    OBL_CHECK(at("land") < at("clear"));
    OBL_CHECK(at("clear") < at("rand"));
    OBL_CHECK(o.m_pathCursor == &cursor);
}

OBL_TEST_CASE("moving_messages.land_object_without_a_usable_target_leaves_the_destination_at_the_origin")
{
    for (const int id : {-1, 12}) {
        Holder h;
        auto& o = h.Object();
        o.m_DestXYZ[0] = 900.0f;
        FakeEnv env;
        env.sender = nullptr;
        OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundObject(obl::messages::MT_MOVE2OBJECT, id, 1), env), true));
        OBL_CHECK_EQ(o.m_DestObjectID, id);
        OBL_CHECK_EQ(o.m_DestXYZ[0], 0.0f);
        OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
        OBL_CHECK(env.lands.empty() && env.posted.empty());
        OBL_CHECK_EQ(env.randCalls, 0);
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 0);
    }
}

OBL_TEST_CASE("moving_messages.pavement_point_routes_by_the_pavement_flags_and_waits_without_reading_the_flags_again")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 10.0f, 20.0f);
    path::RouteCursor cursor{};
    FakeEnv env;
    env.defaultFlags = kPavementTile;
    env.pavementResult = &cursor;
    OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundPoint(obl::messages::MT_PAVEMENT2POINT, 300, 400), env), true));
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 1);
    OBL_REQUIRE(env.pavements.size() == 1);
    OBL_CHECK(env.pavements[0].foundIn);
    OBL_CHECK_EQ(env.pavements[0].sx, 1u);
    OBL_CHECK_EQ(env.pavements[0].ty, 40u);
    OBL_CHECK_EQ(env.flagsAsked.size(), 2u); // BuildRoute's two reads only
    OBL_CHECK(env.adjacentAsked.empty() && env.posted.empty());
    OBL_CHECK_EQ(env.randCalls, 0);
    OBL_CHECK(o.m_pathCursor == &cursor);
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
}

OBL_TEST_CASE("moving_messages.pavement_point_already_there_reports_arrival_without_a_route")
{
    Holder h;
    auto& o = h.Object();
    PlaceAt(o, 300.0f, 400.0f);
    FakeEnv env;
    OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundPoint(obl::messages::MT_PAVEMENT2POINT, 300, 400), env), true));
    OBL_CHECK_EQ(static_cast<int>(o.m_moving), 0);
    OBL_CHECK(env.pavements.empty() && env.flagsAsked.empty() && env.posted.empty());
    OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 0);
}

OBL_TEST_CASE("moving_messages.pavement_point_blocked_report_follows_the_flags_and_the_neighbour_test")
{
    // BuildRoute builds nothing (no pavement route result); the report then reads the flags of both ends again and asks the neighbour test.
    struct Case {
        uint32_t startFlags, stopFlags;
        bool adjacent;
        int x, y; // the destination; (300, 400) is another tile than the object's (1, 2), (12, 25) is the same one
        bool blocked;
    };
    const Case cases[] = {
            {kPavementTile, kPavementTile, true, 300, 400, false}, // pavement ends that are neighbours: no report
            {kPavementTile, 0, false, 300, 400, true}, // not neighbours
            {kPavementTile, 0, true, 300, 400, false}, // neighbours, one end on the pavement
            {0, 0, true, 300, 400, true}, // neither end on the pavement: blocked even if neighbours
            {kPavementTile, kPavementTile, false, 12, 25, false}, // the same tile is always "reachable"
            {0, 0, false, 12, 25, true}, // ... unless neither end is pavement
    };
    for (const Case& c : cases) {
        Holder h;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        o.m_id = 9;
        o.m_state = 3;
        FakeEnv env;
        env.tileFlags.push_back({1, 2, c.startFlags});
        env.tileFlags.push_back({c.x / 10u, c.y / 10u, c.stopFlags});
        env.defaultFlags = 0;
        env.adjacent = c.adjacent;
        env.prepareResult = false;
        env.pavementResult = nullptr;
        OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundPoint(obl::messages::MT_PAVEMENT2POINT, c.x, c.y), env), true));
        OBL_CHECK_EQ(env.posted.size(), c.blocked ? 1u : 0u);
        OBL_CHECK_EQ(static_cast<int>(o.m_moving), c.blocked ? 0 : 1);
        if (c.blocked) {
            OBL_CHECK_EQ(env.posted[0].message.type, static_cast<int>(obl::messages::MT_PATHBLOCKED));
            OBL_CHECK_EQ(env.posted[0].message.sender, 9);
            OBL_CHECK_EQ(env.posted[0].message.receiver, 9);
            OBL_CHECK_EQ(env.posted[0].message.extra, 3);
        }
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1); // the wait state is set either way
    }
}

OBL_TEST_CASE("moving_messages.pavement_object_draws_the_offset_after_the_route_and_reports_a_blocked_walk")
{
    for (const bool route : {true, false}) {
        Holder h;
        Holder target;
        target.Object().m_type = object::ObjectType::Building;
        auto& o = h.Object();
        PlaceAt(o, 10.0f, 20.0f);
        o.m_id = 12;
        path::RouteCursor cursor{};
        FakeEnv env;
        env.sender = &target.Object();
        env.defaultFlags = kPavementTile;
        env.pavementResult = route ? &cursor : nullptr;
        env.randValue = 7;
        OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundObject(obl::messages::MT_PAVEMENT2OBJECT, 55, 1), env), true));
        OBL_CHECK_EQ(o.m_DestObjectID, 55);
        OBL_CHECK_EQ(o.m_DestXYZ[0], 7.0f); // the serve position
        OBL_REQUIRE(env.pavements.size() == 1);
        OBL_CHECK(!env.pavements[0].foundIn);
        OBL_CHECK_EQ(o.m_posOffset, 2u);
        OBL_CHECK_EQ(env.randCalls, 1);
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 1);
        // The fake says the ends are no neighbours, so a missing route is reported; with a route nothing is.
        OBL_CHECK_EQ(env.posted.size(), route ? 0u : 1u);
        if (!route) {
            OBL_CHECK_EQ(env.posted[0].message.type, static_cast<int>(obl::messages::MT_PATHBLOCKED));
            OBL_CHECK_EQ(static_cast<int>(o.m_moving), 0);
        }
        OBL_CHECK(o.m_pathCursor == (route ? &cursor : nullptr));
    }
}

OBL_TEST_CASE("moving_messages.pavement_object_without_a_usable_target_leaves_the_destination_at_the_origin")
{
    for (const int id : {-1, 12}) {
        Holder h;
        auto& o = h.Object();
        o.m_DestXYZ[1] = 800.0f;
        FakeEnv env;
        env.sender = nullptr;
        OBL_CHECK(Answered(moving::ReceiveMessage(o, GroundObject(obl::messages::MT_PAVEMENT2OBJECT, id, 0), env), true));
        OBL_CHECK_EQ(o.m_DestXYZ[1], 0.0f);
        OBL_CHECK(env.pavements.empty() && env.posted.empty() && env.flagsAsked.empty());
        OBL_CHECK_EQ(env.randCalls, 0);
        OBL_CHECK_EQ(static_cast<int>(o.m_waitingForMoveImHere), 0);
    }
}
