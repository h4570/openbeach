#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/mover_target.h"
#include "game/objects/object_base.h"
#include "game/messages/message_state.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace obl;
namespace {
using object::Mover;
using obl::messages::Message;
using path::RouteCursor;

struct Holder {
    alignas(Mover) unsigned char bytes[sizeof(Mover)];
    Holder()
    {
        std::memset(bytes, 0, sizeof(bytes));
        obl::unit::StampVtable<Mover>(bytes);
        Object().m_id = 77;
        Object().m_mass = 1.0f;
        Object().m_maxVel = 10.0f;
        Object().m_maxForce = 1000.0f;
        Object().m_targetTolerance = 0.5f;
        Object().m_type = object::ObjectType::Scenery;
    }
    Mover& Object() { return *reinterpret_cast<Mover*>(bytes); }
};

// Records every effect in order, answers the queries from plain fields.
struct FakeEnv {
    enum class Kind { Post, PostDelayed, SendDelayed, Release, Rand };
    struct Event {
        Kind kind;
        Message message{};
        float delay = 0;
        const void* cursor = nullptr;
    };
    std::vector<Event> events;
    std::vector<int> draws;
    size_t nextDraw = 0;
    bool followMoving = false;
    float followPosition[3] = {0, 0, 0};
    float walkPoint[2] = {0, 0};
    bool buildingStands = true;
    float height = 0.0f;
    int32_t tileSize = 64;
    uint32_t flagsAt[2][2] = {}; // flags answered for the route tile (index 0) and for the object's own tile (index 1)
    uint32_t queriedTiles[8][2] = {};
    int tileQueries = 0;
    uint32_t ownTile[2] = {5, 6};
    int32_t heightX = 0, heightY = 0;

    void Post(const Message& m) { events.push_back({Kind::Post, m}); }
    void PostDelayed(const Message& m, float delay) { events.push_back({Kind::PostDelayed, m, delay}); }
    void SendDelayed(Mover&, const Message& m, float delay) { events.push_back({Kind::SendDelayed, m, delay}); }
    void ReleaseCursor(RouteCursor* cursor) { events.push_back({Kind::Release, {}, 0, cursor}); }
    int Rand()
    {
        events.push_back({Kind::Rand});
        return nextDraw < draws.size() ? draws[nextDraw++] : 0;
    }
    float TerrainHeight(int32_t x, int32_t y)
    {
        heightX = x;
        heightY = y;
        return height;
    }
    bool ObjectMoving(int32_t) { return followMoving; }
    void ObjectPosition(int32_t, float out[3]) { std::memcpy(out, followPosition, sizeof(followPosition)); }
    bool BuildingWalkPoint(int32_t, uint32_t, float out[2])
    {
        if (!buildingStands) return false;
        std::memcpy(out, walkPoint, sizeof(walkPoint));
        return true;
    }
    int32_t TileSize() { return tileSize; }
    uint32_t TileFlags(uint32_t x, uint32_t y)
    {
        queriedTiles[tileQueries][0] = x;
        queriedTiles[tileQueries][1] = y;
        return flagsAt[tileQueries++ == 0 ? 0 : 1][0];
    }
    void WorldToTile(float, float, float, uint32_t out[2])
    {
        out[0] = ownTile[0];
        out[1] = ownTile[1];
    }
    void PositionOffset(int variant, float out[2])
    {
        out[0] = variant == 1 ? 15.0f : -15.0f;
        out[1] = 0.0f;
    }
};

struct Route {
    std::shared_ptr<path::Route> data = std::make_shared<path::Route>();
    RouteCursor cursor{};
    Route()
    {
        data->m_steps = {{2, 3}, {4, 5}, {6, 7}};
        data->m_valid = 1;
        data->m_pathType = 1;
        cursor.m_route = data;
        cursor.m_currentStep = 1;
    }
};

bool Near(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}
} // namespace

