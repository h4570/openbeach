#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/mover_events.h"
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
        Object().m_id = 41;
        Object().m_state = 12;
        Object().m_mass = 1.0f;
        Object().m_maxVel = 10.0f;
        Object().m_maxForce = 1000.0f;
        Object().m_targetTolerance = 0.5f;
        Object().m_type = object::ObjectType::Scenery;
    }
    Mover& Object() { return *reinterpret_cast<Mover*>(bytes); }
};

// Records the effects in order and answers the queries from plain fields.
struct FakeEnv {
    enum class Kind { Post, PostDelayed, SendDelayed, Release, Teleport, Sprite };
    struct Event {
        Kind kind;
        Message message{};
        float delay = 0;
        float x = 0, y = 0, z = 0;
    };
    std::vector<Event> events;
    bool followMoving = false;
    int32_t tileSize = 64;
    uint32_t tileFlags = 0;
    float delta = 0.0f;

    void Post(const Message& m) { events.push_back({Kind::Post, m}); }
    void PostDelayed(const Message& m, float delay) { events.push_back({Kind::PostDelayed, m, delay}); }
    void SendDelayed(Mover&, const Message& m, float delay) { events.push_back({Kind::SendDelayed, m, delay}); }
    void ReleaseCursor(RouteCursor*) { events.push_back({Kind::Release}); }
    int Rand() { return 0; }
    float TerrainHeight(int32_t, int32_t) { return 0.0f; }
    bool ObjectMoving(int32_t) { return followMoving; }
    void ObjectPosition(int32_t, float out[3]) { out[0] = out[1] = out[2] = 0.0f; }
    bool BuildingWalkPoint(int32_t, uint32_t, float out[2])
    {
        out[0] = out[1] = 0.0f;
        return true;
    }
    int32_t TileSize() { return tileSize; }
    uint32_t TileFlags(uint32_t, uint32_t) { return tileFlags; }
    void WorldToTile(float, float, float, uint32_t out[2]) { out[0] = out[1] = 0; }
    void PositionOffset(int, float out[2]) { out[0] = out[1] = 0.0f; }
    float WorldDelta() { return delta; }
    void Teleport(Mover&, float x, float y, float z)
    {
        Event e{Kind::Teleport};
        e.x = x, e.y = y, e.z = z;
        events.push_back(e);
    }
    void SetSpriteDirection(void*, float x, float y)
    {
        Event e{Kind::Sprite};
        e.x = x, e.y = y;
        events.push_back(e);
    }
    size_t Count(Kind kind) const
    {
        size_t n = 0;
        for (const Event& e : events) n += e.kind == kind;
        return n;
    }
    const Event& First(Kind kind) const
    {
        for (const Event& e : events)
            if (e.kind == kind) return e;
        return events.front();
    }
};

// Five tiles in one column: path points (32*0, 64*y) for even x.
struct Route {
    std::shared_ptr<path::Route> data = std::make_shared<path::Route>();
    RouteCursor cursor{};
    explicit Route(int step, bool reverse = false)
    {
        data->m_steps = {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}};
        data->m_valid = 1;
        data->m_pathType = 1;
        data->m_stop[0] = 0;
        data->m_stop[1] = 5;
        cursor.m_route = data;
        cursor.m_reverse = reverse;
        cursor.m_currentStep = step;
    }
};
} // namespace

