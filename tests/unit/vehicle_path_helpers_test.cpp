#include "object_storage.h"
#include "unit_test.h"
#include "game/map/path_data.h"
#include "game/objects/vehicle_helpers.h"
#include "game/messages/message_state.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

// Vehicle route helpers (docs/specs/vehicle.md): MoveToNextCircuitPoint, PickRoamPoint, PickFishingPoint, TurnTowardsPoint over a flat fake grid.
namespace {
using obl::map::GridCell;
using obl::messages::Message;
using obl::object::Vehicle;
namespace helpers = obl::vehicle::helpers;
namespace life = obl::vehicle::lifecycle;

constexpr int kStep[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

// Eight directions as unit steps, every tile water unless a test says otherwise.
struct PathEnv {
    static constexpr uint32_t kWater = 0x20000000;
    std::map<std::pair<uint32_t, uint32_t>, GridCell> cells;
    std::vector<Message> sent;
    std::vector<std::string> calls;
    std::vector<int> draws;
    int32_t spriteDirection = 0;
    bool straight = false;
    bool dropCursorOnPost = false;
    Vehicle* live = nullptr;
    uint32_t tile[2] = {10, 10};

    GridCell& Cell(uint32_t x, uint32_t y)
    {
        auto [it, fresh] = cells.try_emplace({x, y});
        if (fresh) it->second.m_flags = kWater;
        return it->second;
    }
    GridCell* CellAt(uint32_t x, uint32_t y) { return &Cell(x, y); }
    void Neighbour(uint32_t x, uint32_t y, int32_t direction, uint32_t out[2])
    {
        OBL_CHECK(direction >= 0 && direction < 8);
        out[0] = static_cast<uint32_t>(static_cast<int>(x) + kStep[direction][0]);
        out[1] = static_cast<uint32_t>(static_cast<int>(y) + kStep[direction][1]);
    }
    void NeighbourCells(uint32_t x, uint32_t y, GridCell* out[8])
    {
        for (int i = 0; i < 8; ++i) {
            uint32_t at[2];
            Neighbour(x, y, i, at);
            out[i] = &Cell(at[0], at[1]);
        }
    }
    void TilePosition(const Vehicle&, uint32_t out[2])
    {
        out[0] = tile[0];
        out[1] = tile[1];
    }
    void TileToWorldPoint(uint32_t x, uint32_t y, float out[3])
    {
        out[0] = x * 10 + 0.75f;
        out[1] = y * 10 + 0.25f;
        out[2] = 5;
    }
    void TileToPathPoint(uint32_t x, uint32_t y, float out[3])
    {
        out[0] = x * 100 + 0.5f;
        out[1] = y * 100 + 1.5f;
        out[2] = 0;
    }
    int Rand()
    {
        OBL_CHECK(!draws.empty());
        if (draws.empty()) return 0;
        const int value = draws.front();
        draws.erase(draws.begin());
        return value;
    }
    int32_t Direction(void*) { return spriteDirection; }
    void SetMoving(Vehicle& vehicle, bool moving)
    {
        calls.push_back("moving");
        vehicle.m_moving = moving;
    }
    void TurnToDirection(Vehicle&, int32_t direction) { calls.push_back("turn " + std::to_string(direction)); }
    bool PathIsStraight(const Vehicle&) { return straight; }
    void Post(const Message& message)
    {
        sent.push_back(message);
        if (dropCursorOnPost) live->m_pathCursor = nullptr;
    }
    void BlockBerth(uint32_t x, uint32_t y, bool blocked)
    {
        for (int i = 0; i < 8; ++i) {
            uint32_t at[2];
            Neighbour(x, y, i, at);
            if (blocked)
                Cell(at[0], at[1]).m_flags |= 0x10;
            else
                Cell(at[0], at[1]).m_flags &= ~0x10u;
        }
    }
    bool BerthBlocked(uint32_t x, uint32_t y)
    {
        bool all = true;
        for (int i = 0; i < 8; ++i) {
            uint32_t at[2];
            Neighbour(x, y, i, at);
            all &= (Cell(at[0], at[1]).m_flags & 0x10) != 0;
        }
        return all;
    }
};

// The sector chosen for a target (dx, dy) away from a boat at the origin.
int Sector(float dx, float dy)
{
    PathEnv env;
    Vehicle vehicle{};
    helpers::TurnTowardsPoint(vehicle, dx - 1, dy - 1, env); // the helper shifts the target by (1, 1)
    OBL_CHECK(env.calls.size() == 1);
    return env.calls.empty() ? -1 : std::stoi(env.calls[0].substr(5));
}
} // namespace

OBL_TEST_CASE("Vehicle circuit: next free point, blocked points, the way round and the speed from the path")
{
    uint32_t points[4][2] = {{1, 1}, {2, 2}, {3, 3}, {4, 4}};
    obl::path::RouteCursor cursorMarker;
    auto make = [&](PathEnv& env, Vehicle& vehicle, uint32_t current, uint32_t start) {
        obl::unit::ResetObject(vehicle);
        env.live = &vehicle;
        vehicle.m_id = 9;
        vehicle.m_vehicleType = life::Jetski;
        vehicle.m_circuitPointCount = 4;
        vehicle.m_circuitPoints = points;
        vehicle.m_currCircuitPoint = current;
        vehicle.m_startCircuitPoint = start;
        vehicle.m_maxVel = 1;
    };
    { // The next point is free: move order to its path point (truncated), roam point is its world point.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 0, 3);
        helpers::MoveToNextCircuitPoint(vehicle, env);
        OBL_CHECK(vehicle.m_currCircuitPoint == 1);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].type == obl::messages::MT_SWIM2SEAPOINT);
        OBL_CHECK(env.sent[0].sender == 9);
        OBL_CHECK(env.sent[0].receiver == 9);
        OBL_CHECK(env.sent[0].flags == 200);
        OBL_CHECK(env.sent[0].data == 201);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[0] == 20.75f);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[1] == 20.25f);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[2] == 5);
        OBL_CHECK(vehicle.m_maxVel == 1); // no path cursor, no speed
    }
    { // Blocked points are skipped.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 0, 3);
        env.Cell(2, 2).m_flags |= 0x10;
        helpers::MoveToNextCircuitPoint(vehicle, env);
        OBL_CHECK(vehicle.m_currCircuitPoint == 2);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].flags == 300);
    }
    { // The way round ends at the start: stop message, and the roam point is still taken from the point reached.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 0, 3);
        env.Cell(2, 2).m_flags |= 0x10;
        env.Cell(3, 3).m_flags |= 0x10;
        helpers::MoveToNextCircuitPoint(vehicle, env);
        // Points 1 and 2 are blocked and point 3 is the start: the search stops there.
        OBL_CHECK(vehicle.m_currCircuitPoint == 3);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].type == obl::messages::MT_STATECHANGE);
        OBL_CHECK(env.sent[0].flags == 0);
        OBL_CHECK(env.sent[0].data == 0x42);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[0] == 40.75f);
    }
    { // The next point is the start itself: stop without looking at any cell.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 2, 3);
        helpers::MoveToNextCircuitPoint(vehicle, env);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].type == obl::messages::MT_STATECHANGE);
        OBL_CHECK(env.cells.empty());
    }
    { // The index wraps around the table.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 3, 2);
        helpers::MoveToNextCircuitPoint(vehicle, env);
        OBL_CHECK(vehicle.m_currCircuitPoint == 0);
    }
    for (int type : {life::Jetski, life::SpeedBoat})
        for (bool straight : {false, true}) { // speed of a boat on a path
            PathEnv env;
            Vehicle vehicle;
            make(env, vehicle, 0, 3);
            vehicle.m_vehicleType = type;
            vehicle.m_pathCursor = &cursorMarker;
            env.straight = straight;
            helpers::MoveToNextCircuitPoint(vehicle, env);
            OBL_CHECK(vehicle.m_maxVel == (type == life::SpeedBoat ? 1.0f : straight ? 200.0f : 100.0f));
        }
    { // A cursor dropped by the immediate delivery of the order is not asked.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 0, 3);
        vehicle.m_pathCursor = &cursorMarker;
        env.dropCursorOnPost = true;
        env.straight = true;
        helpers::MoveToNextCircuitPoint(vehicle, env);
        OBL_CHECK(vehicle.m_maxVel == 1);
    }
}

