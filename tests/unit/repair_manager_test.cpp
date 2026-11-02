// RepairManager logic (src/game/repair/repair_logic.h) on the native manager with a scripted environment.

#include "unit_test.h"

#include "game/repair/repair_logic.h"

#include <cstring>
#include <map>
#include <vector>

using namespace obl::repair;

namespace {

struct Object {
    uint8_t type = 3; // not a building unless set to 2
    float pos[3] = {0, 0, 0};
    bool cleanerClaimed = false;
    bool mechanicClaimed = false;
    bool waterable = false;
    bool directed = false;
};

// Everything the logic asks of the game, backed by maps.
struct TestEnv {
    float time = 100.0f;
    std::map<int, Object> objects;
    std::vector<PlantInfo> plants;
    std::vector<int> rolls; // values returned by Rand(), in order; the last one repeats
    size_t rolled = 0;
    std::vector<std::pair<int, bool>> claims; // SetMechanicClaimed calls in order

    int mechanics = 1;
    float GameTime() { return time; }
    int MechanicCount() { return mechanics; }
    bool ObjectExists(int id) { return objects.count(id) != 0; }
    uint8_t ObjectClass(int id) { return objects[id].type; }
    bool Waterable(int id) { return objects[id].waterable; }
    void SetDirected(int id) { objects[id].directed = true; }
    void Position(int id, float out[3]) { std::memcpy(out, objects[id].pos, sizeof(float) * 3); }
    bool IsCleanerClaimed(int id) { return objects[id].cleanerClaimed; }
    bool IsMechanicClaimed(int id) { return objects[id].mechanicClaimed; }
    void SetMechanicClaimed(int id, bool claimed)
    {
        claims.push_back({id, claimed});
        objects[id].mechanicClaimed = claimed;
    }
    int Rand()
    {
        if (rolls.empty()) return 0;
        int v = rolls[rolled < rolls.size() ? rolled : rolls.size() - 1];
        ++rolled;
        return v;
    }
    size_t PlantCount() { return plants.size(); }
    PlantInfo PlantAt(size_t i) { return plants[i]; }
};

struct Fixture {
    TestEnv env;
    Manager manager;
    Object& Add(int id, float x, float y = 0, float z = 0, uint8_t type = 3)
    {
        Object& o = env.objects[id];
        o.pos[0] = x, o.pos[1] = y, o.pos[2] = z, o.type = type;
        return o;
    }
    // A building at distance x from the origin, requested at time 0 (so ready at the default time 100).
    void Building(int id, float x, uint32_t priority = 2)
    {
        Add(id, x, 0, 0, 2);
        AddJob(manager, env, id, priority);
        env.time = 100.0f;
        manager.buildingJobs.back().timeCalled = 0.0f;
    }
    size_t Pending() { return manager.buildingJobs.size() + manager.sceneryJobs.size(); }
    size_t Active() { return manager.activeJobs.size(); }
};

const float kOrigin[3] = {0, 0, 0};

} // namespace

OBL_TEST_CASE("repair.add_job_queues_by_priority_and_counts_repeats")
{
    Fixture f;
    f.env.time = 12.5f;
    f.Add(1, 1, 0, 0, 2);
    f.Add(2, 2, 0, 0, 2);
    f.Add(3, 3, 0, 0, 3);
    AddJob(f.manager, f.env, 1, 0); // inspection -> building list
    AddJob(f.manager, f.env, 2, 2); // breakdown -> building list
    AddJob(f.manager, f.env, 3, 1); // scenery priority -> scenery list
    AddJob(f.manager, f.env, 99, 2); // unrecognized object: ignored
    OBL_CHECK_EQ(f.manager.buildingJobs.size(), 2u);
    OBL_CHECK_EQ(f.manager.sceneryJobs.size(), 1u);
    Job* first = &f.manager.buildingJobs.front();
    OBL_CHECK_EQ(first->id, 1);
    OBL_CHECK_EQ(first->type, 0);
    OBL_CHECK_EQ(first->urgent, 0);
    OBL_CHECK_EQ(first->calls, 0u);
    OBL_CHECK_EQ(first->timeCalled, 12.5f);
    AddJob(f.manager, f.env, 1, 0); // repeated inspection: counted
    OBL_CHECK_EQ(first->calls, 1u);
    OBL_CHECK_EQ(first->type, 0);
    AddJob(f.manager, f.env, 1, 2); // inspection becomes a breakdown
    OBL_CHECK_EQ(first->calls, 2u);
    OBL_CHECK_EQ(first->type, 2);
    AddJob(f.manager, f.env, 1, 0); // a breakdown is not demoted
    OBL_CHECK_EQ(first->type, 2);
    OBL_CHECK_EQ(f.manager.buildingJobs.size(), 2u);
    OBL_CHECK_EQ(f.manager.breakdownCount, 0); // the counters are not maintained by the queue methods
}

