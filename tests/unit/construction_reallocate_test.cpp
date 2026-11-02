#include "unit_test.h"

#include "game/construction/construction_jobs.h"

#include <cstring>
#include <map>
#include <memory>
#include <vector>

// Construction job rules (docs/specs/construction.md): the builder reallocate builders routine.

using namespace obl;
using namespace obl::messages;
using namespace obl::construction;

namespace {

struct Posted {
    int builder, type, building;
    bool operator==(const Posted& other) const { return builder == other.builder && type == other.type && building == other.building; }
};

struct Fixture {
    Manager manager;
    std::vector<Job*> jobs; // in the order AddJob made them

    struct BuilderInfo {
        BuilderFacts facts;
        int token = 0;
    };
    std::map<int, BuilderInfo> builders; // builder id -> facts; the key also means the object exists
    std::map<int, float> buildings; // building id -> capacity; the key also means the object exists
    std::vector<Posted> posted;
    int freed = 0; // calls of FreeBuilder

    Job& AddJob(int id, bool building, bool demolition = false, float capacity = 5.0f)
    {
        Job& job = *manager.jobs.emplace_back(std::make_unique<Job>());
        jobs.push_back(&job);
        for (int& owner : job.m_workPointOwners) owner = -1;
        job.m_buildingId = id;
        job.m_underConstruction = building;
        job.m_underDemolition = demolition;
        ++manager.m_constructionCount;
        buildings[id] = capacity;
        return job;
    }
    // A builder that is usable (on shift, home open, outside the hut, not directed) unless the caller changes the facts.
    construction::Builder& AddBuilder(int id)
    {
        auto& entry = manager.m_builders[manager.m_builderCount++];
        entry.m_builderId = id;
        entry.m_status = 7;
        entry.m_buildingId = -1;
        BuilderInfo info;
        info.facts.homeExists = info.facts.onShift = info.facts.homeOpen = true;
        info.token = 1000 + id;
        builders[id] = info;
        return entry;
    }

    // Env contract of construction::ReallocateBuilders.
    bool Builder(int id, BuilderFacts* facts)
    {
        auto it = builders.find(id);
        if (it == builders.end()) return false;
        *facts = it->second.facts;
        return true;
    }
    bool BuilderToken(int id, int* token)
    {
        auto it = builders.find(id);
        if (it == builders.end()) return false;
        *token = it->second.token;
        return true;
    }
    bool MaxBuilders(int id, float* limit)
    {
        auto it = buildings.find(id);
        if (it == buildings.end()) return false;
        *limit = it->second;
        return true;
    }
    void FreeBuilder(int id)
    {
        ++freed;
        ClearBuilderJobEntries(manager, id);
    }
    void Post(int builder, int type, int building) { posted.push_back({builder, type, building}); }

    bool Run() { return ReallocateBuilders(manager, *this); }
    const construction::Builder& Entry(int i) const { return manager.m_builders[i]; }
};

} // namespace

OBL_TEST_CASE("construction.reallocate_status_needs_shift_open_home_hut_manual_and_state")
{
    Fixture f;
    f.AddBuilder(1); // usable
    f.AddBuilder(2);
    f.builders[2].facts.onShift = false;
    f.AddBuilder(3);
    f.builders[3].facts.homeOpen = false;
    f.AddBuilder(4);
    f.builders[4].facts.inHut = 1;
    f.AddBuilder(5).m_manuallyAssigned = 1;
    f.AddBuilder(6);
    f.builders[6].facts.state = kBuilderStateDirected;
    f.AddBuilder(7);
    f.builders[7].facts.state = 0x82; // only 0x83 excludes
    OBL_CHECK(f.Run());
    const int expected[] = {0, 1, 1, 1, 1, 1, 0};
    for (int i = 0; i < 7; ++i) OBL_CHECK_EQ(f.Entry(i).m_status, expected[i]);
    OBL_CHECK(f.posted.empty());
}

OBL_TEST_CASE("construction.reallocate_without_jobs_clears_every_assignment_but_not_the_manual_flag")
{
    Fixture f;
    f.AddBuilder(1).m_buildingId = 10;
    f.AddBuilder(2).m_buildingId = 11;
    f.manager.m_builders[1].m_manuallyAssigned = 1;
    f.manager.m_constructionCount = 0;
    OBL_CHECK(f.Run());
    OBL_CHECK_EQ(f.Entry(0).m_buildingId, -1);
    OBL_CHECK_EQ(f.Entry(1).m_buildingId, -1);
    OBL_CHECK_EQ(f.Entry(1).m_manuallyAssigned, 1);
    OBL_CHECK_EQ(f.Entry(1).m_status, 1); // the manual flag still made it unavailable
    OBL_CHECK(f.posted.empty());
}

OBL_TEST_CASE("construction.reallocate_a_missing_home_keeps_the_old_status_and_ends_after_the_pass")
{
    Fixture f;
    f.AddBuilder(1);
    f.builders[1].facts.homeExists = false;
    f.AddBuilder(2);
    f.AddJob(10, true);
    OBL_CHECK(f.Run());
    OBL_CHECK_EQ(f.Entry(0).m_status, 7); // untouched
    OBL_CHECK_EQ(f.Entry(1).m_status, 0); // the pass goes on to the end
    OBL_CHECK_EQ(f.Entry(0).m_buildingId, -1);
    OBL_CHECK_EQ(f.Entry(1).m_buildingId, -1);
    OBL_CHECK(f.posted.empty());
}