// ---- CompleteIfAtDestination -----------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("moving_target.complete_stops_and_posts_move_im_here_after_a_hundredth")
{
    Holder h;
    auto& o = h.Object();
    o.m_WorldXYZ[0] = 10.0f;
    o.m_WorldXYZ[1] = 20.0f;
    o.m_DestXYZ[0] = 10.05f;
    o.m_DestXYZ[1] = 19.95f;
    o.m_moving = 1;
    o.m_vVelocity[0] = 3;
    o.m_vSteerForce[1] = 4;
    o.m_state = 91;
    o.m_waitingForMoveImHere = 1;
    o.m_messageReceived = 5.0f;
    FakeEnv env;
    OBL_CHECK(moving::CompleteIfAtDestination(o, env));
    OBL_CHECK_EQ(o.m_moving, 0);
    OBL_CHECK_EQ(o.m_vVelocity[0], 0.0f);
    OBL_CHECK_EQ(o.m_vSteerForce[1], 0.0f);
    OBL_CHECK_EQ(o.m_waitingForMoveImHere, 0);
    OBL_CHECK_EQ(o.m_messageReceived, 0.0f);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    OBL_CHECK(env.events[0].kind == FakeEnv::Kind::PostDelayed);
    OBL_CHECK_EQ(env.events[0].message.type, static_cast<int>(obl::messages::MT_MOVEIMHERE));
    OBL_CHECK_EQ(env.events[0].message.sender, 77);
    OBL_CHECK_EQ(env.events[0].message.receiver, 77);
    OBL_CHECK_EQ(env.events[0].message.extra, 91); // the object's state travels in the message
    OBL_CHECK_EQ(env.events[0].delay, 0.01f);
}

OBL_TEST_CASE("moving_target.complete_guest_sends_through_its_own_slot")
{
    Holder h;
    auto& o = h.Object();
    o.m_type = object::ObjectType::Guest;
    FakeEnv env;
    OBL_CHECK(moving::CompleteIfAtDestination(o, env));
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    OBL_CHECK(env.events[0].kind == FakeEnv::Kind::SendDelayed);
}

OBL_TEST_CASE("moving_target.complete_tests_each_axis_against_a_tenth_and_nan_passes")
{
    Holder h;
    auto& o = h.Object();
    o.m_moving = 1;
    o.m_DestXYZ[0] = 0.2f; // x too far: nothing happens, y is not even looked at
    FakeEnv env;
    OBL_CHECK(!moving::CompleteIfAtDestination(o, env));
    OBL_CHECK_EQ(o.m_moving, 1);
    OBL_CHECK(env.events.empty());
    o.m_DestXYZ[0] = 0.0f;
    o.m_DestXYZ[1] = 0.1f; // 0.1f is not below the widened 0.1f (the limit is the double of the same float)
    OBL_CHECK(!moving::CompleteIfAtDestination(o, env));
    o.m_DestXYZ[1] = 0.099f;
    OBL_CHECK(moving::CompleteIfAtDestination(o, env));
    o.m_moving = 1;
    o.m_DestXYZ[0] = std::numeric_limits<float>::quiet_NaN(); // an unordered compare counts as reached
    OBL_CHECK(moving::CompleteIfAtDestination(o, env));
}

// ---- SetDestinationFromMessage ---------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("moving_target.set_destination_takes_unsigned_words_and_asks_the_terrain_for_the_height")
{
    Holder h;
    auto& o = h.Object();
    o.m_WorldXYZ[0] = 5.5f;
    o.m_WorldXYZ[1] = 6.5f;
    o.m_behaviourState = 3;
    Message message{};
    message.flags = 1000;
    message.data = 2000;
    FakeEnv env;
    env.height = 12.5f;
    moving::SetDestinationFromMessage(o, message, env);
    OBL_CHECK_EQ(o.m_behaviourState, 1u);
    OBL_CHECK_EQ(o.m_vPosition[0], 5.5f);
    OBL_CHECK_EQ(o.m_vPosition[1], 6.5f);
    OBL_CHECK_EQ(o.m_DestXYZ[0], 1000.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[1], 2000.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[2], 12.5f);
    OBL_CHECK_EQ(env.heightX, 1000);
    OBL_CHECK_EQ(env.heightY, 2000);
    OBL_CHECK(env.events.empty()); // no route to drop
}