OBL_TEST_CASE("repair.add_job_on_active_job_raises_type_only")
{
    Fixture f;
    f.Add(1, 1, 0, 0, 2);
    ClaimJob(f.manager, f.env, 1, 0, 0);
    AddJob(f.manager, f.env, 1, 2);
    OBL_CHECK_EQ(f.manager.activeJobs.front().type, 2);
    AddJob(f.manager, f.env, 1, 0);
    OBL_CHECK_EQ(f.manager.activeJobs.front().type, 2);
    OBL_CHECK_EQ(f.Pending(), 0u);
}

OBL_TEST_CASE("repair.add_urgent_job_chooses_list_by_object_class")
{
    Fixture f;
    f.Add(1, 1, 0, 0, 2); // building
    f.Add(2, 2, 0, 0, 5); // scenery
    AddUrgentJob(f.manager, f.env, 1);
    AddUrgentJob(f.manager, f.env, 2);
    AddUrgentJob(f.manager, f.env, 77);
    OBL_CHECK_EQ(f.manager.buildingJobs.size(), 1u);
    OBL_CHECK_EQ(f.manager.sceneryJobs.size(), 1u);
    Job* building = &f.manager.buildingJobs.front();
    Job* scenery = &f.manager.sceneryJobs.front();
    OBL_CHECK_EQ(building->type, 2);
    OBL_CHECK_EQ(building->urgent, 1);
    OBL_CHECK_EQ(scenery->type, 1);
    OBL_CHECK_EQ(scenery->urgent, 1);
    // A normal request already queued turns urgent and takes the urgent type; repeats count.
    f.Add(3, 3, 0, 0, 2);
    AddJob(f.manager, f.env, 3, 0);
    AddUrgentJob(f.manager, f.env, 3);
    Job* promoted = &f.manager.buildingJobs.back();
    OBL_CHECK_EQ(promoted->id, 3);
    OBL_CHECK_EQ(promoted->urgent, 1);
    OBL_CHECK_EQ(promoted->type, 2);
    OBL_CHECK_EQ(promoted->calls, 1u);
    AddUrgentJob(f.manager, f.env, 3);
    OBL_CHECK_EQ(promoted->calls, 2u);
}

OBL_TEST_CASE("repair.add_urgent_job_on_active_job_marks_it_urgent")
{
    Fixture f;
    f.Add(1, 1, 0, 0, 2);
    ClaimJob(f.manager, f.env, 1, 0, 0);
    AddUrgentJob(f.manager, f.env, 1);
    ActiveJob* active = &f.manager.activeJobs.front();
    OBL_CHECK_EQ(active->urgent, 1);
    OBL_CHECK_EQ(active->type, 2); // raised from inspection
    OBL_CHECK_EQ(f.Pending(), 0u);
}

