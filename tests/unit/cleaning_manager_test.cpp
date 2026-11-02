// CleaningManager logic (src/game/cleaning/cleaning_logic.h) on the native manager with a scripted environment.

#include "unit_test.h"

#include "game/tables/building_behavior.h"
#include "game/cleaning/cleaning_logic.h"

#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace obl::cleaning;

namespace {

// What the logic asks the world about one object (the fields the test environment answers).
struct ObjectModel {
    bool exists = false;
    uint8_t objectClass = 0;
    char name[32] = {};
    bool cleanerClaimed = false, mechanicClaimed = false, cleaningDirected = false;
    bool vandalised = false, hasBuildJob = false, hasDemolishJob = false;
    int tile[2] = {0, 0};
    obl::BuildingBehavior behavior = obl::BuildingBehavior::None;
};

struct TestEnv {
    float time = 100.0f;
    std::map<int, ObjectModel> objects;
    int target[2] = {0, 0};
    int cleaners = 1;
    int worldToTileCalls = 0;
    std::vector<std::pair<int, bool>> cleanerClaims; // SetCleanerClaimed calls in order

    ObjectModel& Get(int id) { return objects[id]; }
    float GameTime() { return time; }
    bool ObjectExists(int id) { return objects.count(id) && objects[id].exists; }
    uint8_t ObjectClass(int id) { return objects[id].objectClass; }
    const char* ObjectName(int id) { return objects[id].name; }
    obl::BuildingBehavior ObjectBehavior(int id) { return objects[id].behavior; }
    bool IsCleanerClaimed(int id) { return objects[id].cleanerClaimed; }
    bool IsMechanicClaimed(int id) { return objects[id].mechanicClaimed; }
    void SetCleanerClaimed(int id, bool claimed)
    {
        cleanerClaims.push_back({id, claimed});
        objects[id].cleanerClaimed = claimed;
    }
    void SetCleaningDirected(int id) { objects[id].cleaningDirected = true; }
    bool Vandalised(int id) { return objects[id].vandalised; }
    bool HasBuildJob(int id) { return objects[id].hasBuildJob; }
    bool HasDemolishJob(int id) { return objects[id].hasDemolishJob; }
    void ObjectTile(int id, int out[2]) { out[0] = objects[id].tile[0], out[1] = objects[id].tile[1]; }
    void WorldToTile(const float*, int out[2])
    {
        ++worldToTileCalls;
        out[0] = target[0], out[1] = target[1];
    }
    int CleanerCount() { return cleaners; }
};

struct Fixture {
    TestEnv env;
    Manager manager;

    // A scenery object (class 1) or building (class 2) named `name` at tile (x, y).
    ObjectModel& Add(int id, const char* name, int x = 0, int y = 0, uint8_t objectClass = 1)
    {
        ObjectModel& o = env.objects[id];
        o.exists = true;
        o.objectClass = objectClass;
        std::strncpy(o.name, name, sizeof(o.name) - 1);
        // The catalog gives the toilet its behavior from its key (LOO); the test objects carry it the same way.
        o.behavior = std::strcmp(name, "LOO") == 0 ? obl::BuildingBehavior::Toilet : obl::BuildingBehavior::None;
        o.tile[0] = x, o.tile[1] = y;
        return o;
    }
    size_t Pending() const { return manager.jobs.size(); }
    size_t Active() const { return manager.activeJobs.size(); }
    // Queued request number `i` (0 = front).
    Job& JobAt(size_t i) { return *std::next(manager.jobs.begin(), static_cast<std::ptrdiff_t>(i)); }
    // Marks a queued request as old enough/urgent for the urgent search (the time stamp must not be 0).
    void Urgent(int id) { AddUrgentJob(manager, env, id); }
};

const float kPoint[3] = {0, 0, 0};

} // namespace

OBL_TEST_CASE("cleaning.add_job_queues_and_counts_repeats")
{
    Fixture f;
    f.env.time = 7.5f;
    f.Add(1, "Bin");
    f.Add(2, "Litter");
    AddJob(f.manager, f.env, 1);
    AddJob(f.manager, f.env, 2);
    AddJob(f.manager, f.env, 1);
    OBL_CHECK_EQ(f.Pending(), 2u);
    const Job& first = f.JobAt(0);
    OBL_CHECK_EQ(first.id, 1);
    OBL_CHECK_EQ(first.calls, 1u);
    OBL_CHECK_EQ(first.timeCalled, 7.5f);
    OBL_CHECK_EQ(first.urgent, 0);
    OBL_CHECK_EQ(f.JobAt(1).calls, 0u);
    // The active list is not consulted: a claimed object queues again.
    ClaimJob(f.manager, 2, 0);
    AddJob(f.manager, f.env, 2);
    OBL_CHECK_EQ(f.JobAt(1).calls, 1u);
    f.Add(3, "Bin");
    ClaimJob(f.manager, 3, 0);
    AddJob(f.manager, f.env, 3);
    OBL_CHECK_EQ(f.Pending(), 3u);
}