OBL_TEST_CASE("moving_target.set_destination_releases_the_route_and_remembers_the_building")
{
    Holder h;
    auto& o = h.Object();
    Route route;
    o.m_pathCursor = &route.cursor;
    o.m_insideBuilding = 1;
    o.m_DestObjectID = 321;
    Message message{};
    FakeEnv env;
    moving::SetDestinationFromMessage(o, message, env);
    OBL_CHECK(o.m_pathCursor == nullptr);
    OBL_CHECK_EQ(o.m_BuildingLeavingID, 321);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    OBL_CHECK(env.events[0].kind == FakeEnv::Kind::Release);
    OBL_CHECK(env.events[0].cursor == &route.cursor);
}

OBL_TEST_CASE("moving_target.set_destination_widens_unsigned_and_truncates_x_from_the_stored_float")
{
    Holder h;
    auto& o = h.Object();
    Message message{};
    message.flags = static_cast<int>(0xfffffffeu); // 4294967294: not -2
    message.data = static_cast<int>(0x01000001u); // 16777217 needs 25 bits: the float copy rounds to 16777216, the truncated int does not
    FakeEnv env;
    moving::SetDestinationFromMessage(o, message, env);
    OBL_CHECK_EQ(o.m_DestXYZ[0], 4294967296.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[1], 16777216.0f);
    OBL_CHECK_EQ(env.heightY, 16777217); // y is truncated from the exact integer
    OBL_CHECK_EQ(env.heightX, 0); // x from float 2^32, whose low 32 bits are zero
}

// ---- StepMotion ------------------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("moving_target.step_seek_moves_toward_the_target_and_reports_arrival")
{
    Holder h;
    auto& o = h.Object();
    o.m_behaviourState = 1;
    o.m_vTarget[0] = 100;
    FakeEnv env;
    OBL_CHECK(!moving::StepMotion(o, 0.02f, env));
    OBL_CHECK(o.m_vPosition[0] > 0.0f);
    o.m_vPosition[0] = 99.8f; // within the 0.5 tolerance
    OBL_CHECK(moving::StepMotion(o, 0.0f, env));
    OBL_CHECK(env.events.empty());
}

OBL_TEST_CASE("moving_target.step_unrecognized_behaviour_only_integrates")
{
    Holder h;
    auto& o = h.Object();
    o.m_behaviourState = 0;
    o.m_vVelocity[0] = 2.0f;
    o.m_vTarget[0] = 50;
    FakeEnv env;
    OBL_CHECK(!moving::StepMotion(o, 0.5f, env));
    OBL_CHECK(Near(o.m_vPosition[0], 1.0f, 1e-6f)); // v * dt, the force stayed zero
    o.m_behaviourState = 9;
    moving::StepMotion(o, 0.5f, env);
    OBL_CHECK(Near(o.m_vPosition[0], 2.0f, 1e-6f));
}

OBL_TEST_CASE("moving_target.step_wander_draws_twice_and_keeps_the_force_short")
{
    Holder h;
    auto& o = h.Object();
    o.m_behaviourState = 3;
    o.m_vTarget[0] = 1e6f;
    FakeEnv env;
    env.draws = {250, 199}; // 250 % 200 - 100 = -50 -> -0.5, 199 - 100 = 99 -> 0.99
    moving::StepMotion(o, 0.02f, env);
    OBL_CHECK_EQ(env.events.size(), size_t{2});
    OBL_CHECK(env.events[0].kind == FakeEnv::Kind::Rand && env.events[1].kind == FakeEnv::Kind::Rand);
    OBL_CHECK(Near(o.m_vSteerForce[0], -0.5f, 1e-6f));
    OBL_CHECK(Near(o.m_vSteerForce[1], 0.99f, 1e-6f));
}