OBL_TEST_CASE("repair.claim_release_started")
{
    Fixture f;
    f.Add(4, 0);
    ClaimJob(f.manager, f.env, 4, 1, 2);
    ClaimJob(f.manager, f.env, 4, 0, 0); // already active: ignored, no count
    OBL_CHECK_EQ(f.Active(), 1u);
    OBL_CHECK_EQ(f.manager.activeCount, 1);
    ActiveJob* job = &f.manager.activeJobs.front();
    OBL_CHECK_EQ(job->repairing, 0);
    OBL_CHECK_EQ(job->urgent, 1);
    OBL_CHECK_EQ(job->type, 2);
    StartedJob(f.manager, f.env, 4);
    StartedJob(f.manager, f.env, 5); // unrecognized: nothing
    OBL_CHECK_EQ(job->repairing, 1);
    ReleaseJob(f.manager, f.env, 5);
    OBL_CHECK_EQ(f.Active(), 1u);
    ReleaseJob(f.manager, f.env, 4);
    OBL_CHECK_EQ(f.Active(), 0u);
    OBL_CHECK_EQ(f.manager.activeCount, 1); // never decremented
}

OBL_TEST_CASE("repair.scan_needs_waiting_time_and_free_object")
{
    Fixture f;
    f.Building(1, 10);
    f.env.time = 5.0f;
    f.manager.buildingJobs.front().timeCalled = 0.0f;
    Scan scan;
    ScanJobs(f.manager, f.env, scan, kOrigin, 2);
    OBL_CHECK_EQ(scan.kind1Found, 0); // exactly 5 is not enough
    f.env.time = 5.001f;
    scan = {};
    ScanJobs(f.manager, f.env, scan, kOrigin, 2);
    OBL_CHECK_EQ(scan.kind1Found, 1);
    OBL_CHECK(scan.kind1 == &f.manager.buildingJobs.front());
    OBL_CHECK(scan.kind1List == &f.manager.buildingJobs);
    OBL_CHECK_EQ(f.manager.buildingJobs.front().distance, 100.0f);
    f.env.objects[1].cleanerClaimed = true;
    scan = {};
    ScanJobs(f.manager, f.env, scan, kOrigin, 2);
    OBL_CHECK_EQ(scan.kind1Found, 0);
}

OBL_TEST_CASE("repair.scan_picks_nearest_per_kind_and_ignores_other_kinds")
{
    Fixture f;
    f.Building(1, 30);
    f.Building(2, 10);
    f.Building(3, 20);
    f.Building(4, 5, 0); // an inspection
    Scan scan;
    ScanJobs(f.manager, f.env, scan, kOrigin, 2);
    OBL_CHECK_EQ(scan.kind1Found, 1);
    OBL_CHECK_EQ(scan.kind1->id, 2);
    OBL_CHECK_EQ(scan.kind0Found, 0);
    OBL_CHECK_EQ(scan.urgentFound, 0);
    scan = {};
    ScanJobs(f.manager, f.env, scan, kOrigin, 0);
    OBL_CHECK_EQ(scan.kind0Found, 1);
    OBL_CHECK_EQ(scan.kind0->id, 4);
    OBL_CHECK_EQ(scan.kind1Found, 0);
    scan = {};
    ScanJobs(f.manager, f.env, scan, kOrigin, 1); // scenery kind with no scenery jobs
    OBL_CHECK_EQ(scan.kind1Found + scan.kind0Found + scan.urgentFound, 0);
}

