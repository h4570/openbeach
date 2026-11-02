#include "unit_test.h"

#include "game/construction/construction_jobs.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <vector>

// Construction job rules (docs/specs/construction.md): AssignBuilderToNearestJob / AssignBuilderToJob, the builder free builder of job routine and the NoBuildCallCheck variant.

using namespace obl;
using namespace obl::construction;

namespace {

struct Fixture {
    Manager manager;
    std::vector<Job*> jobs; // in the order AddJob made them

    Job& AddJob(int id, bool building, bool demolition = false)
    {
        Job& job = *manager.jobs.emplace_back(std::make_unique<Job>());
        jobs.push_back(&job);
        for (int& owner : job.m_workPointOwners) owner = -1;
        job.m_buildingId = id;
        job.m_underConstruction = building;
        job.m_underDemolition = demolition;
        return job;
    }
    void AddBuilder(int id)
    {
        auto& entry = manager.m_builders[manager.m_builderCount++];
        entry.m_builderId = id;
        entry.m_buildingId = -1;
    }
};

struct Env {
    struct Building {
        int tile[2];
        float maxBuilders;
    };
    std::map<int, Building> buildings; // building id -> tile and capacity; the key also means the object exists
    std::map<int, int> tokens; // builder id -> BuilderNumber
    int scale = 1; // world units per tile of the fake map view
    void WorldToTile(const float* world, int out[2])
    {
        out[0] = static_cast<int>(world[0]) / scale;
        out[1] = static_cast<int>(world[1]) / scale;
    }
    bool BuildingTile(int id, int out[2])
    {
        if (!buildings.count(id)) return false;
        out[0] = buildings[id].tile[0];
        out[1] = buildings[id].tile[1];
        return true;
    }
    bool MaxBuilders(int id, float* limit)
    {
        if (!buildings.count(id)) return false;
        *limit = buildings[id].maxBuilders;
        return true;
    }
    bool BuilderToken(int id, int* token)
    {
        if (!tokens.count(id)) return false;
        *token = tokens[id];
        return true;
    }
};

} // namespace

OBL_TEST_CASE("construction.job_has_room_treats_unordered_as_below")
{
    OBL_CHECK(JobHasRoom(0, 1.0f));
    OBL_CHECK(!JobHasRoom(1, 1.0f));
    OBL_CHECK(!JobHasRoom(0, 0.0f));
    OBL_CHECK(JobHasRoom(2, 2.5f));
    OBL_CHECK(JobHasRoom(0, std::numeric_limits<float>::quiet_NaN())); // unordered passes the below test
    OBL_CHECK(JobHasRoom(5, std::numeric_limits<float>::infinity()));
    OBL_CHECK(!JobHasRoom(0, -1.0f));
    OBL_CHECK(!JobHasRoom(-1, 1e9f)); // the count is converted as unsigned: 4294967295
}

OBL_TEST_CASE("construction.assign_nearest_picks_the_closest_tile_and_marks_the_builder")
{
    Fixture f;
    Env env;
    env.buildings = {{10, {{50, 50}, 2.0f}}, {11, {{12, 11}, 2.0f}}, {12, {{30, 30}, 2.0f}}};
    f.AddJob(10, true);
    Job& near = f.AddJob(11, true);
    f.AddJob(12, true);
    f.AddBuilder(5);
    f.AddBuilder(6);
    f.manager.m_builders[1].m_manuallyAssigned = 1;
    const float position[3] = {10.0f, 10.0f, 0.0f};
    uint32_t isBuild = 7;
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 6, position, 0, &isBuild, env), 11);
    OBL_CHECK_EQ(isBuild, 0u); // a construction job gives 0
    OBL_CHECK_EQ(near.m_builderCount, 1);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_buildingId, 11);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_manuallyAssigned, 0);
    OBL_CHECK_EQ(f.manager.m_builders[0].m_buildingId, -1);
}

OBL_TEST_CASE("construction.assign_nearest_keeps_the_first_of_equal_distances_and_uses_manhattan")
{
    Fixture f;
    Env env;
    env.buildings = {{1, {{14, 10}, 3.0f}}, {2, {{10, 14}, 3.0f}}, {3, {{12, 12}, 3.0f}}};
    f.AddJob(1, true);
    f.AddJob(2, true);
    f.AddBuilder(9);
    const float position[3] = {10.0f, 10.0f, 0.0f};
    uint32_t isBuild = 0;
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 9, position, 2, &isBuild, env), 1); // 4 and 4: the list's first one
    f.AddJob(3, true); // (12,12) is 4 away as well
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 9, position, 2, &isBuild, env), 1);
    env.buildings[3].tile[0] = 11; // now 3 away: the strictly closer one wins even as the last one
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 9, position, 2, &isBuild, env), 3);
}