OBL_TEST_CASE("moving_target.step_wander_scales_a_force_above_the_limit")
{
    Holder h;
    auto& o = h.Object();
    o.m_behaviourState = 3;
    o.m_maxForce = 1.0f;
    o.m_vSteerForce[0] = 30.0f;
    o.m_vTarget[0] = 1e6f;
    FakeEnv env;
    env.draws = {100, 100}; // noise 0, 0
    moving::StepMotion(o, 0.0f, env);
    OBL_CHECK(Near(o.m_vSteerForce[0], 1.0f, 0.05f)); // the scale is a reciprocal estimate: close, not exact
    OBL_CHECK_EQ(o.m_vSteerForce[1], 0.0f);
}

OBL_TEST_CASE("moving_target.step_follow_off_route_posts_once_and_waits_for_the_followed_object_to_stop")
{
    Holder h;
    auto& o = h.Object();
    o.m_behaviourState = 4;
    o.m_followDistance = 50;
    o.m_followId = 9;
    o.m_vTarget[0] = 10; // 10 away: inside the follow distance
    FakeEnv env;
    env.followMoving = true;
    OBL_CHECK(!moving::StepMotion(o, 0.0f, env));
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    OBL_CHECK(env.events[0].kind == FakeEnv::Kind::Post);
    OBL_CHECK_EQ(env.events[0].message.type, static_cast<int>(obl::messages::MT_REACHED_FOLLOW_DISTANCE));
    OBL_CHECK_EQ(env.events[0].message.sender, 77);
    OBL_CHECK_EQ(o.m_reachedFollowDistance, 1);
    OBL_CHECK(!moving::StepMotion(o, 0.0f, env));
    OBL_CHECK_EQ(env.events.size(), size_t{1}); // not again while the flag is set
    env.followMoving = false;
    OBL_CHECK(moving::StepMotion(o, 0.0f, env));
}

OBL_TEST_CASE("moving_target.step_follow_off_route_outside_the_distance_is_not_arrival")
{
    Holder h;
    auto& o = h.Object();
    o.m_behaviourState = 4;
    o.m_followDistance = 5;
    o.m_vTarget[0] = 100;
    FakeEnv env;
    OBL_CHECK(!moving::StepMotion(o, 0.0f, env));
    OBL_CHECK(env.events.empty());
    OBL_CHECK_EQ(o.m_reachedFollowDistance, 0);
}

OBL_TEST_CASE("moving_target.step_follow_on_a_route_seeks_and_arrives_within_one_and_a_half")
{
    Holder h;
    auto& o = h.Object();
    Route route;
    o.m_behaviourState = 4;
    o.m_pathCursor = &route.cursor;
    o.m_followDistance = 1000; // would be arrival off route
    o.m_vTarget[0] = 20;
    FakeEnv env;
    OBL_CHECK(!moving::StepMotion(o, 0.02f, env));
    o.m_vPosition[0] = 19.0f;
    OBL_CHECK(moving::StepMotion(o, 0.0f, env));
    OBL_CHECK(env.events.empty());
}

OBL_TEST_CASE("moving_target.step_unordered_compare_counts_as_arrived")
{
    Holder h;
    auto& o = h.Object();
    o.m_behaviourState = 1;
    o.m_vTarget[0] = 100;
    o.m_targetTolerance = std::numeric_limits<float>::quiet_NaN(); // an unordered compare reads as below
    FakeEnv env;
    OBL_CHECK(moving::StepMotion(o, 0.0f, env));
}

// ---- UpdateMoveTarget ------------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("moving_target.update_without_a_route_keeps_the_destination")
{
    Holder h;
    auto& o = h.Object();
    o.m_DestXYZ[0] = 11;
    o.m_DestXYZ[1] = 22;
    FakeEnv env;
    moving::UpdateMoveTarget(o, env);
    OBL_CHECK_EQ(o.m_vTarget[0], 11.0f);
    OBL_CHECK_EQ(o.m_vTarget[1], 22.0f);
    OBL_CHECK(env.events.empty());
}