OBL_TEST_CASE("repair.scan_urgent_skips_scenery_pass_and_uses_mechanic_flag_for_buildings")
{
    Fixture f;
    f.Building(1, 50);
    f.Add(2, 5, 0, 0, 2);
    AddUrgentJob(f.manager, f.env, 2);
    f.manager.buildingJobs.back().timeCalled = 0.0f;
    f.Add(3, 1, 0, 0, 5);
    AddUrgentJob(f.manager, f.env, 3); // urgent scenery job, nearest of all
    f.manager.sceneryJobs.front().timeCalled = 0.0f;
    Scan scan;
    ScanJobs(f.manager, f.env, scan, kOrigin, 2);
    OBL_CHECK_EQ(scan.urgentFound, 1);
    OBL_CHECK_EQ(scan.urgent->id, 2); // the building pass found one, the scenery list is not looked at
    OBL_CHECK_EQ(f.manager.sceneryJobs.front().distance, 0.0f); // the scenery record was never measured
    f.env.objects[2].mechanicClaimed = true; // a claimed urgent building is skipped ...
    scan = {};
    ScanJobs(f.manager, f.env, scan, kOrigin, 2);
    OBL_CHECK_EQ(scan.urgentFound, 1);
    OBL_CHECK_EQ(scan.urgent->id, 3); // ... so the scenery pass runs
    OBL_CHECK(scan.urgentList == &f.manager.sceneryJobs);
    OBL_CHECK(scan.kind1List == &f.manager.buildingJobs);
    OBL_CHECK_EQ(scan.kind1Found, 1); // and the normal building job was found by the first pass
    f.env.objects[3].cleanerClaimed = true; // urgent scenery tests the cleaner flag
    scan = {};
    ScanJobs(f.manager, f.env, scan, kOrigin, 2);
    OBL_CHECK_EQ(scan.urgentFound, 0);
}

OBL_TEST_CASE("repair.scan_ties_keep_the_first_candidate")
{
    Fixture f;
    f.Building(1, 3.0f);
    f.Building(2, 3.0f);
    Scan scan;
    ScanJobs(f.manager, f.env, scan, kOrigin, 2);
    OBL_CHECK_EQ(scan.kind1->id, 1); // ties keep the first
}

OBL_TEST_CASE("repair.find_job_claims_and_dequeues_the_winner")
{
    Fixture f;
    f.Building(1, 30);
    f.Building(2, 10);
    uint32_t type = 99;
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 2, &type), 2);
    OBL_CHECK_EQ(type, 2u);
    OBL_CHECK_EQ(f.Active(), 1u);
    OBL_CHECK_EQ(f.Pending(), 1u);
    OBL_CHECK(f.env.objects[2].mechanicClaimed);
    OBL_CHECK_EQ(f.manager.activeJobs.front().urgent, 0);
    type = 99;
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 1, &type), -1); // nothing of kind 1
    OBL_CHECK_EQ(type, 99u); // untouched on failure
}

OBL_TEST_CASE("repair.find_job_prefers_urgent_and_falls_back_to_inspection")
{
    Fixture f;
    f.Building(1, 5, 0);
    f.Add(2, 90, 0, 0, 2);
    AddUrgentJob(f.manager, f.env, 2);
    f.manager.buildingJobs.back().timeCalled = 0.0f;
    uint32_t type = 0;
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 0, &type), 2); // urgent wins whatever the kind or distance
    OBL_CHECK_EQ(f.manager.activeJobs.front().urgent, 1);
    OBL_CHECK_EQ(type, 2u);
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 0, &type), 1);
    OBL_CHECK_EQ(type, 0u);
    OBL_CHECK_EQ(f.Pending(), 0u);
}

OBL_TEST_CASE("repair.find_job_watering_uses_plants_within_radius")
{
    Fixture f;
    PlantInfo closePlant;
    closePlant.id = 7;
    closePlant.pos[0] = 50;
    closePlant.waterable = true;
    PlantInfo farPlant = closePlant;
    farPlant.id = 8;
    farPlant.pos[0] = 500;
    f.env.plants = {farPlant, closePlant};
    f.env.rolls = {0, 1}; // draws index 0 (too far), then index 1
    uint32_t type = 0;
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 3, &type), 7);
    OBL_CHECK_EQ(type, 3u);
    ActiveJob* job = &f.manager.activeJobs.front();
    OBL_CHECK_EQ(job->urgent, 1);
    OBL_CHECK_EQ(job->type, 3);
    OBL_CHECK_EQ(f.env.rolled, 2u);
    // The plant is active now, so asking again finds nothing (and consumes one draw per plant).
    f.env.rolled = 0;
    f.env.rolls = {1};
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 3, &type), -1);
    OBL_CHECK_EQ(f.env.rolled, 2u);
}