OBL_TEST_CASE("cleaning.add_urgent_job_replaces_normal_request_at_the_back")
{
    Fixture f;
    f.Add(1, "Bin");
    f.Add(2, "Bin");
    f.env.time = 10.0f;
    AddJob(f.manager, f.env, 1);
    AddJob(f.manager, f.env, 1); // iCalls 1
    AddJob(f.manager, f.env, 2);
    f.env.time = 20.0f;
    AddUrgentJob(f.manager, f.env, 1);
    OBL_CHECK_EQ(f.Pending(), 2u);
    OBL_CHECK_EQ(f.JobAt(0).id, 2);
    Job& promoted = f.JobAt(1);
    OBL_CHECK_EQ(promoted.id, 1);
    OBL_CHECK_EQ(promoted.urgent, 1);
    OBL_CHECK_EQ(promoted.calls, 0u); // the count starts over
    OBL_CHECK_EQ(promoted.timeCalled, 20.0f); // so does the time
    AddUrgentJob(f.manager, f.env, 1);
    OBL_CHECK_EQ(promoted.calls, 1u); // an urgent one is counted
    AddJob(f.manager, f.env, 1);
    OBL_CHECK_EQ(promoted.calls, 2u);
    OBL_CHECK_EQ(promoted.urgent, 1); // and stays urgent
}

OBL_TEST_CASE("cleaning.add_urgent_job_on_active_job_or_new_object")
{
    Fixture f;
    f.Add(1, "Bin");
    f.Add(2, "Bin");
    ClaimJob(f.manager, 1, 0);
    AddUrgentJob(f.manager, f.env, 1);
    OBL_CHECK_EQ(f.manager.activeJobs.front().urgent, 1);
    OBL_CHECK_EQ(f.Pending(), 0u);
    AddUrgentJob(f.manager, f.env, 2);
    OBL_CHECK_EQ(f.Pending(), 1u);
    OBL_CHECK_EQ(f.JobAt(0).urgent, 1);
}

OBL_TEST_CASE("cleaning.claim_and_release_keep_the_active_counter")
{
    Fixture f;
    ClaimJob(f.manager, 5, 1);
    ClaimJob(f.manager, 5, 0); // already active
    ClaimJob(f.manager, 6, 0);
    OBL_CHECK_EQ(f.Active(), 2u);
    OBL_CHECK_EQ(f.manager.activeCount, 2);
    const ActiveJob& job = f.manager.activeJobs.front();
    OBL_CHECK_EQ(job.id, 5);
    OBL_CHECK_EQ(job.cleaning, 0);
    OBL_CHECK_EQ(job.urgent, 1);
    ReleaseJob(f.manager, 7); // unrecognized
    OBL_CHECK_EQ(f.manager.activeCount, 2);
    ReleaseJob(f.manager, 5);
    OBL_CHECK_EQ(f.Active(), 1u);
    OBL_CHECK_EQ(f.manager.activeCount, 1); // unlike the repair queue, released jobs are subtracted
}

OBL_TEST_CASE("cleaning.remove_job_and_status")
{
    Fixture f;
    f.Add(1, "Bin");
    f.Add(2, "Bin");
    f.Add(3, "Bin");
    AddJob(f.manager, f.env, 1);
    ClaimJob(f.manager, 2, 0);
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 3), 2); // not queued
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 1), 3); // queued
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 2), 4); // claimed
    StartedCleaning(f.manager, f.env, 2);
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 2), 5); // being cleaned
    f.env.cleaners = 0;
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 2), 1); // nobody employed
    f.env.cleaners = 1;
    RemoveJob(f.manager, 3); // unrecognized
    OBL_CHECK_EQ(f.Pending(), 1u);
    RemoveJob(f.manager, 1);
    OBL_CHECK_EQ(f.Pending(), 0u);
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 1), 2);
}

OBL_TEST_CASE("cleaning.started_cleaning_needs_the_object_and_an_active_job")
{
    Fixture f;
    f.Add(1, "Bin");
    ClaimJob(f.manager, 1, 0);
    StartedCleaning(f.manager, f.env, 9); // no such object
    OBL_CHECK_EQ(f.manager.activeJobs.front().cleaning, 0);
    f.Add(2, "Bin");
    StartedCleaning(f.manager, f.env, 2);
    StartedCleaning(f.manager, f.env, 1);
    OBL_CHECK_EQ(f.manager.activeJobs.front().cleaning, 1);
}