OBL_TEST_CASE("construction.assign_nearest_filters_by_mode_and_capacity")
{
    Fixture f;
    Env env;
    env.buildings = {{1, {{0, 0}, 1.0f}}, {2, {{5, 5}, 2.0f}}, {3, {{9, 9}, 2.0f}}};
    Job& full = f.AddJob(1, true);
    full.m_builderCount = 1; // at capacity
    f.AddJob(2, false, true); // demolition only
    f.AddJob(3, false, false); // neither flag: only mode 2 takes it
    f.AddBuilder(1);
    const float position[3] = {0.0f, 0.0f, 0.0f};
    uint32_t isBuild = 5;
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 1, position, 0, &isBuild, env), -1);
    OBL_CHECK_EQ(isBuild, 5u); // untouched when nothing qualifies
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 1, position, 3, &isBuild, env), -1); // an unrecognized mode matches nothing
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 1, position, 1, &isBuild, env), 2);
    OBL_CHECK_EQ(isBuild, 1u); // a demolition job gives 1
    OBL_CHECK_EQ(f.manager.m_builders[0].m_buildingId, 2);
    f.manager.m_builders[0].m_buildingId = -1;
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 1, position, 2, &isBuild, env),
            2); // mode 2: the nearer of demolition and the flagless one
    OBL_CHECK_EQ(f.jobs[1]->m_builderCount, 2);
}

OBL_TEST_CASE("construction.assign_nearest_distance_wraps_like_32_bit_arithmetic")
{
    OBL_CHECK_EQ(DistanceAbs(3, 10), 7);
    OBL_CHECK_EQ(DistanceAbs(-5, 5), 10);
    OBL_CHECK_EQ(DistanceAbs(std::numeric_limits<int32_t>::min(), 0), std::numeric_limits<int32_t>::min()); // abs of INT_MIN wraps
    OBL_CHECK_EQ(DistanceAbs(std::numeric_limits<int32_t>::max(), -1), std::numeric_limits<int32_t>::min());
}

OBL_TEST_CASE("construction.assign_nearest_skips_jobs_without_a_building_and_refuses_an_unrecognized_builder")
{
    Fixture f;
    Env env;
    env.buildings = {{2, {{9, 9}, 2.0f}}};
    f.AddJob(1, true);
    Job& job = f.AddJob(2, true);
    f.AddBuilder(4);
    const float position[3] = {0.0f, 0.0f, 0.0f};
    uint32_t isBuild = 0;
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 77, position, 0, &isBuild, env), -1); // builder not registered: nothing is written
    OBL_CHECK_EQ(job.m_builderCount, 0);
    OBL_CHECK_EQ(AssignBuilderToNearestJob(f.manager, 4, position, 0, &isBuild, env), 2);
    OBL_CHECK_EQ(job.m_builderCount, 1);
}

OBL_TEST_CASE("construction.assign_to_job_sets_the_manual_flag_and_returns_the_kind")
{
    Fixture f;
    Env env;
    env.buildings = {{20, {{0, 0}, 2.0f}}, {21, {{0, 0}, 2.0f}}};
    Job& build = f.AddJob(20, true);
    Job& demolish = f.AddJob(21, false, true);
    f.AddBuilder(3);
    f.AddBuilder(4);
    OBL_CHECK_EQ(AssignBuilderToJob(f.manager, 4, 20, env), 0); // construction: 0
    OBL_CHECK_EQ(build.m_builderCount, 1);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_buildingId, 20);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_manuallyAssigned, 1);
    OBL_CHECK_EQ(AssignBuilderToJob(f.manager, 3, 21, env), 1); // anything else: 1
    OBL_CHECK_EQ(demolish.m_builderCount, 1);
    OBL_CHECK_EQ(f.manager.m_builders[0].m_manuallyAssigned, 1);
    OBL_CHECK_EQ(f.manager.m_builders[0].m_builderId, 3);
}

OBL_TEST_CASE("construction.assign_to_job_refuses_unrecognized_finished_full_and_unmatched")
{
    Fixture f;
    Env env;
    env.buildings = {{20, {{0, 0}, 1.0f}}, {21, {{0, 0}, 5.0f}}};
    Job& full = f.AddJob(20, true);
    full.m_builderCount = 1;
    Job& done = f.AddJob(21, true);
    done.m_finished = 1;
    f.AddJob(22, true); // no building object
    f.AddBuilder(3);
    OBL_CHECK_EQ(AssignBuilderToJob(f.manager, 3, 99, env), -1);
    OBL_CHECK_EQ(AssignBuilderToJob(f.manager, 3, 20, env), -1); // 1 < 1 is false
    OBL_CHECK_EQ(AssignBuilderToJob(f.manager, 3, 21, env), -1);
    OBL_CHECK_EQ(AssignBuilderToJob(f.manager, 3, 22, env), -1);
    env.buildings[20].maxBuilders = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(AssignBuilderToJob(f.manager, 8, 20, env), -1); // builder 8 has no entry: refused, nothing written
    OBL_CHECK_EQ(full.m_builderCount, 1);
    OBL_CHECK_EQ(AssignBuilderToJob(f.manager, 3, 20, env), 0); // a NaN limit leaves room
    OBL_CHECK_EQ(full.m_builderCount, 2);
    OBL_CHECK_EQ(f.manager.m_builders[0].m_buildingId, 20);
}