OBL_TEST_CASE("repair.find_plant_far_rule_draws_a_second_time")
{
    Fixture f;
    PlantInfo p;
    p.id = 1;
    p.waterable = true;
    p.pos[0] = 130;
    f.env.plants = {p};
    // draw 0: index 0; draw 1 % 3 == 0 -> needs more than 150: 130 does not qualify.
    f.env.rolls = {0, 3};
    OBL_CHECK_EQ(FindPlantToWater(f.manager, f.env, kOrigin, 0), -1);
    OBL_CHECK_EQ(f.env.rolled, 2u);
    // draw 1 % 3 != 0 -> needs more than 120: it does.
    f.env.rolled = 0;
    f.env.rolls = {0, 1};
    OBL_CHECK_EQ(FindPlantToWater(f.manager, f.env, kOrigin, 0), 1);
    // Dying and non-waterable plants are skipped before any second draw.
    f.env.plants[0].inactive = true;
    f.env.rolled = 0;
    OBL_CHECK_EQ(FindPlantToWater(f.manager, f.env, kOrigin, 0), -1);
    OBL_CHECK_EQ(f.env.rolled, 1u);
    f.env.plants[0].inactive = false;
    f.env.plants[0].waterable = false;
    f.env.rolled = 0;
    OBL_CHECK_EQ(FindPlantToWater(f.manager, f.env, kOrigin, 0), -1);
    OBL_CHECK_EQ(f.env.rolled, 1u);
    f.env.plants.clear();
    f.env.rolled = 0;
    OBL_CHECK_EQ(FindPlantToWater(f.manager, f.env, kOrigin, 0), -1);
    OBL_CHECK_EQ(f.env.rolled, 0u); // no plants: no draw
}

OBL_TEST_CASE("repair.find_watering_job_claims_a_normal_watering_job")
{
    Fixture f;
    PlantInfo p;
    p.id = 9;
    p.waterable = true;
    p.pos[1] = 200;
    f.env.plants = {p};
    f.env.rolls = {0, 1};
    f.Add(9, 0, 200);
    OBL_CHECK_EQ(FindWateringJob(f.manager, f.env, kOrigin), 9);
    ActiveJob* job = &f.manager.activeJobs.front();
    OBL_CHECK_EQ(job->type, 3);
    OBL_CHECK_EQ(job->urgent, 0);
    OBL_CHECK(f.env.objects[9].mechanicClaimed);
    OBL_CHECK_EQ(FindWateringJob(f.manager, f.env, kOrigin), -1);
}

OBL_TEST_CASE("repair.return_job_requeues_by_kind")
{
    Fixture f;
    f.Add(1, 1, 0, 0, 2);
    f.Add(2, 2, 0, 0, 2);
    f.Add(3, 3, 0, 0, 2);
    f.env.objects[1].mechanicClaimed = f.env.objects[2].mechanicClaimed = f.env.objects[3].mechanicClaimed = true;
    ClaimJob(f.manager, f.env, 1, 0, 2); // normal breakdown
    ClaimJob(f.manager, f.env, 2, 1, 2); // urgent
    ClaimJob(f.manager, f.env, 3, 1, 3); // watering
    ReturnJob(f.manager, f.env, 1, 2);
    ReturnJob(f.manager, f.env, 2, 2);
    ReturnJob(f.manager, f.env, 3, 3);
    OBL_CHECK_EQ(f.Active(), 0u);
    OBL_CHECK_EQ(f.manager.buildingJobs.size(), 2u); // 1 normal, 2 urgent; the watering job is not queued
    OBL_CHECK_EQ(f.manager.buildingJobs.front().id, 1);
    OBL_CHECK_EQ(f.manager.buildingJobs.front().urgent, 0);
    OBL_CHECK_EQ(f.manager.buildingJobs.back().id, 2);
    OBL_CHECK_EQ(f.manager.buildingJobs.back().urgent, 1);
    OBL_CHECK(!f.env.objects[1].mechanicClaimed);
    OBL_CHECK(!f.env.objects[2].mechanicClaimed);
    OBL_CHECK(!f.env.objects[3].mechanicClaimed);
    // A return without an active record is a plain AddJob.
    ReturnJob(f.manager, f.env, 1, 2);
    OBL_CHECK_EQ(f.manager.buildingJobs.front().calls, 1u);
}