OBL_TEST_CASE("moving_target.update_in_a_building_takes_the_walk_point")
{
    Holder h;
    auto& o = h.Object();
    o.m_walkInBuilding = 1;
    o.m_buildingId = 5;
    o.m_currWalkPt = 2;
    o.m_DestXYZ[2] = 3.0f;
    FakeEnv env;
    env.walkPoint[0] = 40;
    env.walkPoint[1] = 50;
    moving::UpdateMoveTarget(o, env);
    OBL_CHECK_EQ(o.m_DestXYZ[0], 40.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[1], 50.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[2], 3.0f); // the height is left alone
    OBL_CHECK_EQ(o.m_vTarget[1], 50.0f);
}

OBL_TEST_CASE("moving_target.update_in_a_demolished_building_keeps_the_destination")
{
    Holder h;
    auto& o = h.Object();
    o.m_walkInBuilding = 1;
    o.m_buildingId = 5;
    o.m_currWalkPt = 2;
    o.m_DestXYZ[0] = 6.0f;
    o.m_DestXYZ[1] = 7.0f;
    FakeEnv env;
    env.buildingStands = false;
    moving::UpdateMoveTarget(o, env);
    OBL_CHECK_EQ(o.m_DestXYZ[0], 6.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[1], 7.0f);
    OBL_CHECK_EQ(o.m_vTarget[1], 7.0f);
}

OBL_TEST_CASE("moving_target.update_follow_off_route_copies_the_followed_position")
{
    Holder h;
    auto& o = h.Object();
    o.m_behaviourState = 4;
    o.m_followId = 8;
    FakeEnv env;
    env.followPosition[0] = 7;
    env.followPosition[1] = 8;
    env.followPosition[2] = 9;
    moving::UpdateMoveTarget(o, env);
    OBL_CHECK_EQ(o.m_DestXYZ[0], 7.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[1], 8.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[2], 9.0f);
    OBL_CHECK_EQ(o.m_vTarget[0], 7.0f);
    OBL_CHECK_EQ(o.m_vTarget[1], 8.0f);
    o.m_walkInBuilding = 1; // building wins over follow
    env.walkPoint[0] = 1;
    env.walkPoint[1] = 2;
    moving::UpdateMoveTarget(o, env);
    OBL_CHECK_EQ(o.m_DestXYZ[0], 1.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[2], 9.0f);
}

OBL_TEST_CASE("moving_target.update_on_a_route_aims_at_the_step_tile_with_the_lane_offset")
{
    Holder h;
    auto& o = h.Object();
    Route route;
    o.m_pathCursor = &route.cursor;
    FakeEnv env;
    moving::UpdateMoveTarget(o, env); // lane 0
    OBL_CHECK_EQ(o.m_vTarget[0], 128.0f); // (64/2) * 4
    OBL_CHECK_EQ(o.m_vTarget[1], 320.0f); // 64 * 5, even column: no half tile
    o.m_posOffset = 1;
    env.tileQueries = 0;
    moving::UpdateMoveTarget(o, env);
    OBL_CHECK_EQ(o.m_vTarget[0], 143.0f);
    o.m_posOffset = 2;
    env.tileQueries = 0;
    moving::UpdateMoveTarget(o, env);
    OBL_CHECK_EQ(o.m_vTarget[0], 113.0f);
    o.m_posOffset = 7; // any other value takes the second vector
    env.tileQueries = 0;
    moving::UpdateMoveTarget(o, env);
    OBL_CHECK_EQ(o.m_vTarget[0], 113.0f);
    route.cursor.m_currentStep = 0; // tile (2, 3)... odd column check uses (3, 7)
    route.data->m_steps[0] = {3, 7};
    o.m_posOffset = 0;
    env.tileQueries = 0;
    moving::UpdateMoveTarget(o, env);
    OBL_CHECK_EQ(o.m_vTarget[0], 96.0f);
    OBL_CHECK_EQ(o.m_vTarget[1], 64.0f * 7 + 32.0f); // odd column: half a tile lower
    OBL_CHECK_EQ(env.queriedTiles[0][0], 3u);
    OBL_CHECK_EQ(env.queriedTiles[0][1], 7u);
    OBL_CHECK(env.events.empty());
    OBL_CHECK(o.m_pathCursor == &route.cursor);
}