OBL_TEST_CASE("Vehicle roaming: first long run wins, else the longest, nothing free, move or turn")
{
    auto make = [](PathEnv& env, Vehicle& vehicle, int facing) {
        obl::unit::ResetObject(vehicle);
        env.live = &vehicle;
        env.spriteDirection = facing;
        vehicle.m_id = 4;
        vehicle.m_moving = 1;
    };
    { // Direction 2 (east) runs 8 tiles at once; the boat already faces direction 3 (= 2 + 1), so the order is sent.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 3);
        env.draws = {2};
        helpers::PickRoamPoint(vehicle, env);
        OBL_CHECK(vehicle.m_moving == 0);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[0] == 18 * 10 + 0.75f);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[1] == 10 * 10 + 0.25f);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].type == obl::messages::MT_MOVE2SEAPOINT);
        OBL_CHECK(env.sent[0].flags == 180);
        OBL_CHECK(env.sent[0].data == 100);
        OBL_CHECK((env.calls == std::vector<std::string>{"moving"}));
    }
    { // Not facing it yet: turn towards (direction + 1) % 8.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 6);
        env.draws = {7}; // direction 7 runs 8 tiles to the north-west
        helpers::PickRoamPoint(vehicle, env);
        OBL_CHECK((env.calls == std::vector<std::string>{"moving", "turn 0"}));
        OBL_CHECK(env.sent.empty());
    }
    { // Every direction is blocked after three tiles but south (four): the longest run wins, searched from the drawn direction round.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 0);
        env.draws = {0};
        for (int d = 0; d < 8; ++d) {
            const int length = d == 4 ? 4 : 3;
            env.Cell(10 + kStep[d][0] * (length + 1), 10 + kStep[d][1] * (length + 1)).m_flags = 0;
        }
        helpers::PickRoamPoint(vehicle, env);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[0] == 10 * 10 + 0.75f);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[1] == 14 * 10 + 0.25f);
        OBL_CHECK((env.calls == std::vector<std::string>{"moving", "turn 5"}));
    }
    { // Equal runs keep the first one found, even when later ones tie.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 0);
        env.draws = {1};
        for (int d = 0; d < 8; ++d) env.Cell(10 + kStep[d][0] * 4, 10 + kStep[d][1] * 4).m_flags = 0;
        helpers::PickRoamPoint(vehicle, env);
        OBL_CHECK((env.calls == std::vector<std::string>{"moving", "turn 2"}));
    }
    { // Water outside the border and blocked tiles end a run; with no tile at all the stop message is sent and the boat is not stopped.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 0);
        env.draws = {5};
        env.tile[0] = 2;
        env.tile[1] = 2;
        for (int d = 0; d < 8; ++d) env.Cell(2 + kStep[d][0], 2 + kStep[d][1]).m_flags = d % 2 ? 0 : PathEnv::kWater | 0x10;
        helpers::PickRoamPoint(vehicle, env);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].type == obl::messages::MT_STATECHANGE);
        OBL_CHECK(env.sent[0].data == 0x42);
        OBL_CHECK(env.calls.empty());
        OBL_CHECK(vehicle.m_moving == 1);
    }
    { // x = 1 is outside the playable border even on open water.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 0);
        env.draws = {6};
        env.tile[0] = 3;
        for (int d = 0; d < 8; ++d) env.Cell(3 + kStep[d][0], 10 + kStep[d][1]).m_flags = d == 6 ? PathEnv::kWater : 0;
        env.tile[1] = 10;
        helpers::PickRoamPoint(vehicle, env);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[0] == 2 * 10 + 0.75f); // one tile west reaches x = 2, the next (x = 1) is refused
    }
}