OBL_TEST_CASE("repair.look_for_better_job_urgent_replaces_normal")
{
    Fixture f;
    f.Building(1, 10);
    uint32_t type = 0;
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 2, &type), 1); // the mechanic holds building 1
    f.Add(2, 80, 0, 0, 2);
    AddUrgentJob(f.manager, f.env, 2);
    f.manager.buildingJobs.front().timeCalled = 0.0f;
    type = 99;
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 1, &type), 2);
    OBL_CHECK_EQ(type, 2u);
    OBL_CHECK(!f.env.objects[1].mechanicClaimed); // returned to the queue
    OBL_CHECK(f.env.objects[2].mechanicClaimed);
    OBL_CHECK_EQ(f.Active(), 1u);
    OBL_CHECK_EQ(f.manager.activeJobs.front().id, 2);
    OBL_CHECK_EQ(f.manager.activeJobs.front().urgent, 1);
    OBL_CHECK_EQ(f.manager.buildingJobs.size(), 1u);
    OBL_CHECK_EQ(f.manager.buildingJobs.front().id, 1);
}

OBL_TEST_CASE("repair.look_for_better_job_normal_needs_nearer_or_higher_type")
{
    Fixture f;
    f.Building(1, 10);
    uint32_t type = 0;
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 2, &type), 1);
    f.Building(2, 20);
    type = 99;
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 1, &type), -1); // farther: stay
    OBL_CHECK_EQ(type, 99u);
    f.Building(3, 20); // equal-or-farther jobs keep losing
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 1, &type), -1);
    f.Building(4, 5);
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 1, &type), 4); // nearer: switch
    OBL_CHECK_EQ(f.manager.activeJobs.front().id, 4);
    OBL_CHECK_EQ(f.manager.activeJobs.front().urgent, 0);
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 77, &type), -1); // not held
}

OBL_TEST_CASE("repair.look_for_better_job_inspection_gives_way_to_breakdown_only_when_kinds_allow")
{
    Fixture f;
    f.Building(1, 10, 0);
    uint32_t type = 0;
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 0, &type), 1);
    OBL_CHECK_EQ(type, 0u);
    f.Building(2, 500, 2); // a far breakdown: the scan asks for kind 0, so it is not even a candidate
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 1, &type), -1);
    f.Building(3, 5, 0); // a nearer inspection of the same type
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 1, &type), 3);
}

OBL_TEST_CASE("repair.look_for_better_job_urgent_holder_only_yields_to_nearer_urgent")
{
    Fixture f;
    f.Add(1, 20, 0, 0, 2);
    AddUrgentJob(f.manager, f.env, 1);
    f.manager.buildingJobs.front().timeCalled = 0.0f;
    uint32_t type = 0;
    OBL_CHECK_EQ(FindJob(f.manager, f.env, kOrigin, 2, &type), 1);
    f.Add(2, 30, 0, 0, 2);
    AddUrgentJob(f.manager, f.env, 2);
    f.manager.buildingJobs.front().timeCalled = 0.0f;
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 1, &type), -1); // farther urgent
    f.Add(3, 10, 0, 0, 2);
    AddUrgentJob(f.manager, f.env, 3);
    f.manager.buildingJobs.back().timeCalled = 0.0f;
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 1, &type), 3);
    f.Building(4, 1); // a normal job never replaces an urgent one
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 3, &type), -1);
}

