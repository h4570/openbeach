#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "unit_test.h"

#include "game/map/grid_state.h"
#include "game/objects/staff_lifeguard_fns.h"

#include <cstring>
#include <vector>

// The seven LifeguardFn_* helpers (src/common/lifeguard_fns.h) on a small staggered grid of 64-unit tiles.

using namespace obl;
using namespace obl::staff::lifeguard;
using map::Area;
using map::GridCell;

namespace {

struct TestGrid {
    obl::map::Grid grid{};
    std::vector<map::GridCell>& cells = grid.m_cells;
    explicit TestGrid(uint32_t size, uint32_t flags = 2)
    {
        grid.m_tileSize = 64;
        grid.m_width = grid.m_height = size;
        cells.resize(size * size);
        for (GridCell& cell : cells) cell.m_flags = flags;
    }
};

Area MakeArea(const std::vector<uint32_t>& points, int minX, int minY, int maxX, int maxY)
{
    Area area;
    for (size_t i = 0; i + 1 < points.size(); i += 2) area.m_points.push_back({points[i], points[i + 1]});
    area.m_minX = minX;
    area.m_minY = minY;
    area.m_maxX = maxX;
    area.m_maxY = maxY;
    return area;
}

struct Env {
    std::vector<int> draws;
    size_t used = 0;
    std::vector<obl::messages::Message> sent;
    object::Base home{};
    ui::SpriteInstance homeSprite{};
    object::Guest guest{};
    float serve[3] = {1000.0f, 2000.0f, 7.0f};
    int32_t tile[2] = {3, 4};
    std::vector<std::vector<float>> teleports;
    int Rand() { return used < draws.size() ? draws[used++] : (++used, 0); }
    void Post(const obl::messages::Message& message) { sent.push_back(message); }
    object::Base* Object(int32_t id) { return id == 77 ? &home : id == 88 ? &guest : nullptr; }
    void ServePosition(const object::Base*, float out[3]) { std::memcpy(out, serve, sizeof(serve)); }
    void WorldToTile(const float*, int32_t out[2])
    {
        out[0] = tile[0];
        out[1] = tile[1];
    }
    void Teleport(float x, float y, float z) { teleports.push_back({x, y, z}); }
    Env()
    {
        home.m_id = 77;
        home.m_sprite = &homeSprite;
    }
};

void MakeLifeguard(object::Lifeguard& lifeguard)
{
    lifeguard.m_id = 5;
    lifeguard.m_HomeID = 77;
    lifeguard.m_saving_id = 88;
}

} // namespace

OBL_TEST_CASE("lifeguard.find_beach_takes_the_nearest_vertex_node_and_its_chain_index")
{
    TestGrid g(16);
    std::vector<uint32_t> a{10, 10, 12, 12}, b{3, 3}, c{9, 2};
    Area na = MakeArea(a, 0, 0, 1, 1), nb = MakeArea(b, 0, 0, 1, 1), nc = MakeArea(c, 0, 0, 1, 1);
    g.grid.m_beachAreas = {na, nb, nc};
    object::Lifeguard lifeguard{};
    MakeLifeguard(lifeguard);
    // The origin of tile (3, 3) is x = 96, y = 192 (odd column: not moved up).
    lifeguard.m_WorldXYZ[0] = 100.0f;
    lifeguard.m_WorldXYZ[1] = 190.0f;
    OBL_CHECK(FindBeach(g.grid, lifeguard));
    OBL_CHECK(lifeguard.m_beachArea == &g.grid.m_beachAreas[1]);
    OBL_CHECK_EQ(lifeguard.m_BeachArea, 1);
}

OBL_TEST_CASE("lifeguard.find_beach_keeps_replacing_the_best_while_it_is_exactly_zero")
{
    // The "no best yet" test is `best == 0`: a vertex standing on the lifeguard (distance 0) leaves a best of 0, so the next vertex replaces it.
    TestGrid g(16);
    std::vector<uint32_t> a{3, 3}, b{8, 8};
    Area na = MakeArea(a, 0, 0, 1, 1), nb = MakeArea(b, 0, 0, 1, 1);
    g.grid.m_beachAreas = {na, nb};
    object::Lifeguard lifeguard{};
    MakeLifeguard(lifeguard);
    lifeguard.m_WorldXYZ[0] = 96.0f;
    lifeguard.m_WorldXYZ[1] = 192.0f;
    OBL_CHECK(FindBeach(g.grid, lifeguard));
    OBL_CHECK(lifeguard.m_beachArea == &g.grid.m_beachAreas[1]);
    OBL_CHECK_EQ(lifeguard.m_BeachArea, 1);
}

OBL_TEST_CASE("lifeguard.find_beach_declines_a_missing_chain")
{
    TestGrid g(16);
    object::Lifeguard lifeguard{};
    MakeLifeguard(lifeguard);
    OBL_CHECK(!FindBeach(g.grid, lifeguard));
}

