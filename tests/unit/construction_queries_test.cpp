#include "unit_test.h"

#include "game/construction/construction_jobs.h"
#include "game/construction/construction_queries.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <vector>

using namespace obl;
using namespace obl::construction;

namespace {

// A manager with a circular job list on the test's heap.
struct Fixture {
    Manager manager;
    std::vector<Job*> jobs; // in the order AddJob made them

    Job& AddJob(int id)
    {
        Job& job = *manager.jobs.emplace_back(std::make_unique<Job>());
        jobs.push_back(&job);
        for (int& owner : job.m_workPointOwners) owner = -1;
        job.m_buildingId = id;
        return job;
    }
    void AddBuilder(int id, uint32_t location)
    {
        auto& entry = manager.m_builders[manager.m_builderCount++];
        entry.m_builderId = id;
        entry.m_location = location;
    }
};

struct FakeEnv {
    std::map<int, int> directions; // building id -> sprite direction; the key also means the object exists
    std::map<int, bool> working; // builder id -> on shift at an open building
    bool ObjectExists(int id) { return directions.count(id) != 0; }
    bool BuilderCanWork(int id) { return working.count(id) && working[id]; }
    int BuildingDirection(int id) { return directions.count(id) ? directions[id] : 0; }
};

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

OBL_TEST_CASE("construction.clamp_progress_treats_nan_as_below_zero")
{
    OBL_CHECK_EQ(Bits(ClampProgress(0.5f)), Bits(0.5f));
    OBL_CHECK_EQ(Bits(ClampProgress(-0.25f)), Bits(0.0f));
    OBL_CHECK_EQ(Bits(ClampProgress(1.5f)), Bits(1.0f));
    OBL_CHECK_EQ(Bits(ClampProgress(1.0f)), Bits(1.0f));
    OBL_CHECK_EQ(Bits(ClampProgress(std::numeric_limits<float>::quiet_NaN())), Bits(0.0f));
    OBL_CHECK_EQ(Bits(ClampProgress(-std::numeric_limits<float>::quiet_NaN())), Bits(0.0f));
    OBL_CHECK_EQ(Bits(ClampProgress(0.0f)), Bits(0.0f));
    OBL_CHECK_EQ(Bits(ClampProgress(-0.0f)), Bits(-0.0f)); // compares as >= 0, passes unchanged
    OBL_CHECK_EQ(Bits(ClampProgress(std::numeric_limits<float>::infinity())), Bits(1.0f));
    OBL_CHECK_EQ(Bits(ClampProgress(-std::numeric_limits<float>::infinity())), Bits(0.0f));
}

OBL_TEST_CASE("construction.find_job_returns_the_first_match")
{
    Fixture f;
    OBL_CHECK(FindJob(f.manager, 4) == nullptr);
    Job& a = f.AddJob(4);
    f.AddJob(9);
    Job& dup = f.AddJob(4);
    OBL_CHECK(FindJob(f.manager, 4) == &a);
    OBL_CHECK(FindJob(f.manager, 4) != &dup);
    OBL_CHECK(FindJob(f.manager, 9) != nullptr);
    OBL_CHECK(FindJob(f.manager, 5) == nullptr);
}

OBL_TEST_CASE("construction.count_jobs_by_mode")
{
    Fixture f;
    f.AddJob(1).m_underConstruction = 1;
    f.AddJob(2).m_underDemolition = 1;
    Job& both = f.AddJob(3);
    both.m_underConstruction = 1;
    both.m_underDemolition = 1;
    f.AddJob(4); // finished: neither flag
    OBL_CHECK_EQ(CountJobs(f.manager, 0), 2);
    OBL_CHECK_EQ(CountJobs(f.manager, 1), 2); // the demolition-only job and the one with both flags
    OBL_CHECK_EQ(CountJobs(f.manager, 2), 4);
    OBL_CHECK_EQ(CountJobs(f.manager, 3), 0);
    OBL_CHECK_EQ(CountJobs(f.manager, -1), 0);
}

OBL_TEST_CASE("construction.progress_defaults_to_finished_without_a_job")
{
    Fixture f;
    OBL_CHECK_EQ(Bits(BuildProgress(f.manager, 3)), Bits(1.0f));
    OBL_CHECK_EQ(Bits(DemolishProgress(f.manager, 3)), Bits(1.0f));
    Job& job = f.AddJob(3);
    job.m_constructionProgress = 0.4f;
    job.m_demolitionProgress = 7.0f;
    OBL_CHECK_EQ(Bits(BuildProgress(f.manager, 3)), Bits(0.4f));
    OBL_CHECK_EQ(Bits(DemolishProgress(f.manager, 3)), Bits(1.0f));
    job.m_constructionProgress = -3.0f;
    job.m_demolitionProgress = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(Bits(BuildProgress(f.manager, 3)), Bits(0.0f));
    OBL_CHECK_EQ(Bits(DemolishProgress(f.manager, 3)), Bits(0.0f));
}

OBL_TEST_CASE("construction.is_building_and_demolishing_need_the_object")
{
    Fixture f;
    Job& job = f.AddJob(6);
    job.m_underConstruction = 1;
    job.m_underDemolition = 0;
    FakeEnv gone;
    OBL_CHECK_EQ(Bits(IsBuilding(f.manager, gone, 6)), Bits(0.0f)); // the job exists, the object does not
    FakeEnv env;
    env.directions[6] = 0;
    OBL_CHECK_EQ(Bits(IsBuilding(f.manager, env, 6)), Bits(1.0f));
    OBL_CHECK_EQ(Bits(IsDemolishing(f.manager, env, 6)), Bits(0.0f));
    job.m_underConstruction = 0;
    job.m_underDemolition = 1;
    OBL_CHECK_EQ(Bits(IsBuilding(f.manager, env, 6)), Bits(0.0f));
    OBL_CHECK_EQ(Bits(IsDemolishing(f.manager, env, 6)), Bits(1.0f));
    job.m_underDemolition = 0xff; // the byte is converted, not normalised
    OBL_CHECK_EQ(Bits(IsDemolishing(f.manager, env, 6)), Bits(255.0f));
    env.directions[8] = 0;
    OBL_CHECK_EQ(Bits(IsBuilding(f.manager, env, 8)), Bits(0.0f)); // no job
}

OBL_TEST_CASE("construction.is_started_returns_the_raw_byte")
{
    Fixture f;
    OBL_CHECK_EQ(IsStarted(f.manager, 2), 0);
    f.AddJob(2).m_started = 0x7f;
    OBL_CHECK_EQ(IsStarted(f.manager, 2), 0x7f);
    OBL_CHECK_EQ(IsStarted(f.manager, 3), 0);
}

OBL_TEST_CASE("construction.builder_location_and_usable_builders")
{
    Fixture f;
    OBL_CHECK_EQ(GetBuilderLocation(f.manager, 10), -1);
    f.AddBuilder(10, 3);
    f.AddBuilder(11, 5);
    f.AddBuilder(12, 0);
    OBL_CHECK_EQ(GetBuilderLocation(f.manager, 10), 3);
    OBL_CHECK_EQ(GetBuilderLocation(f.manager, 11), 5);
    OBL_CHECK_EQ(GetBuilderLocation(f.manager, 12), 0);
    OBL_CHECK_EQ(GetBuilderLocation(f.manager, 13), -1);
    // A stale entry past the count is not a registered builder.
    f.manager.m_builders[3].m_builderId = 99;
    f.manager.m_builders[3].m_location = 4;
    OBL_CHECK_EQ(GetBuilderLocation(f.manager, 99), -1);
    FakeEnv env;
    env.working[10] = true;
    env.working[12] = true;
    OBL_CHECK_EQ(CountUsableBuilders(f.manager, env), 2);
    f.manager.m_builderCount = 0;
    OBL_CHECK_EQ(CountUsableBuilders(f.manager, env), 0);
}

OBL_TEST_CASE("construction.work_direction_turns_by_the_building_rotation")
{
    Fixture f;
    FakeEnv env;
    env.directions[5] = 1;
    OBL_CHECK_EQ(GetWorkDirection(f.manager, env, 5, 70), 0); // no job
    Job& job = f.AddJob(5);
    OBL_CHECK_EQ(GetWorkDirection(f.manager, env, 5, 70), 0); // job without work points
    job.workPoints[0].direction = 1;
    job.workPoints[1].direction = 5;
    job.workPoints[2].direction = 7;
    OBL_CHECK_EQ(GetWorkDirection(f.manager, env, 5, 70), 0); // the directions are there, but the job has no work points
    job.hasWorkPoints = true;
    job.m_workPointOwners[1] = 70;
    job.m_workPointOwners[2] = 71;
    OBL_CHECK_EQ(GetWorkDirection(f.manager, env, 5, 70), 3); // 5 - 2*1
    OBL_CHECK_EQ(GetWorkDirection(f.manager, env, 5, 71), 5); // 7 - 2*1
    OBL_CHECK_EQ(GetWorkDirection(f.manager, env, 5, 99), 7); // unrecognized token: slot 0, 1 - 2 = -1 -> 7
    env.directions[5] = 3;
    OBL_CHECK_EQ(GetWorkDirection(f.manager, env, 5, 71), 1); // 7 - 6
    OBL_CHECK_EQ(GetWorkDirection(f.manager, env, 5, 99), 3); // -5 + 8
    env.directions[5] = 0;
    OBL_CHECK_EQ(GetWorkDirection(f.manager, env, 5, 70), 5);
}

OBL_TEST_CASE("construction.set_started_writes_the_first_jobs_byte")
{
    Fixture f;
    Job& first = f.AddJob(4);
    Job& repeated = f.AddJob(4);
    OBL_CHECK(SetStarted(f.manager, 4, 0xff));
    OBL_CHECK_EQ(int(first.m_started), 0xff);
    OBL_CHECK_EQ(int(repeated.m_started), 0);
    OBL_CHECK(SetStarted(f.manager, 4, 0));
    OBL_CHECK_EQ(int(first.m_started), 0);
    OBL_CHECK(!SetStarted(f.manager, 5, 1));
}

OBL_TEST_CASE("construction.set_builder_location_only_inside_the_count")
{
    Fixture f;
    f.AddBuilder(10, 0);
    f.AddBuilder(11, 0);
    f.AddBuilder(10, 3); // repeated id: the first entry wins
    OBL_CHECK(SetBuilderLocation(f.manager, 10, 4));
    OBL_CHECK_EQ(f.manager.m_builders[0].m_location, 4u);
    OBL_CHECK_EQ(f.manager.m_builders[2].m_location, 3u);
    OBL_CHECK_EQ(GetBuilderLocation(f.manager, 10), 4);
    OBL_CHECK(SetBuilderLocation(f.manager, 11, -1));
    OBL_CHECK_EQ(f.manager.m_builders[1].m_location, 0xffffffffu);
    f.manager.m_builders[3].m_builderId = 12;
    OBL_CHECK(!SetBuilderLocation(f.manager, 12, 1));
    OBL_CHECK_EQ(f.manager.m_builders[3].m_location, 0u);
    f.manager.m_builderCount = 0;
    OBL_CHECK(!SetBuilderLocation(f.manager, 10, 5));
    OBL_CHECK_EQ(f.manager.m_builders[0].m_location, 4u);
    OBL_CHECK(FindBuilder(f.manager, 10) == nullptr);
}

OBL_TEST_CASE("construction.construct_job_with_a_footprint_has_ten_free_points")
{
    Job job;
    job.hoardings.resize(2);
    job.m_finished = 1;
    job.workPoints[3] = {5, 6, 7};
    ConstructJob(job, 42, JobDefinition{300, -1, true});
    OBL_CHECK_EQ(job.m_buildingId, 42);
    OBL_CHECK_EQ(job.m_builderCount, 0);
    OBL_CHECK(job.hoardings.empty());
    OBL_CHECK_EQ(job.m_constructionRemaining, 300.0f);
    OBL_CHECK_EQ(job.m_demolitionRemaining, 4294967296.0f); // the time is converted as unsigned
    OBL_CHECK_EQ(job.m_constructionStart + job.m_demolitionStart + job.m_constructionProgress + job.m_demolitionProgress, 0.0f);
    OBL_CHECK_EQ(job.m_underConstruction | job.m_underDemolition | job.m_started | job.m_finished, 0);
    OBL_CHECK(job.hasWorkPoints);
    for (int i = 0; i < kWorkPoints; ++i) {
        OBL_CHECK(job.workPoints[i] == WorkPoint{});
        OBL_CHECK_EQ(job.m_workPointOwners[i], -1);
    }
}

OBL_TEST_CASE("construction.construct_job_without_a_footprint_has_no_work_points")
{
    Job job;
    job.m_workPointOwners[0] = 9;
    ConstructJob(job, 7, JobDefinition{1, 2, false});
    OBL_CHECK(!job.hasWorkPoints);
    OBL_CHECK_EQ(job.m_workPointOwners[0], -1);
    OBL_CHECK(job.workPoints[0] == WorkPoint{});
}

OBL_TEST_CASE("construction.append_job_links_at_the_end")
{
    Fixture f;
    Job& first = f.AddJob(1);
    Job& added = AppendJob(f.manager, 2, JobDefinition{10, 20, true});
    OBL_CHECK_EQ(f.manager.jobs.size(), size_t(2));
    OBL_CHECK(f.manager.jobs.front().get() == &first);
    OBL_CHECK(f.manager.jobs.back().get() == &added);
    OBL_CHECK_EQ(added.m_buildingId, 2);
    OBL_CHECK_EQ(added.m_demolitionRemaining, 20.0f);
    OBL_CHECK(&first == FindJob(f.manager, 1)); // the earlier job did not move
}

OBL_TEST_CASE("construction.delete_job_removes_the_first_match")
{
    Fixture f;
    f.AddJob(5);
    Job& b = f.AddJob(6);
    Job& c = f.AddJob(5);
    f.manager.m_constructionCount = 3;
    OBL_CHECK(!DeleteJob(f.manager, 9));
    OBL_CHECK_EQ(f.manager.jobs.size(), size_t(3));
    OBL_CHECK(DeleteJob(f.manager, 5));
    OBL_CHECK_EQ(f.manager.jobs.size(), size_t(2));
    OBL_CHECK(FindJob(f.manager, 5) == &c);
    OBL_CHECK(f.manager.jobs.front().get() == &b);
    OBL_CHECK_EQ(f.manager.m_constructionCount, 3); // the count is not DeleteJob's
}

namespace {

struct BuilderEnv {
    std::map<int, int> tokens; // builder id -> Builder::m_BuilderNumber; the key also means the object exists
    int reallocations = 0;
    bool BuilderToken(int id, int* token)
    {
        if (!tokens.count(id)) return false;
        *token = tokens[id];
        return true;
    }
    void Reallocate() { ++reallocations; }
};

} // namespace

OBL_TEST_CASE("construction.add_builder_appends_a_clean_entry")
{
    Fixture f;
    BuilderEnv env;
    for (Builder& entry : f.manager.m_builders) entry = {0x5a5a5a5a, 0x5a5a5a5a, 0x5a5a5a5au, 0x5a5a5a5a, 0x5a};
    f.manager.m_builderCount = 2;
    OBL_CHECK(AddBuilder(f.manager, 41, env));
    const Builder& entry = f.manager.m_builders[2];
    OBL_CHECK_EQ(entry.m_builderId, 41);
    OBL_CHECK_EQ(entry.m_status, 0);
    OBL_CHECK_EQ(entry.m_location, 0u);
    OBL_CHECK_EQ(entry.m_buildingId, -1);
    OBL_CHECK_EQ(entry.m_manuallyAssigned, 0);
    OBL_CHECK_EQ(f.manager.m_builderCount, 3);
    OBL_CHECK_EQ(f.manager.m_builders[3].m_builderId, 0x5a5a5a5a);
    OBL_CHECK_EQ(env.reallocations, 1);
}

OBL_TEST_CASE("construction.add_builder_refuses_a_full_table")
{
    Fixture f;
    BuilderEnv env;
    f.manager.m_builderCount = kMaxBuilders;
    OBL_CHECK(!AddBuilder(f.manager, 1, env));
    OBL_CHECK_EQ(f.manager.m_builderCount, kMaxBuilders);
    OBL_CHECK_EQ(env.reallocations, 0);
}

OBL_TEST_CASE("construction.remove_builder_gives_the_job_back_and_closes_the_gap")
{
    Fixture f;
    BuilderEnv env;
    Job& job = f.AddJob(7);
    job.m_builderCount = 2;
    job.m_workPointOwners[0] = 70;
    job.m_workPointOwners[1] = 22;
    job.m_workPointOwners[2] = 22; // only the first matching token is released
    f.AddBuilder(11, 1);
    f.AddBuilder(22, 2);
    f.AddBuilder(33, 3);
    f.manager.m_builders[1].m_buildingId = 7;
    f.manager.m_builders[1].m_manuallyAssigned = 1;
    f.manager.m_builders[2].m_buildingId = 7;
    env.tokens = {{11, 70}, {22, 22}, {33, 33}};
    OBL_CHECK(RemoveBuilder(f.manager, 22, env));
    OBL_CHECK_EQ(job.m_builderCount, 1);
    OBL_CHECK_EQ(job.m_workPointOwners[0], 70);
    OBL_CHECK_EQ(job.m_workPointOwners[1], -1);
    OBL_CHECK_EQ(job.m_workPointOwners[2], 22);
    OBL_CHECK_EQ(f.manager.m_builderCount, 2);
    OBL_CHECK_EQ(f.manager.m_builders[0].m_builderId, 11);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_builderId, 33);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_buildingId, 7); // another builder's assignment is not touched
    OBL_CHECK_EQ(f.manager.m_builders[2].m_builderId, 33); // the vacated slot keeps the moved entry's stale copy
    OBL_CHECK_EQ(env.reallocations, 1);
}