OBL_TEST_CASE("cleaning.did_not_clean_requeues_with_the_same_urgency")
{
    Fixture f;
    f.Add(1, "Bin").cleanerClaimed = true;
    f.Add(2, "Bin").cleanerClaimed = true;
    ClaimJob(f.manager, 1, 0);
    ClaimJob(f.manager, 2, 1);
    DidNotClean(f.manager, f.env, 1);
    DidNotClean(f.manager, f.env, 2);
    OBL_CHECK_EQ(f.Active(), 0u);
    OBL_CHECK_EQ(f.manager.activeCount, 0);
    OBL_CHECK_EQ(f.Pending(), 2u);
    OBL_CHECK_EQ(f.JobAt(0).id, 1);
    OBL_CHECK_EQ(f.JobAt(0).urgent, 0);
    OBL_CHECK_EQ(f.JobAt(1).id, 2);
    OBL_CHECK_EQ(f.JobAt(1).urgent, 1);
    OBL_CHECK(!f.env.objects[1].cleanerClaimed);
    OBL_CHECK(!f.env.objects[2].cleanerClaimed);
    DidNotClean(f.manager, f.env, 1); // without an active record: just a repeated request
    OBL_CHECK_EQ(f.JobAt(0).calls, 1u);
}

OBL_TEST_CASE("cleaning.claim_object_claims_building_or_scenery_and_names_the_work")
{
    Fixture f;
    f.Add(1, "Litter");
    f.Add(2, "Bin");
    f.Add(3, "LOO", 0, 0, 2);
    f.Add(4, "LAMP"); // scenery of another name
    f.Add(5, "Guest", 0, 0, 3); // not a static object
    AddJob(f.manager, f.env, 2);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 1), 0);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 2), 1);
    OBL_CHECK_EQ(f.Pending(), 0u); // the queued request disappeared
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 3), 2);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 4), -1); // claimed all the same
    OBL_CHECK_EQ(f.Active(), 4u);
    OBL_CHECK(f.env.objects[4].cleanerClaimed);
    OBL_CHECK(f.env.objects[4].cleaningDirected);
    OBL_CHECK_EQ(f.manager.activeJobs.front().urgent, 1);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 5), -1); // refused
    OBL_CHECK(!f.env.objects[5].cleanerClaimed);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 99), -1);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 1), -1); // now claimed by a cleaner
    f.Add(6, "Bin").mechanicClaimed = true;
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 6), -1);
    OBL_CHECK_EQ(f.Active(), 4u);
}

OBL_TEST_CASE("cleaning.toilet_kind_comes_from_the_behavior_not_the_name")
{
    Fixture f;
    f.Add(1, "LOO", 0, 0, 1); // scenery named like the toilet: no toilet behavior, so no kind by name
    f.Add(2, "LOO", 0, 0, 2); // the toilet building (behavior Toilet)
    f.env.Get(1).behavior = obl::BuildingBehavior::None;
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 1), -1);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 2), 2);
}

OBL_TEST_CASE("cleaning.find_urgent_job_last_urgent_match_wins")
{
    Fixture f;
    for (int id : {1, 2, 3, 4}) f.Add(id, "Litter");
    f.Add(5, "Bin");
    f.Add(6, "LOO", 0, 0, 2);
    for (int id : {1, 2, 5, 6}) f.Urgent(id);
    AddJob(f.manager, f.env, 3); // a normal request is not urgent
    int type = 99;
    // Scenery of another name than Bin needs the `scenery` flag; the last matching request wins (id 6 needs `buildings`).
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, false, true, false), 2);
    OBL_CHECK_EQ(type, kTypeLitter);
    OBL_CHECK_EQ(f.manager.activeJobs.front().urgent, 1);
    OBL_CHECK(f.env.objects[2].cleanerClaimed);
    OBL_CHECK_EQ(f.Pending(), 4u);
    type = 99;
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, true, true, true), 6); // the toilet is the last urgent request
    OBL_CHECK_EQ(type, kTypeToilet);
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, true, false, false), 5);
    OBL_CHECK_EQ(type, kTypeBin);
    type = 99;
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, true, false, false), -1); // litter 1 needs `scenery`
    OBL_CHECK_EQ(type, 99); // untouched on failure
}

OBL_TEST_CASE("cleaning.find_urgent_job_filters")
{
    Fixture f;
    f.Add(1, "Bin");
    f.Add(2, "LOO", 0, 0, 2);
    f.Urgent(1);
    f.Urgent(2);
    int type = 0;
    f.env.objects[1].vandalised = true;
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, true, true, false), -1); // vandalised bin, toilet needs `buildings`
    f.env.objects[1].vandalised = false;
    f.env.objects[1].mechanicClaimed = true;
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, true, true, false), -1); // bin held by a mechanic
    f.env.objects[2].hasBuildJob = true;
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, false, false, true), -1); // toilet under construction
    f.env.objects[2].hasBuildJob = false;
    f.env.objects[2].hasDemolishJob = true;
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, false, false, true), -1); // being demolished
    f.env.objects[2].hasDemolishJob = false;
    f.env.objects[2].cleanerClaimed = true;
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, true, true, true), -1); // claimed by a cleaner
    f.env.objects[2].cleanerClaimed = false;
    // A request with time stamp 0 is not handed out.
    f.JobAt(1).timeCalled = 0.0f;
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, false, false, true), -1);
    f.JobAt(1).timeCalled = 1.0f;
    OBL_CHECK_EQ(FindUrgentJob(f.manager, f.env, &type, false, false, true), 2);
}