OBL_TEST_CASE("lifeguard.find_beach_point_draws_x_then_y_until_the_tile_is_free_sand")
{
    TestGrid g(16);
    std::vector<uint32_t> pts{4, 4};
    Area node = MakeArea(pts, 4, 4, 8, 8);
    for (uint32_t x = 4; x <= 8; ++x)
        for (uint32_t y = 4; y <= 8; ++y) g.grid.Cell(x, y).m_flags = 0; // water: every draw but the last fails
    g.grid.Cell(6, 6).m_flags = 2;
    Env env;
    // rand 16384 * 3.0518509e-5 * 4 = 2.0000610 -> 2, plus 4 = 6. First draw (4, 4) -> water, then (6, 6) -> sand.
    env.draws = {0, 0, 16384, 16384};
    object::Lifeguard lifeguard{};
    MakeLifeguard(lifeguard);
    lifeguard.m_beachArea = &node;
    OBL_CHECK(FindBeachPoint(g.grid, lifeguard, env));
    OBL_CHECK_EQ(env.used, size_t{4});
    OBL_REQUIRE(env.sent.size() == 1);
    const obl::messages::Message& m = env.sent[0];
    OBL_CHECK_EQ(m.sender, 5);
    OBL_CHECK_EQ(m.receiver, 5);
    OBL_CHECK_EQ(m.type, kMsgPavement2Point);
    // Tile (6, 6): even column, x = 32 * 6 = 192, y = 64 * 6 - 32 = 352.
    OBL_CHECK_EQ(m.flags, 192);
    OBL_CHECK_EQ(m.data, 352);
}

OBL_TEST_CASE("lifeguard.find_beach_point_message_depends_on_the_blocked_path_counter")
{
    TestGrid g(16);
    std::vector<uint32_t> pts{4, 4};
    Area node = MakeArea(pts, 4, 4, 8, 8);
    struct Row {
        int32_t before;
        int32_t type;
        int32_t after;
    };
    const Row rows[] = {{0, kMsgPavement2Point, 0}, {9, kMsgPavement2Point, 9}, {10, kMsgMove2Point, 0}, {15, kMsgMove2Point, 0},
            {0x14, kMsgPathThroughScenery, 0}, {0x15, kMsgMove2Point, 0}};
    for (const Row& row : rows) {
        Env env;
        object::Lifeguard lifeguard{};
        MakeLifeguard(lifeguard);
        lifeguard.m_beachArea = &node;
        lifeguard.m_timesPathBlocked = row.before;
        OBL_CHECK(FindBeachPoint(g.grid, lifeguard, env));
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK_EQ(env.sent[0].type, row.type);
        OBL_CHECK_EQ(lifeguard.m_timesPathBlocked, row.after);
    }
    object::Lifeguard none{};
    MakeLifeguard(none);
    Env env;
    OBL_CHECK(!FindBeachPoint(g.grid, none, env));
    OBL_CHECK(env.sent.empty());
}

OBL_TEST_CASE("lifeguard.move2tower_sends_by_the_direction_of_the_home_sprite")
{
    for (int32_t direction = -1; direction <= 5; ++direction) {
        Env env;
        env.homeSprite.m_direction = direction;
        object::Lifeguard lifeguard{};
        MakeLifeguard(lifeguard);
        OBL_CHECK(MoveToTower(lifeguard, env));
        if (direction == 0 || direction == 1) {
            OBL_REQUIRE(env.sent.size() == 1);
            OBL_CHECK_EQ(env.sent[0].type, kMsgPavement2Object);
            OBL_CHECK_EQ(env.sent[0].flags, 0);
            OBL_CHECK_EQ(env.sent[0].data, 77);
        } else if (direction == 2 || direction == 3) {
            OBL_REQUIRE(env.sent.size() == 1);
            OBL_CHECK_EQ(env.sent[0].type, kMsgMove2Point);
            OBL_CHECK_EQ(env.sent[0].flags, direction == 2 ? 971 : 1029);
            OBL_CHECK_EQ(env.sent[0].data, 2030);
        } else {
            OBL_CHECK(env.sent.empty());
        }
    }
    Env env;
    object::Lifeguard lost{};
    MakeLifeguard(lost);
    lost.m_HomeID = 1;
    OBL_CHECK(!MoveToTower(lost, env));
}