OBL_TEST_CASE("construction.remove_builder_clears_every_entry_with_the_id_after_the_first_releases")
{
    Fixture f;
    BuilderEnv env;
    Job& a = f.AddJob(7);
    Job& b = f.AddJob(8);
    a.m_builderCount = b.m_builderCount = 3;
    a.m_workPointOwners[4] = 5;
    b.m_workPointOwners[9] = 5;
    f.AddBuilder(4, 0);
    f.AddBuilder(6, 0);
    f.AddBuilder(4, 0);
    f.manager.m_builders[0].m_buildingId = 7;
    f.manager.m_builders[2].m_buildingId = 8;
    f.manager.m_builders[2].m_manuallyAssigned = 1;
    env.tokens = {{4, 5}, {6, 6}};
    OBL_CHECK(RemoveBuilder(f.manager, 4, env));
    OBL_CHECK_EQ(a.m_builderCount, 2);
    // The first entry's pass already cleared the second entry's assignment, so the second job is never looked up: it keeps its builder.
    OBL_CHECK_EQ(b.m_builderCount, 3);
    OBL_CHECK_EQ(a.m_workPointOwners[4], -1);
    OBL_CHECK_EQ(b.m_workPointOwners[9], 5);
    OBL_CHECK_EQ(f.manager.m_builderCount, 2);
    OBL_CHECK_EQ(f.manager.m_builders[0].m_builderId, 6); // the first entry went, the second 4 is still registered
    OBL_CHECK_EQ(f.manager.m_builders[1].m_builderId, 4);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_buildingId, -1);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_manuallyAssigned, 0);
}