OBL_TEST_CASE("Vehicle fishing: spot of five tiles, ring check, berth cells, move or turn")
{
    auto make = [](PathEnv& env, Vehicle& vehicle, int unload, int facing) {
        obl::unit::ResetObject(vehicle);
        env.live = &vehicle;
        env.spriteDirection = facing;
        vehicle.m_id = 6;
        vehicle.m_moving = 1;
        vehicle.m_unloadDir = unload;
        vehicle.m_CurrGridXY[0] = 40;
        vehicle.m_CurrGridXY[1] = 40;
    };
    { // (unload 1 + 2) % 8 = 3: the steps use direction (3 + 7) % 8 = 2 (east), 15 free tiles; the berth is blocked again afterwards.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 1, 7);
        env.BlockBerth(40, 40, true);
        OBL_CHECK(helpers::PickFishingPoint(vehicle, env));
        OBL_CHECK(vehicle.m_CurrentRoamPoint[0] == 25 * 10 + 0.75f);
        OBL_CHECK(vehicle.m_CurrentRoamPoint[1] == 10 * 10 + 0.25f);
        OBL_CHECK(env.BerthBlocked(40, 40));
        OBL_CHECK(vehicle.m_moving == 0);
        OBL_CHECK((env.calls == std::vector<std::string>{"moving", "turn 3"}));
    }
    { // Already facing it: a move order instead of the turn.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 1, 3);
        OBL_CHECK(helpers::PickFishingPoint(vehicle, env));
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].type == obl::messages::MT_MOVE2SEAPOINT);
        OBL_CHECK(env.sent[0].flags == 250);
        OBL_CHECK(env.sent[0].data == 100);
        OBL_CHECK(env.calls == std::vector<std::string>{"moving"});
    }
    { // Four tiles are too few; the berth stays free, nothing is moved or ordered.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 1, 3);
        env.Cell(15, 10).m_flags = 0;
        env.BlockBerth(40, 40, true);
        OBL_CHECK(!helpers::PickFishingPoint(vehicle, env));
        OBL_CHECK(!env.BerthBlocked(40, 40));
        for (int i = 0; i < 8; ++i) {
            uint32_t at[2];
            env.Neighbour(40, 40, i, at);
            OBL_CHECK((env.Cell(at[0], at[1]).m_flags & 0x10) == 0);
        }
        OBL_CHECK(env.calls.empty());
        OBL_CHECK(env.sent.empty());
        OBL_CHECK(vehicle.m_moving == 1);
    }
    { // Five tiles are enough (the sixth is refused by its ring).
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 1, 3);
        env.Cell(17, 10).m_flags = 0; // in the ring of the sixth tile only
        OBL_CHECK(helpers::PickFishingPoint(vehicle, env));
        OBL_CHECK(vehicle.m_CurrentRoamPoint[0] == 15 * 10 + 0.75f);
    }
    { // From the third tile on a blocked neighbour stops the search; a blocked neighbour of the first two does not.
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, 1, 3);
        env.Cell(13, 11).m_flags |= 0x10; // beside the third tile east of (10, 10)
        OBL_CHECK(!helpers::PickFishingPoint(vehicle, env));
        PathEnv early;
        make(early, vehicle, 1, 3);
        early.Cell(11, 11).m_flags |= 0x10;
        early.Cell(12, 12).m_flags |= 0x10;
        // (11, 11) touches only the first two tiles' rings, (12, 12) none: the spot is still found.
        OBL_CHECK(helpers::PickFishingPoint(vehicle, early));
    }
    { // Negative unload direction: the signed remainder keeps (-3 + 2) % 8 = -1 and steps with (-1 + 7) % 8 = 6 (west) until the border (x = 2).
        PathEnv env;
        Vehicle vehicle;
        make(env, vehicle, -3, -1);
        OBL_CHECK(helpers::PickFishingPoint(vehicle, env));
        OBL_CHECK(vehicle.m_CurrentRoamPoint[0] == 2 * 10 + 0.75f);
        OBL_CHECK(env.calls == std::vector<std::string>{"moving"});
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK(env.sent[0].type == obl::messages::MT_MOVE2SEAPOINT);
    }
}

OBL_TEST_CASE("Vehicle aiming: sectors, the offset of one, a target on the boat")
{
    OBL_CHECK(Sector(100, 0) == 2); // east
    OBL_CHECK(Sector(0, 100) == 4); // south (y grows downwards on the map)
    OBL_CHECK(Sector(-100, 0) == 6); // west
    OBL_CHECK(Sector(0, -100) == 0); // north
    OBL_CHECK(Sector(100, 100) == 3);
    OBL_CHECK(Sector(-100, 100) == 5);
    OBL_CHECK(Sector(-100, -100) == 7);
    OBL_CHECK(Sector(100, -100) == 1);
    // Zero length: the reciprocal estimate overflows to infinity, 0 * inf is NaN and NaN selects the south sector (the math direction from vector routine).
    OBL_CHECK(Sector(0, 0) == 4);
    // The boat position is used as it is (not as a tile): a target 100 units east of a boat at (30, 40).
    PathEnv env;
    Vehicle vehicle{};
    vehicle.m_vPosition[0] = 30;
    vehicle.m_vPosition[1] = 40;
    helpers::TurnTowardsPoint(vehicle, 129, 39, env);
    OBL_CHECK(env.calls == std::vector<std::string>{"turn 2"});
}