OBL_TEST_CASE("construction.free_builder_of_job_gives_the_job_back_and_keeps_the_entry")
{
    Fixture f;
    Env env;
    Job& job = f.AddJob(7, true);
    job.m_builderCount = 2;
    job.m_workPointOwners[0] = 70;
    job.m_workPointOwners[3] = 22;
    f.AddBuilder(11);
    f.AddBuilder(22);
    f.manager.m_builders[1].m_buildingId = 7;
    f.manager.m_builders[1].m_manuallyAssigned = 1;
    f.manager.m_builders[0].m_buildingId = 7;
    env.tokens = {{11, 70}, {22, 22}};
    OBL_CHECK(FreeBuilderOfJob(f.manager, 22, env));
    OBL_CHECK_EQ(job.m_builderCount, 1);
    OBL_CHECK_EQ(job.m_workPointOwners[3], -1);
    OBL_CHECK_EQ(job.m_workPointOwners[0], 70); // another builder's token stays
    OBL_CHECK_EQ(f.manager.m_builderCount, 2); // unlike RemoveBuilder the table keeps the builder
    OBL_CHECK_EQ(f.manager.m_builders[1].m_builderId, 22);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_buildingId, -1);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_manuallyAssigned, 0);
    OBL_CHECK_EQ(f.manager.m_builders[0].m_buildingId, 7);
}

OBL_TEST_CASE("construction.free_builder_of_job_second_entry_with_the_id_finds_no_job")
{
    Fixture f;
    Env env;
    Job& a = f.AddJob(7, true);
    Job& b = f.AddJob(8, true);
    a.m_builderCount = b.m_builderCount = 3;
    f.AddBuilder(4);
    f.AddBuilder(4);
    f.manager.m_builders[0].m_buildingId = 7;
    f.manager.m_builders[1].m_buildingId = 8;
    env.tokens = {{4, 5}};
    OBL_CHECK(FreeBuilderOfJob(f.manager, 4, env));
    OBL_CHECK_EQ(a.m_builderCount, 2);
    OBL_CHECK_EQ(b.m_builderCount, 3); // the first pass cleared the second entry before it was read
    OBL_CHECK_EQ(f.manager.m_builders[1].m_buildingId, -1);
}

OBL_TEST_CASE("construction.free_builder_of_job_without_an_object_keeps_tokens_and_a_bad_count_refuses")
{
    Fixture f;
    Env env;
    Job& job = f.AddJob(7, true);
    job.m_builderCount = 1;
    job.m_workPointOwners[2] = 3;
    f.AddBuilder(3);
    f.manager.m_builders[0].m_buildingId = 7;
    OBL_CHECK(FreeBuilderOfJob(f.manager, 3, env));
    OBL_CHECK_EQ(job.m_builderCount, 0);
    OBL_CHECK_EQ(job.m_workPointOwners[2], 3);
    f.manager.m_builderCount = 0;
    OBL_CHECK(FreeBuilderOfJob(f.manager, 3, env)); // an empty table does nothing
    f.manager.m_builderCount = kMaxBuilders + 1;
    OBL_CHECK(!FreeBuilderOfJob(f.manager, 3, env));
    OBL_CHECK(!ClearBuilderJobEntries(f.manager, 3));
    f.manager.m_builderCount = -1;
    OBL_CHECK(!FreeBuilderOfJob(f.manager, 3, env));
}

OBL_TEST_CASE("construction.free_builder_no_build_call_check_clears_only_the_entries")
{
    Fixture f;
    Job& job = f.AddJob(7, true);
    job.m_builderCount = 2;
    job.m_workPointOwners[1] = 22;
    f.AddBuilder(22);
    f.AddBuilder(23);
    f.AddBuilder(22);
    for (auto& entry : f.manager.m_builders) {
        entry.m_buildingId = 7;
        entry.m_manuallyAssigned = 1;
        entry.m_location = 4;
    }
    OBL_CHECK(ClearBuilderJobEntries(f.manager, 22));
    OBL_CHECK_EQ(f.manager.m_builders[0].m_buildingId, -1);
    OBL_CHECK_EQ(f.manager.m_builders[0].m_manuallyAssigned, 0);
    OBL_CHECK_EQ(f.manager.m_builders[0].m_location, 4u);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_buildingId, 7); // other builders stay
    OBL_CHECK_EQ(f.manager.m_builders[2].m_buildingId, -1);
    OBL_CHECK_EQ(f.manager.m_builders[3].m_buildingId, 7); // behind the count: not an entry
    OBL_CHECK_EQ(job.m_builderCount, 2); // the job is not told
    OBL_CHECK_EQ(job.m_workPointOwners[1], 22); // and keeps the token
    OBL_CHECK_EQ(f.manager.m_builderCount, 3);
}