OBL_TEST_CASE("construction.reallocate_spreads_evenly_and_tells_each_builder")
{
    Fixture f;
    for (int id = 1; id <= 4; ++id) f.AddBuilder(id);
    Job& build = f.AddJob(10, true);
    Job& demolish = f.AddJob(11, false, true);
    OBL_CHECK(f.Run());
    OBL_CHECK_EQ(build.m_builderCount, 2);
    OBL_CHECK_EQ(demolish.m_builderCount, 2);
    const int jobOf[] = {10, 10, 11, 11};
    for (int i = 0; i < 4; ++i) OBL_CHECK_EQ(f.Entry(i).m_buildingId, jobOf[i]);
    const std::vector<Posted> expected = {{1, MT_CONSTRUCT, 10}, {2, MT_CONSTRUCT, 10}, {3, MT_DEMOLISH, 11}, {4, MT_DEMOLISH, 11}};
    OBL_CHECK(f.posted == expected);
}

OBL_TEST_CASE("construction.reallocate_respects_capacity_and_hands_the_rest_to_the_extra_pass")
{
    Fixture f;
    for (int id = 1; id <= 4; ++id) f.AddBuilder(id);
    Job& small = f.AddJob(10, true, false, 1.0f);
    Job& wide = f.AddJob(11, false, true);
    OBL_CHECK(f.Run());
    // share 2: the small job stops at its capacity of 1, the wide one takes the next two; one builder is left, extra = ceil(1 / 2) = 1 and goes
    // to the wide job (the small one has no room).
    OBL_CHECK_EQ(small.m_builderCount, 1);
    OBL_CHECK_EQ(wide.m_builderCount, 3);
    const int jobOf[] = {10, 11, 11, 11};
    for (int i = 0; i < 4; ++i) OBL_CHECK_EQ(f.Entry(i).m_buildingId, jobOf[i]);
    OBL_CHECK_EQ(f.posted.size(), 4u);
    OBL_CHECK(f.posted.back() == (Posted{4, MT_DEMOLISH, 11}));
}

OBL_TEST_CASE("construction.reallocate_takes_surplus_builders_off_a_crowded_job")
{
    Fixture f;
    for (int id = 1; id <= 3; ++id) f.AddBuilder(id).m_buildingId = 10;
    Job& crowded = f.AddJob(10, true);
    Job& empty = f.AddJob(11, true);
    crowded.m_builderCount = 3;
    crowded.m_workPointOwners[0] = 1002;
    crowded.m_workPointOwners[3] = 1001;
    OBL_CHECK(f.Run());
    // usable 3 over 2 jobs: share 1, spare 1: one builder (the first of the job) moves over; its work-point token (1001) is freed.
    OBL_CHECK_EQ(crowded.m_builderCount, 2);
    OBL_CHECK_EQ(empty.m_builderCount, 1);
    OBL_CHECK_EQ(crowded.m_workPointOwners[3], -1);
    OBL_CHECK_EQ(crowded.m_workPointOwners[0], 1002);
    OBL_CHECK_EQ(f.Entry(0).m_buildingId, 11);
    OBL_CHECK_EQ(f.Entry(1).m_buildingId, 10);
    OBL_CHECK_EQ(f.Entry(2).m_buildingId, 10);
    OBL_CHECK_EQ(f.freed, 1);
    OBL_CHECK(f.posted == (std::vector<Posted>{{1, MT_CONSTRUCT, 11}}));
}

OBL_TEST_CASE("construction.reallocate_skips_finished_jobs_and_gives_the_leftover_to_the_open_one")
{
    Fixture f;
    f.AddBuilder(1);
    f.AddBuilder(2);
    Job& finished = f.AddJob(10, true);
    finished.m_finished = 1;
    Job& open = f.AddJob(11, true);
    OBL_CHECK(f.Run());
    // usable 2 over 2 jobs: share 1 for the open job; then extra = ceil(1 / 2) = 1 adds the second builder to it.
    OBL_CHECK_EQ(finished.m_builderCount, 0);
    OBL_CHECK_EQ(open.m_builderCount, 2);
    OBL_CHECK_EQ(f.Entry(0).m_buildingId, 11);
    OBL_CHECK_EQ(f.Entry(1).m_buildingId, 11);
    OBL_CHECK_EQ(f.posted.size(), 2u);
}

OBL_TEST_CASE("construction.reallocate_extra_pass_ignores_the_kind_flags")
{
    Fixture f;
    f.AddBuilder(1);
    f.AddBuilder(2);
    Job& plain = f.AddJob(10, false, false);
    OBL_CHECK(f.Run());
    // no kind: the share pass passes the job by; the extra pass (ceil(2 / 1) = 2) gives it both builders and the message is a demolition.
    OBL_CHECK_EQ(plain.m_builderCount, 2);
    OBL_CHECK(f.posted == (std::vector<Posted>{{1, MT_DEMOLISH, 10}, {2, MT_DEMOLISH, 10}}));
}

OBL_TEST_CASE("construction.reallocate_refuses_inputs_that_would_crash")
{
    Fixture f;
    f.AddBuilder(1);
    f.manager.m_builderCount = 51;
    OBL_CHECK(!f.Run());
    f.manager.m_builderCount = -1;
    OBL_CHECK(!f.Run());
    f.manager.m_builderCount = 1;
    f.builders.clear(); // no builder object
    OBL_CHECK(!f.Run());
    OBL_CHECK_EQ(f.Entry(0).m_status, 7);

    Fixture g;
    g.AddBuilder(2);
    g.AddJob(10, true);
    g.buildings.clear(); // the job's building is gone
    OBL_CHECK(!g.Run());
}