// ---- the math direction from vector routine ------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("moving_events.direction_sectors_follow_acos_of_minus_y_mirrored_for_negative_x")
{
    OBL_CHECK_EQ(path::DirectionIndex(0.0f, -1.0f, 8), 0); // up the screen
    OBL_CHECK_EQ(path::DirectionIndex(1.0f, 0.0f, 8), 2);
    OBL_CHECK_EQ(path::DirectionIndex(0.0f, 1.0f, 8), 4);
    OBL_CHECK_EQ(path::DirectionIndex(-1.0f, 0.0f, 8), 6);
    OBL_CHECK_EQ(path::DirectionIndex(0.7f, -0.7f, 8), 1);
    OBL_CHECK_EQ(path::DirectionIndex(-0.7f, -0.7f, 8), 7);
    // NaN y is clamped to -1 (pi, sector 4), NaN x mirrors it (still sector 4); slightly past 1 is clamped.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(path::DirectionIndex(1.0f, nan, 8), 4);
    OBL_CHECK_EQ(path::DirectionIndex(nan, -1.0001f, 8), 0);
    // A sector boundary rounds up to the next direction (odd half-sectors are bumped).
    OBL_CHECK_EQ(path::DirectionIndex(std::sin(0.4f), -std::cos(0.4f), 8), 1);
}

// ---- OnMoveTargetReached ----------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("moving_events.reached_past_the_route_end_releases_it_and_does_nothing_else")
{
    Holder h;
    auto& o = h.Object();
    Route route(4);
    o.m_pathCursor = &route.cursor;
    o.m_moving = 1;
    FakeEnv env;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK(o.m_pathCursor == nullptr);
    OBL_CHECK_EQ(o.m_moving, 1);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    OBL_CHECK(env.events[0].kind == FakeEnv::Kind::Release);
}

OBL_TEST_CASE("moving_events.reached_second_to_last_point_reports_last_point_with_the_state")
{
    Holder h;
    auto& o = h.Object();
    Route route(2);
    o.m_pathCursor = &route.cursor;
    o.m_posOffset = 1;
    FakeEnv env;
    env.tileFlags = moving::tile_flags::kSea; // the last-point report wins over the sea test
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(route.cursor.m_currentStep, 3);
    OBL_CHECK_EQ(o.m_posOffset, 0u);
    OBL_CHECK_EQ(o.m_enteredTheSea, 0);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    OBL_CHECK(env.events[0].kind == FakeEnv::Kind::PostDelayed);
    OBL_CHECK_EQ(env.events[0].message.type, static_cast<int>(obl::messages::MT_LAST_POINT));
    OBL_CHECK_EQ(env.events[0].message.extra, 12);
    OBL_CHECK_EQ(env.events[0].delay, 0.01f);

    // Reversed, the last point is index 0 and a guest sends through its own slot.
    Route back(2, true);
    o.m_pathCursor = &back.cursor;
    o.m_type = object::ObjectType::Guest;
    FakeEnv env2;
    moving::OnMoveTargetReached(o, env2);
    OBL_CHECK_EQ(back.cursor.m_currentStep, 1);
    OBL_CHECK_EQ(env2.events.size(), size_t{0});
    moving::OnMoveTargetReached(o, env2);
    OBL_CHECK_EQ(env2.events.size(), size_t{1});
    OBL_CHECK(env2.events[0].kind == FakeEnv::Kind::SendDelayed);
}

OBL_TEST_CASE("moving_events.reached_open_sea_once_reports_entered_sea_with_the_truncated_position")
{
    Holder h;
    auto& o = h.Object();
    Route route(0);
    o.m_pathCursor = &route.cursor;
    o.m_WorldXYZ[0] = 100.9f;
    o.m_WorldXYZ[1] = 250.2f;
    FakeEnv env;
    env.tileFlags = moving::tile_flags::kSea | moving::tile_flags::kShallow; // shallow is not the sea
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(env.events.size(), size_t{0});
    env.tileFlags = moving::tile_flags::kSea;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_enteredTheSea, 1);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    OBL_CHECK_EQ(env.events[0].message.type, static_cast<int>(obl::messages::MT_ENTERED_SEA));
    OBL_CHECK_EQ(env.events[0].message.flags, 100);
    OBL_CHECK_EQ(env.events[0].message.data, 250);
    OBL_CHECK_EQ(env.events[0].message.extra, 12);
    // Already in the sea: no second report.
    route.cursor.m_currentStep = 0;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
}