OBL_TEST_CASE("construction.remove_builder_of_an_unregistered_id_works_on_the_stale_entry")
{
    Fixture f;
    BuilderEnv env;
    Job& job = f.AddJob(7);
    job.m_builderCount = 1;
    f.AddBuilder(1, 0);
    f.AddBuilder(2, 0);
    f.manager.m_builders[1].m_buildingId = 7;
    f.manager.m_builders[2].m_builderId = 2; // stale entry right behind the table, same id as a registered builder
    env.tokens = {{1, 1}, {2, 2}};
    OBL_CHECK(RemoveBuilder(f.manager, 99, env));
    OBL_CHECK_EQ(job.m_builderCount, 0); // builder 2's job was released although 99 was asked for
    OBL_CHECK_EQ(f.manager.m_builders[1].m_buildingId, -1);
    OBL_CHECK_EQ(f.manager.m_builderCount, 1); // and the table lost its last entry in any case
    OBL_CHECK_EQ(f.manager.m_builders[0].m_builderId, 1);
    OBL_CHECK_EQ(f.manager.m_builders[1].m_builderId, 2); // the entry is still there, behind the count
    OBL_CHECK_EQ(env.reallocations, 1);
}

OBL_TEST_CASE("construction.remove_builder_without_an_object_keeps_the_tokens_and_without_entries_refuses")
{
    Fixture f;
    BuilderEnv env;
    OBL_CHECK(!RemoveBuilder(f.manager, 1, env));
    OBL_CHECK_EQ(f.manager.m_builderCount, 0);
    OBL_CHECK_EQ(env.reallocations, 0);
    Job& job = f.AddJob(7);
    job.m_builderCount = 1;
    job.m_workPointOwners[0] = 3;
    f.AddBuilder(3, 0);
    f.manager.m_builders[0].m_buildingId = 7;
    OBL_CHECK(RemoveBuilder(f.manager, 3, env));
    OBL_CHECK_EQ(job.m_builderCount, 0);
    OBL_CHECK_EQ(job.m_workPointOwners[0], 3);
    OBL_CHECK_EQ(f.manager.m_builderCount, 0);
}