OBL_TEST_CASE("lifeguard.set_draw_pos_moves_the_depth_of_the_serve_tile_and_teleports")
{
    TestGrid g(16);
    g.grid.Cell(3, 4).m_zOffset = 65300;
    for (bool build : {true, false}) {
        Env env;
        env.homeSprite.m_direction = 2;
        object::Lifeguard lifeguard{};
        MakeLifeguard(lifeguard);
        lifeguard.m_WorldXYZ[0] = 5.0f;
        lifeguard.m_WorldXYZ[1] = 6.0f;
        lifeguard.m_WorldXYZ[2] = 7.5f;
        OBL_CHECK(SetDrawPos(g.grid, lifeguard, env, build));
        OBL_REQUIRE(env.teleports.size() == 1);
        if (build) {
            OBL_CHECK_EQ(env.teleports[0][0], 971.0f);
            OBL_CHECK_EQ(env.teleports[0][1], 2030.0f);
            OBL_CHECK_EQ(env.teleports[0][2], 7.0f);
            OBL_CHECK_EQ(g.grid.Cell(3, 4).m_zOffset, static_cast<uint16_t>(264)); // 65300 + 500 wraps
        } else {
            OBL_CHECK_EQ(env.teleports[0][0], 5.0f);
            OBL_CHECK_EQ(env.teleports[0][1], 6.0f);
            OBL_CHECK_EQ(env.teleports[0][2], 7.5f);
            OBL_CHECK_EQ(g.grid.Cell(3, 4).m_zOffset, static_cast<uint16_t>(65300 + 500 - 500));
        }
    }
    // A sprite direction other than 2 and 3 leaves the point alone.
    Env env;
    env.homeSprite.m_direction = 0;
    object::Lifeguard lifeguard{};
    MakeLifeguard(lifeguard);
    OBL_CHECK(SetDrawPos(g.grid, lifeguard, env, true));
    OBL_CHECK_EQ(env.teleports[0][0], 1000.0f);
    OBL_CHECK_EQ(env.teleports[0][1], 2000.0f);
}

OBL_TEST_CASE("lifeguard.shore_vertex_is_the_nearest_free_one_with_the_first_free_vertex_as_the_start")
{
    TestGrid g(16, 4);
    std::vector<uint32_t> a{1, 1, 5, 5, 9, 9}, b{6, 6};
    Area na = MakeArea(a, 0, 0, 1, 1), nb = MakeArea(b, 0, 0, 1, 1);
    g.grid.m_seaAreas = {na, nb};
    g.grid.Cell(1, 1).m_flags |= 0x10; // the first vertex carries a building: the start is the second one
    const float from[3] = {6.0f * 32.0f, 6.0f * 64.0f, 0.0f};
    ShoreVertex v{};
    OBL_CHECK(NearestFreeShoreVertex(g.grid, from, v));
    OBL_CHECK_EQ(v.x, 6u); // node b's vertex (6, 6) is nearer than (5, 5) of node a
    OBL_CHECK_EQ(v.y, 6u);
    g.grid.Cell(6, 6).m_flags |= 0x10;
    OBL_CHECK(NearestFreeShoreVertex(g.grid, from, v));
    OBL_CHECK_EQ(v.x, 9u);
    for (GridCell& cell : g.cells) cell.m_flags |= 0x10;
    OBL_CHECK(!NearestFreeShoreVertex(g.grid, from, v));
    g.grid.m_seaAreas.clear();
    OBL_CHECK(!NearestFreeShoreVertex(g.grid, from, v));
}

OBL_TEST_CASE("lifeguard.go_to_shore_sends_the_two_messages_and_move2sea_entry_also_marks_the_guest_and_the_exit")
{
    TestGrid g(16, 4);
    std::vector<uint32_t> a{5, 5};
    Area na = MakeArea(a, 0, 0, 1, 1);
    g.grid.m_seaAreas = {na};
    {
        Env env;
        object::Lifeguard lifeguard{};
        MakeLifeguard(lifeguard);
        OBL_CHECK(GoToShore(g.grid, lifeguard, env, true));
        OBL_CHECK_EQ(env.guest.m_ActionState, 1);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK_EQ(env.sent[0].type, kMsgMove2Point);
        // Tile (5, 5), kind 4: x = 32 * 5 = 160, y = 64 * 5 + 32 = 352 (odd column).
        OBL_CHECK_EQ(lifeguard.m_ExitPoint[0], 160.0f);
        OBL_CHECK_EQ(lifeguard.m_ExitPoint[1], 352.0f);
        OBL_CHECK_EQ(lifeguard.m_ExitPoint[2], 0.0f);
        OBL_CHECK_EQ(env.sent[0].flags, 160);
        OBL_CHECK_EQ(env.sent[0].data, 352);
    }
    {
        Env env;
        object::Lifeguard lifeguard{};
        MakeLifeguard(lifeguard);
        OBL_CHECK(GoToShore(g.grid, lifeguard, env, false));
        OBL_CHECK_EQ(env.guest.m_ActionState, 0);
        OBL_CHECK_EQ(lifeguard.m_ExitPoint[0], 0.0f);
        OBL_REQUIRE(env.sent.size() == 1);
        OBL_CHECK_EQ(env.sent[0].type, kMsgSwim2SeaPoint);
        OBL_CHECK_EQ(env.sent[0].data, 352);
    }
    Env env;
    object::Lifeguard lost{};
    MakeLifeguard(lost);
    lost.m_saving_id = 1;
    OBL_CHECK(!GoToShore(g.grid, lost, env, true));
    OBL_CHECK(env.sent.empty());
}