OBL_TEST_CASE("repair.look_for_better_job_ignores_urgent_watering")
{
    Fixture f;
    f.Add(5, 10);
    ClaimJob(f.manager, f.env, 5, 1, 3);
    f.Add(6, 1, 0, 0, 2);
    AddUrgentJob(f.manager, f.env, 6);
    f.manager.buildingJobs.front().timeCalled = 0.0f;
    uint32_t type = 0;
    OBL_CHECK_EQ(LookForBetterJob(f.manager, f.env, kOrigin, 5, &type), -1);
}

OBL_TEST_CASE("repair.claim_object_building_takes_the_queued_request")
{
    Fixture f;
    f.Building(1, 10, 0); // a queued inspection
    f.Add(2, 20, 0, 0, 2); // a building nobody asked for
    uint32_t type = 99;
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 1, &type), 1);
    OBL_CHECK_EQ(type, 0u); // the pending request's own type
    OBL_CHECK_EQ(f.Pending(), 0u);
    ActiveJob* job = &f.manager.activeJobs.front();
    OBL_CHECK_EQ(job->urgent, 1);
    OBL_CHECK_EQ(job->type, 2); // claimed as an urgent breakdown whatever the request said
    OBL_CHECK(f.env.objects[1].mechanicClaimed);
    OBL_CHECK(f.env.objects[1].directed);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 1, &type), -1); // now claimed
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 2, &type), 2);
    OBL_CHECK_EQ(type, 2u);
    OBL_CHECK_EQ(f.Active(), 2u);
}

OBL_TEST_CASE("repair.claim_object_scenery_and_refusals")
{
    Fixture f;
    f.Add(1, 1, 0, 0, 1); // a bin
    f.Add(2, 2, 0, 0, 1).waterable = true; // a plant
    f.Add(3, 3, 0, 0, 3); // a guest
    uint32_t type = 99;
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 1, &type), 1);
    OBL_CHECK_EQ(type, 1u);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 2, &type), 2);
    OBL_CHECK_EQ(type, 3u);
    type = 99;
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 3, &type), -1);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 77, &type), -1);
    f.Add(4, 4, 0, 0, 2).cleanerClaimed = true;
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 4, &type), -1);
    OBL_CHECK_EQ(type, 99u);
    // A queued scenery request is removed from the scenery list.
    f.Add(5, 5, 0, 0, 1);
    AddJob(f.manager, f.env, 5, 1);
    OBL_CHECK_EQ(f.manager.sceneryJobs.size(), 1u);
    OBL_CHECK_EQ(ClaimObject(f.manager, f.env, 5, &type), 5);
    OBL_CHECK_EQ(type, 1u);
    OBL_CHECK_EQ(f.manager.sceneryJobs.size(), 0u);
}

OBL_TEST_CASE("repair.get_job_status_and_type")
{
    Fixture f;
    f.Add(1, 1, 0, 0, 2); // a building
    f.Add(2, 2, 0, 0, 1); // scenery
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 1), 2);
    OBL_CHECK_EQ(GetJobType(f.manager, f.env, 1), -1);
    AddJob(f.manager, f.env, 1, 2);
    AddJob(f.manager, f.env, 2, 1);
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 1), 3); // a breakdown
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 2), 4); // another type
    ClaimJob(f.manager, f.env, 1, 0, 2);
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 1), 5);
    OBL_CHECK_EQ(GetJobType(f.manager, f.env, 1), 2);
    StartedJob(f.manager, f.env, 1);
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 1), 6);
    // A scenery id is looked up in the scenery list only.
    f.Add(3, 3, 0, 0, 1);
    AddJob(f.manager, f.env, 3, 2);
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 3), 2);
    f.env.mechanics = 0;
    OBL_CHECK_EQ(GetJobStatus(f.manager, f.env, 1), 0);
    ReleaseJob(f.manager, f.env, 1);
    OBL_CHECK_EQ(GetJobType(f.manager, f.env, 1), -1);
}