namespace {
constexpr uint32_t kShallow = moving::tile_flags::kShallow;
constexpr uint32_t kScenery = moving::tile_flags::kScenery;
constexpr uint32_t kBlocked = moving::tile_flags::kBlocked;

struct RouteWorld {
    Holder holder;
    Route route;
    FakeEnv env;
    RouteWorld()
    {
        auto& o = holder.Object();
        o.m_pathCursor = &route.cursor;
        o.m_DestXYZ[0] = 300.7f;
        o.m_DestXYZ[1] = -20.9f;
    }
    Mover& Object() { return holder.Object(); }
};
} // namespace

OBL_TEST_CASE("moving_target.update_resends_the_command_of_the_route_type_when_the_route_and_the_own_tile_are_bad")
{
    const struct {
        int pathType;
        int expected;
    } cases[] = {
            {0, obl::messages::MT_PAVEMENT2POINT},
            {1, obl::messages::MT_MOVE2POINT},
            {2, obl::messages::MT_MOVE2SEAPOINT},
            {3, obl::messages::MT_SWIM2SEAPOINT},
            {5, 0},
            {-1, 0}, // outside 0..3 the type word stays 0
    };
    for (const auto& c : cases) {
        RouteWorld w;
        w.route.data->m_pathType = c.pathType;
        w.env.flagsAt[0][0] = kShallow; // the step tile: wading and not blocked
        w.env.flagsAt[1][0] = kBlocked; // the tile the object stands on
        moving::UpdateMoveTarget(w.Object(), w.env);
        OBL_CHECK(w.Object().m_pathCursor == nullptr);
        OBL_CHECK_EQ(w.env.events.size(), size_t{2});
        OBL_CHECK(w.env.events[0].kind == FakeEnv::Kind::Release);
        OBL_CHECK(w.env.events[0].cursor == &w.route.cursor);
        OBL_CHECK(w.env.events[1].kind == FakeEnv::Kind::Post);
        OBL_CHECK_EQ(w.env.events[1].message.type, c.expected);
        OBL_CHECK_EQ(w.env.events[1].message.sender, 77);
        OBL_CHECK_EQ(w.env.events[1].message.receiver, 77);
        OBL_CHECK_EQ(w.env.events[1].message.flags, 300); // truncated toward zero
        OBL_CHECK_EQ(w.env.events[1].message.data, -20);
    }
}

OBL_TEST_CASE("moving_target.update_leaves_a_through_scenery_route_alone")
{
    RouteWorld w;
    w.route.data->m_pathType = 4;
    w.env.flagsAt[0][0] = kScenery;
    w.env.flagsAt[1][0] = kBlocked;
    moving::UpdateMoveTarget(w.Object(), w.env);
    OBL_CHECK(w.Object().m_pathCursor == &w.route.cursor);
    OBL_CHECK_EQ(w.route.data->m_valid, 1);
    OBL_CHECK(w.env.events.empty());
}

