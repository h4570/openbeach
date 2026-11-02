#include "unit_test.h"

#include "game/construction/construction_points.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <vector>

using namespace obl;
using namespace obl::construction;

namespace {

// A manager with jobs whose work points are numbered (the same shape as the one of construction_queries_test).
struct Fixture {
    Manager manager;
    std::vector<Job*> jobs; // in the order AddJob made them

    Job& AddJob(int id, bool withPoints = true)
    {
        Job& job = *manager.jobs.emplace_back(std::make_unique<Job>());
        jobs.push_back(&job);
        for (int& owner : job.m_workPointOwners) owner = -1;
        job.m_buildingId = id;
        job.hasWorkPoints = withPoints;
        if (withPoints)
            for (int i = 0; i < kWorkPoints; ++i) job.workPoints[i] = {static_cast<uint32_t>(100 + i), static_cast<uint32_t>(200 + i), i};
        return job;
    }
};

struct ClaimEnv {
    std::map<int, float> limits; // building id -> m_MaxBuilders; the key also means the building exists
    bool MaxBuilders(int id, float* limit)
    {
        auto it = limits.find(id);
        if (it == limits.end()) return false;
        *limit = it->second;
        return true;
    }
};

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

OBL_TEST_CASE("construction.claim_work_point_takes_the_first_free_slot_and_keeps_it_for_the_token")
{
    Fixture f;
    Job& job = f.AddJob(5);
    ClaimEnv env{{{5, 3.0f}}};
    WorkPointTile a = ClaimWorkPoint(job, 70, env);
    OBL_CHECK_EQ(a.x, 100);
    OBL_CHECK_EQ(a.y, 200);
    OBL_CHECK_EQ(job.m_workPointOwners[0], 70);
    WorkPointTile b = ClaimWorkPoint(job, 71, env);
    OBL_CHECK_EQ(b.x, 101);
    OBL_CHECK_EQ(job.m_workPointOwners[1], 71);
    WorkPointTile again = ClaimWorkPoint(job, 70, env); // the owner finds its own slot first
    OBL_CHECK_EQ(again.x, 100);
    OBL_CHECK_EQ(job.m_workPointOwners[2], -1);
    ClaimWorkPoint(job, 72, env);
    WorkPointTile full = ClaimWorkPoint(job, 73, env); // three slots taken, the limit is three
    OBL_CHECK_EQ(full.x, 0);
    OBL_CHECK_EQ(full.y, 0);
    OBL_CHECK_EQ(job.m_workPointOwners[3], -1);
}

OBL_TEST_CASE("construction.claim_work_point_slot_limit_is_a_float_compare")
{
    Fixture f;
    Job& job = f.AddJob(5);
    ClaimEnv env{{{5, 2.5f}}};
    for (int token = 10; token < 14; ++token) ClaimWorkPoint(job, token, env);
    OBL_CHECK_EQ(job.m_workPointOwners[2], 12); // 2 < 2.5
    OBL_CHECK_EQ(job.m_workPointOwners[3], -1); // 3 < 2.5 is false
    env.limits[5] = 0.0f;
    OBL_CHECK_EQ(ClaimWorkPoint(job, 99, env).x, 0);
    env.limits[5] = -1.0f;
    OBL_CHECK_EQ(ClaimWorkPoint(job, 99, env).x, 0);
    OBL_CHECK_EQ(job.m_workPointOwners[3], -1);
    env.limits[5] = 25.0f; // the job has ten slots
    for (int token = 20; token < 40; ++token) ClaimWorkPoint(job, token, env);
    OBL_CHECK_EQ(job.m_workPointOwners[9], 26); // slots 3..9 took the tokens 20..26
    // an unordered compare counts as "below": the loop is only stopped by the end of the slots
    Fixture g;
    Job& other = g.AddJob(6);
    ClaimEnv nan{{{6, std::numeric_limits<float>::quiet_NaN()}}};
    ClaimWorkPoint(other, 1, nan);
    OBL_CHECK_EQ(other.m_workPointOwners[0], 1);
}

OBL_TEST_CASE("construction.claim_work_point_token_minus_one_matches_the_free_slot_without_a_write")
{
    Fixture f;
    Job& job = f.AddJob(5);
    ClaimEnv env{{{5, 4.0f}}};
    job.m_workPointOwners[0] = 7;
    WorkPointTile tile = ClaimWorkPoint(job, -1, env); // slot 1 is "owned" by -1
    OBL_CHECK_EQ(tile.x, 101);
    OBL_CHECK_EQ(job.m_workPointOwners[1], -1);
}

OBL_TEST_CASE("construction.claim_work_point_without_building_or_point_answers_zero")
{
    Fixture f;
    Job& job = f.AddJob(5);
    ClaimEnv none;
    OBL_CHECK_EQ(ClaimWorkPoint(job, 3, none).x, 0);
    OBL_CHECK_EQ(job.m_workPointOwners[0], -1);
    Job& bare = f.AddJob(6, false);
    ClaimEnv env{{{6, 2.0f}}};
    WorkPointTile tile = ClaimWorkPoint(bare, 3, env);
    OBL_CHECK_EQ(tile.x, 0);
    OBL_CHECK_EQ(bare.m_workPointOwners[0], 3); // the claim is written before the point is read
}

OBL_TEST_CASE("construction.get_job_position_converts_the_claimed_tile_to_floats")
{
    Fixture f;
    f.AddJob(1);
    f.AddJob(2);
    ClaimEnv env{{{1, 2.0f}, {2, 2.0f}}};
    Position none = GetJobPosition(f.manager, 9, 3, env);
    OBL_CHECK_EQ(Bits(none.x), Bits(0.0f));
    OBL_CHECK_EQ(Bits(none.z), Bits(0.0f));
    Position p = GetJobPosition(f.manager, 2, 3, env);
    OBL_CHECK_EQ(Bits(p.x), Bits(100.0f));
    OBL_CHECK_EQ(Bits(p.y), Bits(200.0f));
    OBL_CHECK_EQ(Bits(p.z), Bits(0.0f));
    Position q = GetJobPosition(f.manager, 2, 4, env);
    OBL_CHECK_EQ(Bits(q.x), Bits(101.0f));
    OBL_CHECK_EQ(FindJob(f.manager, 2)->m_workPointOwners[1], 4);
    OBL_CHECK_EQ(FindJob(f.manager, 1)->m_workPointOwners[0], -1);
}

namespace {

struct EntryEnv {
    std::map<int, EntryBuilding> buildings;
    Position job;
    int jobCalls = 0;
    EntryBuilding Building(int id)
    {
        auto it = buildings.find(id);
        return it == buildings.end() ? EntryBuilding{} : it->second;
    }
    Position JobPosition(int, int)
    {
        ++jobCalls;
        return job;
    }
};

} // namespace

OBL_TEST_CASE("construction.entry_point_without_a_footprint_hands_out_the_tile_ints_as_float_bits")
{
    EntryEnv env;
    env.buildings[3] = {true, false, 40, 12};
    Position p = BuildEntryPoint(3, 1, {7.0f, 8.0f, 9.0f}, env);
    OBL_CHECK_EQ(Bits(p.x), uint32_t(40));
    OBL_CHECK_EQ(Bits(p.y), uint32_t(12));
    OBL_CHECK(std::fabs(p.x) < 1e-30f); // denormal: the script sees ~0
    OBL_CHECK_EQ(env.jobCalls, 0);
}

OBL_TEST_CASE("construction.entry_point_with_a_footprint_uses_the_claimed_point_unless_both_coordinates_are_near_zero")
{
    EntryEnv env;
    env.buildings[3] = {true, true, 40, 12};
    env.job = {320.0f, 96.0f, 0.0f};
    Position p = BuildEntryPoint(3, 1, {7.0f, 8.0f, 9.0f}, env);
    OBL_CHECK_EQ(Bits(p.x), Bits(320.0f));
    OBL_CHECK_EQ(Bits(p.z), Bits(0.0f));
    env.job = {0.0f, 96.0f, 0.0f}; // one coordinate is enough
    OBL_CHECK_EQ(Bits(BuildEntryPoint(3, 1, {7.0f, 8.0f, 9.0f}, env).y), Bits(96.0f));
    env.job = {-1e-7f, 1.1920928955078125e-07f, 0.0f}; // |v| <= FLT_EPSILON on both
    Position fallback = BuildEntryPoint(3, 1, {7.0f, 8.0f, 9.0f}, env);
    OBL_CHECK_EQ(Bits(fallback.x), Bits(7.0f));
    OBL_CHECK_EQ(Bits(fallback.y), Bits(8.0f));
    OBL_CHECK_EQ(Bits(fallback.z), Bits(9.0f));
    env.job = {2e-7f, 0.0f, 0.0f}; // just above
    OBL_CHECK_EQ(Bits(BuildEntryPoint(3, 1, {7.0f, 8.0f, 9.0f}, env).x), Bits(2e-7f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    env.job = {nan, nan, 0.0f}; // unordered compares "not greater"
    OBL_CHECK_EQ(Bits(BuildEntryPoint(3, 1, {7.0f, 8.0f, 9.0f}, env).x), Bits(7.0f));
    env.job = {nan, 5.0f, 0.0f};
    OBL_CHECK_EQ(Bits(BuildEntryPoint(3, 1, {7.0f, 8.0f, 9.0f}, env).y), Bits(5.0f));
    OBL_CHECK_EQ(BuildEntryPoint(4, 1, {7.0f, 8.0f, 9.0f}, env).x, 0.0f); // a building that does not exist
}

namespace {

// A grid where direction 0 steps one tile right and direction 1 one tile down (no wrapping): the order of the visits is easy to read off.
struct CollectEnv {
    CollectBuilding building;
    int orientRows = 0, orientColumns = 0;
    uint32_t orientRotation = 0;
    int columnDirection = 0, rowDirection = 1;
    bool Building(int, CollectBuilding* out)
    {
        *out = building;
        return building.exists;
    }
    void Orient(TileXY*, uint32_t rows, uint32_t columns, int* column, int* row, uint32_t rotation)
    {
        orientRows = static_cast<int>(rows);
        orientColumns = static_cast<int>(columns);
        orientRotation = rotation;
        *column = columnDirection;
        *row = rowDirection;
    }
    TileXY Neighbour(TileXY tile, int direction)
    {
        if (direction == 0) ++tile.x;
        if (direction == 1) ++tile.y;
        return tile;
    }
    void TileToWorld(TileXY tile, float* xy)
    {
        xy[0] = static_cast<float>(tile.x) * 64.0f + 0.75f;
        xy[1] = static_cast<float>(tile.y) * 32.0f - 0.5f;
    }
};

struct Layer {
    int32_t cells[25 * 25] = {};
    void Set(int row, int column, uint32_t value) { cells[row * 25 + column] = static_cast<int32_t>(value); }
};

} // namespace

OBL_TEST_CASE("construction.collect_work_points_stores_the_flagged_cells_in_visiting_order")
{
    Fixture f;
    Job& job = f.AddJob(8);
    Layer layer;
    layer.Set(0, 1, 0x80000001u); // flag stripped
    layer.Set(1, 0, 0x80000000u); // the bare flag: no point
    layer.Set(1, 2, 3);
    layer.Set(2, 2, 0x40000001u); // free-water mark: no point
    layer.Set(2, 0, 0x00000805u); // "built" mark: no point
    layer.Set(2, 1, 7);
    CollectEnv env;
    env.building = {true, 10, 20, 0, 1, 1, layer.cells};
    CollectWorkPoints(f.manager, 8, env);
    OBL_CHECK_EQ(env.orientRows, 3);
    OBL_CHECK_EQ(env.orientColumns, 3);
    // visiting order (row, column) -> tile (10 + column, 20 + row); world x = 64 * tile x + 0.75, y = 32 * tile y - 0.5, both truncated
    OBL_CHECK_EQ(job.workPoints[0].x, uint32_t(11 * 64)); // cell (0, 1)
    OBL_CHECK_EQ(job.workPoints[0].y, uint32_t(20 * 32 - 1));
    OBL_CHECK_EQ(job.workPoints[0].direction, 1);
    OBL_CHECK_EQ(job.workPoints[1].x, uint32_t(12 * 64)); // cell (1, 2)
    OBL_CHECK_EQ(job.workPoints[1].y, uint32_t(21 * 32 - 1));
    OBL_CHECK_EQ(job.workPoints[1].direction, 3);
    OBL_CHECK_EQ(job.workPoints[2].x, uint32_t(11 * 64)); // cell (2, 1)
    OBL_CHECK_EQ(job.workPoints[2].y, uint32_t(22 * 32 - 1));
    OBL_CHECK_EQ(job.workPoints[2].direction, 7);
    OBL_CHECK_EQ(job.workPoints[3].x, uint32_t(103)); // untouched
}

OBL_TEST_CASE("construction.collect_work_points_turns_the_area_for_an_odd_rotation")
{
    Fixture f;
    Job& job = f.AddJob(8);
    Layer layer;
    layer.Set(1, 2, 5); // row 1, column 2 of the unturned area; the turned walk reads it at column 1, row 2
    CollectEnv env;
    env.building = {true, 0, 0, 3, 1, 2, layer.cells}; // area 3 x 4, turned: 3 rows of 4 columns
    CollectWorkPoints(f.manager, 8, env);
    OBL_CHECK_EQ(env.orientRows, 3);
    OBL_CHECK_EQ(env.orientColumns, 4);
    OBL_CHECK_EQ(env.orientRotation, uint32_t(3));
    // visited: row r, column c reads cell (c, r) of the layer, i.e. index r + 25*c: r = 2, c = 1
    OBL_CHECK_EQ(job.workPoints[0].direction, 5);
    OBL_CHECK_EQ(job.workPoints[0].x, uint32_t(1 * 64));
    OBL_CHECK_EQ(job.workPoints[0].y, uint32_t(2 * 32 - 1));
}

OBL_TEST_CASE("construction.collect_work_points_ignores_cells_beyond_the_ten_slots_and_missing_jobs")
{
    Fixture f;
    Job& job = f.AddJob(8);
    Layer layer;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 5; ++row) layer.Set(row, column, 1);
    CollectEnv env;
    env.building = {true, 0, 0, 0, 1, 3, layer.cells}; // 3 columns x 5 rows = 15 cells
    CollectWorkPoints(f.manager, 8, env);
    OBL_CHECK_EQ(job.workPoints[9].direction, 1);
    OBL_CHECK_EQ(job.m_workPointOwners[0], -1);
    CollectWorkPoints(f.manager, 99, env); // no job: nothing happens
    env.building.workLayer = nullptr; // no footprint: nothing happens
    CollectWorkPoints(f.manager, 8, env);
}