OBL_TEST_CASE("moving_events.reached_sea_vehicle_stops_to_turn_when_the_direction_changes")
{
    Holder h;
    auto& o = h.Object();
    Route route(0);
    o.m_pathCursor = &route.cursor;
    o.m_vehiclePath = 1;
    o.m_turnType = 1;
    o.m_moving = 1;
    o.m_enteredTheSea = 1;
    o.m_vPosition[0] = 0.0f;
    o.m_vPosition[1] = 200.0f; // next tile (0, 2) is at y 128: straight up, direction 0
    o.m_lastDir = 2;
    FakeEnv env;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_lastDir, 0u);
    OBL_CHECK_EQ(o.m_moving, 0);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    OBL_CHECK(env.events[0].kind == FakeEnv::Kind::PostDelayed);
    OBL_CHECK_EQ(env.events[0].message.type, static_cast<int>(obl::messages::MT_VEHICLE_STOP_TURN));
    OBL_CHECK_EQ(env.events[0].message.flags, 0);
    // Same direction again: no message, still moving.
    o.m_moving = 1;
    o.m_vPosition[1] = 300.0f;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_moving, 1);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    // Turn type 0 does not turn here.
    o.m_turnType = 0;
    o.m_lastDir = 5;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_lastDir, 5u);
}

OBL_TEST_CASE("moving_events.reached_without_route_stops_and_reports_move_im_here")
{
    Holder h;
    auto& o = h.Object();
    o.m_moving = 1;
    o.m_lastTimeRemaining = 0.05f;
    o.m_vVelocity[0] = 2;
    o.m_vSteerForce[1] = 3;
    o.m_waitingForMoveImHere = 1;
    o.m_messageReceived = 4.0f;
    FakeEnv env;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_moving, 0);
    OBL_CHECK_EQ(o.m_lastTimeRemaining, 0.0f);
    OBL_CHECK_EQ(o.m_vVelocity[0], 0.0f);
    OBL_CHECK_EQ(o.m_vSteerForce[1], 0.0f);
    OBL_CHECK_EQ(o.m_waitingForMoveImHere, 0);
    OBL_CHECK_EQ(o.m_messageReceived, 0.0f);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
    OBL_CHECK_EQ(env.events[0].message.type, static_cast<int>(obl::messages::MT_MOVEIMHERE));
    OBL_CHECK_EQ(env.events[0].message.extra, 12);
}

OBL_TEST_CASE("moving_events.reached_follower_waits_for_the_followed_object_to_stop")
{
    Holder h;
    auto& o = h.Object();
    o.m_moving = 1;
    o.m_followObject = 1;
    o.m_followId = 9;
    o.m_behaviourState = 4;
    o.m_lastTimeRemaining = 0.05f;
    FakeEnv env;
    env.followMoving = true;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_moving, 1);
    OBL_CHECK_EQ(o.m_lastTimeRemaining, 0.0f); // reset even while waiting
    OBL_CHECK_EQ(env.events.size(), size_t{0});
    env.followMoving = false;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_moving, 0);
    OBL_CHECK_EQ(o.m_followObject, 0);
    OBL_CHECK_EQ(o.m_followId, -1);
    OBL_CHECK_EQ(o.m_behaviourState, 1u);
    OBL_CHECK_EQ(env.events.size(), size_t{1});
}

OBL_TEST_CASE("moving_events.reached_building_walk_ends_after_the_last_point_or_below_zero")
{
    Holder h;
    auto& o = h.Object();
    o.m_moving = 1;
    o.m_walkInBuilding = 1;
    o.m_numWalkPts = 3;
    o.m_currWalkPt = 1;
    FakeEnv env;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_currWalkPt, 2u);
    OBL_CHECK_EQ(env.events.size(), size_t{0});
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_currWalkPt, 3u);
    OBL_CHECK_EQ(o.m_walkInBuilding, 0);
    OBL_CHECK_EQ(o.m_moving, 0);
    OBL_CHECK_EQ(env.events.size(), size_t{1});

    o.m_moving = 1;
    o.m_walkInBuilding = 1;
    o.m_buildingTraversal = 1;
    o.m_currWalkPt = 0;
    moving::OnMoveTargetReached(o, env);
    OBL_CHECK_EQ(o.m_currWalkPt, 0xffffffffu);
    OBL_CHECK_EQ(o.m_walkInBuilding, 0);
    OBL_CHECK_EQ(env.events.size(), size_t{2});
}