OBL_TEST_CASE("moving_target.update_needs_both_a_bad_route_and_a_bad_own_tile")
{
    { // scenery on the step tile, the object itself stands on a free tile
        RouteWorld w;
        w.env.flagsAt[0][0] = kScenery;
        moving::UpdateMoveTarget(w.Object(), w.env);
        OBL_CHECK(w.Object().m_pathCursor == &w.route.cursor);
        OBL_CHECK(w.env.events.empty());
        OBL_CHECK_EQ(w.env.tileQueries, 2); // the own tile was asked for
    }
    { // wading but also blocked: the first test fails and so does the scenery bit; a valid route is kept, own tile never asked
        RouteWorld w;
        w.env.flagsAt[0][0] = kShallow | kBlocked;
        moving::UpdateMoveTarget(w.Object(), w.env);
        OBL_CHECK(w.Object().m_pathCursor == &w.route.cursor);
        OBL_CHECK_EQ(w.env.tileQueries, 1);
    }
    { // plain tile and a valid route: nothing to check
        RouteWorld w;
        moving::UpdateMoveTarget(w.Object(), w.env);
        OBL_CHECK_EQ(w.env.tileQueries, 1);
        OBL_CHECK(w.env.events.empty());
    }
}

OBL_TEST_CASE("moving_target.update_drops_a_route_flagged_invalid_whatever_the_tiles_say")
{
    RouteWorld w;
    w.route.data->m_valid = 0;
    moving::UpdateMoveTarget(w.Object(), w.env); // both tests pass through the invalid flag
    OBL_CHECK(w.Object().m_pathCursor == nullptr);
    OBL_CHECK_EQ(w.env.events.size(), size_t{2});
    OBL_CHECK_EQ(w.env.events[1].message.type, static_cast<int>(obl::messages::MT_MOVE2POINT)); // path type 1
}

OBL_TEST_CASE("moving_target.update_vehicle_paths_are_not_resent_but_turn_back_at_shallow_water")
{
    {
        RouteWorld w;
        w.Object().m_vehiclePath = 1;
        w.env.flagsAt[0][0] = kShallow;
        w.env.flagsAt[1][0] = kBlocked;
        moving::UpdateMoveTarget(w.Object(), w.env);
        OBL_CHECK(w.Object().m_pathCursor == nullptr);
        OBL_CHECK_EQ(w.route.data->m_valid, 1); // the first branch did not run
        OBL_CHECK_EQ(w.env.events.size(), size_t{2});
        OBL_CHECK(w.env.events[0].kind == FakeEnv::Kind::Release);
        OBL_CHECK(w.env.events[1].kind == FakeEnv::Kind::PostDelayed);
        OBL_CHECK_EQ(w.env.events[1].message.type, static_cast<int>(obl::messages::MT_MOVE2SEAPOINT));
        OBL_CHECK_EQ(w.env.events[1].message.flags, 300);
        OBL_CHECK_EQ(w.env.events[1].message.data, -20);
        OBL_CHECK_EQ(w.env.events[1].delay, 3.0f);
    }
    { // vehicle on a tile that is not shallow: nothing, even when the route is unusable for other reasons
        RouteWorld w;
        w.Object().m_vehiclePath = 1;
        w.env.flagsAt[0][0] = kScenery;
        w.env.flagsAt[1][0] = kBlocked;
        moving::UpdateMoveTarget(w.Object(), w.env);
        OBL_CHECK(w.Object().m_pathCursor == &w.route.cursor);
        OBL_CHECK(w.env.events.empty());
    }
}

OBL_TEST_CASE("moving_target.path_point_helper_matches_the_formula")
{
    const auto even = grid_math::TileToPathPoint(64, 4, 5);
    OBL_CHECK_EQ(even.x, 128.0f);
    OBL_CHECK_EQ(even.y, 320.0f);
    const auto odd = grid_math::TileToPathPoint(64, 3, 5);
    OBL_CHECK_EQ(odd.x, 96.0f);
    OBL_CHECK_EQ(odd.y, 352.0f);
    const auto wrapped = grid_math::TileToPathPoint(64, 0xffffffffu, 0); // the 32-bit product wraps and is read as unsigned
    OBL_CHECK_EQ(wrapped.x, static_cast<float>(32u * 0xffffffffu));
    const auto oddSize = grid_math::TileToPathPoint(63, 1, 1); // half = 31
    OBL_CHECK_EQ(oddSize.x, 31.0f);
    OBL_CHECK_EQ(oddSize.y, 63.0f + 31.0f);
}