OBL_TEST_CASE("cleaning.find_best_scored_job_ranks_by_tile_distance_and_kind_bonus")
{
    Fixture f;
    f.env.target[0] = 10, f.env.target[1] = 10;
    f.Add(1, "Litter", 10, 20); // distance 10 - 50 = -40
    f.Add(2, "Bin", 10, 30); // 20 - 70 = -50
    f.Add(3, "LOO", 40, 40, 2); // 60 - 100 = -40
    f.Add(4, "Litter", 12, 10); // 2 - 50 = -48
    for (int id : {1, 2, 3, 4}) AddJob(f.manager, f.env, id);
    int type = 99;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, true, true), 2);
    OBL_CHECK_EQ(type, kTypeBin);
    OBL_CHECK_EQ(f.manager.activeJobs.front().urgent, 0); // claimed as a normal job
    OBL_CHECK(f.env.objects[2].cleanerClaimed);
    OBL_CHECK_EQ(f.Pending(), 3u);
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, true, true), 4);
    OBL_CHECK_EQ(type, kTypeLitter);
    // Tie between litter 1 and the toilet (both -40): the first request wins.
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, true, true), 1);
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, true, true), 3);
    OBL_CHECK_EQ(type, kTypeToilet);
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, true, true), -1); // empty
}

OBL_TEST_CASE("cleaning.find_best_scored_job_capabilities_and_claims")
{
    Fixture f;
    f.Add(1, "Litter", 1, 0);
    f.Add(2, "Bin", 2, 0);
    f.Add(3, "LOO", 3, 0, 2);
    for (int id : {1, 2, 3}) AddJob(f.manager, f.env, id);
    int type = 99;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, false, false, false), -1);
    OBL_CHECK_EQ(type, 99); // not written when nothing qualifies
    // Bins: accepted unless vandalised or held by both a cleaner and a mechanic.
    f.env.objects[2].cleanerClaimed = true;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, false, false), 2);
    f.env.objects[2].mechanicClaimed = true;
    AddJob(f.manager, f.env, 2);
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, false, false), -1);
    f.env.objects[2].mechanicClaimed = false;
    f.env.objects[2].vandalised = true;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, false, false), -1);
    // Toilets: no construction/demolition and free of both claims.
    f.env.objects[3].hasDemolishJob = true;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, false, false, true), -1);
    f.env.objects[3].hasDemolishJob = false;
    f.env.objects[3].mechanicClaimed = true;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, false, false, true), -1);
    f.env.objects[3].mechanicClaimed = false;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, false, false, true), 3);
    OBL_CHECK_EQ(type, kTypeToilet);
    // Litter held by a cleaner is skipped.
    f.env.objects[1].cleanerClaimed = true;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, false, true, false), -1);
}

OBL_TEST_CASE("cleaning.find_best_scored_job_drops_requests_of_vanished_objects")
{
    Fixture f;
    f.Add(1, "Litter", 1, 0);
    f.Add(2, "Litter", 5, 0);
    f.Add(3, "Litter", 9, 0);
    for (int id : {1, 2, 3}) AddJob(f.manager, f.env, id);
    f.env.objects[1].exists = false;
    f.env.objects[3].exists = false;
    int type = 0;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, true, true), 2);
    OBL_CHECK_EQ(f.Pending(), 0u); // both vanished requests were deleted, the winner taken
    // An empty list answers before it converts the point.
    f.env.worldToTileCalls = 0;
    OBL_CHECK_EQ(FindBestScoredJob(f.manager, f.env, kPoint, &type, true, true, true), -1);
    OBL_CHECK_EQ(f.env.worldToTileCalls, 0);
}

OBL_TEST_CASE("cleaning.type_of_name_ignores_case")
{
    OBL_CHECK(obl::cleaning::TypeOfName("litter") == obl::cleaning::kTypeLitter);
    OBL_CHECK(obl::cleaning::TypeOfName("LITTER") == obl::cleaning::kTypeLitter);
    OBL_CHECK(obl::cleaning::TypeOfName("bin") == obl::cleaning::kTypeBin);
    OBL_CHECK(obl::cleaning::TypeOfName("bench") == obl::cleaning::kTypeNone);
}