// The traversal load is a byte copy; every nonzero value takes the backwards branch.
OBL_TEST_CASE("moving_events.traversal_byte_flags_have_the_same_nonzero_behavior")
{
    for (const uint8_t flag : {uint8_t{1}, uint8_t{0x4c}, uint8_t{0xa8}, uint8_t{255}}) {
        Holder h;
        auto& o = h.Object();
        o.m_moving = 1;
        o.m_walkInBuilding = 1;
        o.m_buildingTraversal = flag;
        o.m_numWalkPts = 3;
        o.m_currWalkPt = 0;
        FakeEnv env;
        moving::OnMoveTargetReached(o, env);
        OBL_CHECK_EQ(o.m_currWalkPt, 0xffffffffu);
        OBL_CHECK_EQ(o.m_walkInBuilding, 0);
        OBL_CHECK_EQ(o.m_moving, 0);
        OBL_CHECK_EQ(env.events.size(), size_t{1});
    }
}

// ---- Update -----------------------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("moving_events.update_stopped_object_does_nothing_but_clear_the_debug_break")
{
    Holder h;
    auto& o = h.Object();
    o.m_debugBreak = 1;
    o.m_vPosition[0] = -5.0f;
    FakeEnv env;
    env.delta = 1.0f;
    OBL_CHECK(moving::Update(o, env));
    OBL_CHECK_EQ(o.m_debugBreak, 0);
    OBL_CHECK_EQ(o.m_vPosition[0], -5.0f);
    OBL_CHECK_EQ(o.m_lastTimeRemaining, 0.0f);
    OBL_CHECK_EQ(env.events.size(), size_t{0});
}

OBL_TEST_CASE("moving_events.update_steps_in_fiftieths_and_keeps_the_remainder")
{
    Holder h;
    auto& o = h.Object();
    o.m_moving = 1;
    o.m_behaviourState = 1;
    o.m_vPosition[0] = 100.0f;
    o.m_vPosition[1] = 100.0f;
    o.m_DestXYZ[0] = 1000.0f;
    o.m_DestXYZ[1] = 100.0f;
    o.m_maxVel = 50.0f;
    FakeEnv env;
    env.delta = 0.05f;
    OBL_CHECK(moving::Update(o, env));
    // 0.05 -> two steps of 0.02, about 0.01 left (float arithmetic).
    OBL_CHECK(std::fabs(o.m_lastTimeRemaining - 0.01f) < 1e-6f);
    OBL_CHECK(o.m_vPosition[0] > 100.0f);
    OBL_CHECK_EQ(env.Count(FakeEnv::Kind::Teleport), size_t{1});
    OBL_CHECK_EQ(env.events.back().x, o.m_vPosition[0]);
    OBL_CHECK_EQ(env.events.back().z, 0.0f);
    // Exactly one step's worth is not enough: the comparison is strict.
    o.m_lastTimeRemaining = 0.0f;
    const float x = o.m_vPosition[0];
    env.delta = 0.02f;
    moving::Update(o, env);
    OBL_CHECK_EQ(o.m_vPosition[0], x);
    OBL_CHECK_EQ(o.m_lastTimeRemaining, 0.02f);
}

OBL_TEST_CASE("moving_events.update_clamps_to_the_map_and_nan_goes_to_zero")
{
    Holder h;
    auto& o = h.Object();
    o.m_moving = 1;
    o.m_vPosition[0] = std::numeric_limits<float>::quiet_NaN();
    o.m_vPosition[1] = 9000.0f;
    FakeEnv env;
    moving::Update(o, env);
    OBL_CHECK_EQ(moving::Bits(o.m_vPosition[0]), 0u);
    OBL_CHECK_EQ(o.m_vPosition[1], 8192.0f);
    o.m_vPosition[0] = 5000.0f;
    o.m_vPosition[1] = -0.0f; // -0 is not below zero
    moving::Update(o, env);
    OBL_CHECK_EQ(o.m_vPosition[0], 4096.0f);
    OBL_CHECK_EQ(moving::Bits(o.m_vPosition[1]), 0x80000000u);
}

OBL_TEST_CASE("moving_events.update_arrival_stops_the_loop")
{
    Holder h;
    auto& o = h.Object();
    o.m_moving = 1;
    o.m_behaviourState = 1;
    o.m_targetTolerance = 1000.0f; // every step arrives
    o.m_vPosition[0] = o.m_vPosition[1] = 10.0f;
    FakeEnv env;
    env.delta = 0.2f;
    moving::Update(o, env);
    OBL_CHECK_EQ(o.m_moving, 0);
    OBL_CHECK_EQ(o.m_lastTimeRemaining, 0.0f);
    OBL_CHECK_EQ(env.Count(FakeEnv::Kind::PostDelayed), size_t{1}); // one MT_MOVEIMHERE
    OBL_CHECK_EQ(env.Count(FakeEnv::Kind::Sprite), size_t{0}); // no longer moving
}

OBL_TEST_CASE("moving_events.update_turns_the_sprite_along_the_velocity")
{
    Holder h;
    auto& o = h.Object();
    o.m_moving = 1;
    o.m_controlDirection = 1;
    o.m_vVelocity[0] = 3.0f;
    o.m_vVelocity[1] = -4.0f;
    o.m_vPosition[0] = o.m_vPosition[1] = 10.0f;
    FakeEnv env;
    moving::Update(o, env);
    OBL_CHECK_EQ(env.Count(FakeEnv::Kind::Sprite), size_t{1});
    OBL_CHECK(std::fabs(env.events.back().x - 0.6f) < 0.01f);
    OBL_CHECK(std::fabs(env.events.back().y + 0.8f) < 0.01f);
    // A zero velocity has length 0, whose reciprocal estimate overflows to infinity: the facing is infinity times zero.
    o.m_vVelocity[0] = o.m_vVelocity[1] = 0.0f;
    moving::Update(o, env);
    OBL_CHECK(std::isnan(env.events.back().x));
}

OBL_TEST_CASE("moving_events.update_smooth_turn_fires_once_when_the_route_end_is_near")
{
    Holder h;
    auto& o = h.Object();
    Route route(1);
    o.m_pathCursor = &route.cursor;
    o.m_moving = 1;
    o.m_vehiclePath = 1;
    o.m_turnType = 0;
    o.m_animTimeDiv2 = 2.0f;
    o.m_enteredTheSea = 1;
    o.m_behaviourState = 0; // no steering: the velocity stays as set
    o.m_maxVel = 1000.0f;
    o.m_vPosition[0] = 0.0f;
    o.m_vPosition[1] = 300.0f; // route end (0, 5) is at y 320: 20 away
    o.m_vVelocity[1] = 5.0f; // 4 s away: no turn yet
    o.m_lastTimeRemaining = 0.021f;
    FakeEnv env;
    moving::Update(o, env);
    OBL_CHECK_EQ(env.Count(FakeEnv::Kind::Post), size_t{0});
    OBL_CHECK_EQ(o.m_animTimeDiv2, 2.0f);
    o.m_vVelocity[1] = 50.0f; // 0.4 s away
    o.m_lastTimeRemaining = 0.021f;
    moving::Update(o, env);
    OBL_CHECK_EQ(env.Count(FakeEnv::Kind::Post), size_t{1});
    OBL_CHECK_EQ(env.First(FakeEnv::Kind::Post).message.type, static_cast<int>(obl::messages::MT_VEHICLE_SMOOTH_TURN));
    OBL_CHECK_EQ(o.m_animTimeDiv2, 0.0f);
}
