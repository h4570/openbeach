#include "unit_test.h"

#include "importer/src/readers/building_behavior_keys.h"
#include "game/construction/construction_job_start.h"

#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Construction job rules (docs/specs/construction.md): the construction manager start build routine and the construction manager start demolish routine.

using namespace obl;
using namespace obl::messages;
using namespace obl::construction;

namespace {

struct Sent {
    int sender, receiver, type, data;
};

struct Fixture {
    Manager manager;
    std::map<int, StartFacts> buildings; // building id -> facts; the key also means the object exists
    std::map<int, std::vector<int>> tiles; // building id -> tile
    std::map<int, std::vector<int>> extents; // building id -> left, top, right, bottom
    bool startup = false;
    std::vector<std::string> calls; // the calls into the game, in order
    std::vector<Sent> posted, toHumans;
    std::function<void()> onReallocate;

    // An older job of a building already in the list.
    Job& AddOldJob(int id)
    {
        Job& job = *manager.jobs.emplace_back(std::make_unique<Job>());
        job.m_buildingId = id;
        return job;
    }
    StartFacts& Building(int id, bool footprint = true)
    {
        StartFacts& facts = buildings[id];
        facts.hasDefinition = true;
        facts.buildTime = 300;
        facts.demolishTime = 200;
        facts.hasFootprint = footprint;
        tiles[id] = {12, 34};
        extents[id] = {100, 200, 164, 280};
        return facts;
    }
    Job* Last() const { return manager.jobs.back().get(); }
    size_t Count() const { return manager.jobs.size(); }

    // Env contract of construction::StartBuild / StartDemolish.
    bool Startup() { return startup; }
    bool Building(int id, StartFacts* facts)
    {
        auto it = buildings.find(id);
        if (it == buildings.end()) return false;
        *facts = it->second;
        return true;
    }
    void ApplyBuildingToGrid(int id) { calls.push_back("grid " + std::to_string(id)); }
    void CollectWorkPoints(int id) { calls.push_back("points " + std::to_string(id)); }
    void Reallocate()
    {
        calls.push_back("reallocate");
        if (onReallocate) onReallocate();
    }
    void Post(int sender, int receiver, int type, int data)
    {
        calls.push_back("post");
        posted.push_back({sender, receiver, type, data});
    }
    void PostToHumans(int sender, int type, int data)
    {
        calls.push_back("humans");
        toHumans.push_back({sender, -1, type, data});
    }
    void RemoveRepairJob(int id) { calls.push_back("repair " + std::to_string(id)); }
    void RemoveCleaningJob(int id) { calls.push_back("cleaning " + std::to_string(id)); }
    bool TilePosition(int id, int out[2])
    {
        auto it = tiles.find(id);
        if (it == tiles.end()) return false;
        out[0] = it->second[0];
        out[1] = it->second[1];
        return true;
    }
    void ApplyFootprint(uint32_t x, uint32_t y, int phase, int mode, int id)
    {
        calls.push_back("footprint " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(phase) + " " +
                std::to_string(mode) + " " + std::to_string(id));
    }
    bool Extents(int id, int out[4])
    {
        auto it = extents.find(id);
        if (it == extents.end()) return false;
        for (int i = 0; i < 4; ++i) out[i] = it->second[i];
        return true;
    }
    void QueueBlockRebuild(int x, int y, int width, int height)
    {
        calls.push_back(
                "rebuild " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(width) + " " + std::to_string(height));
    }

    bool Build(int id) { return StartBuild(manager, id, *this); }
    bool Demolish(int id) { return StartDemolish(manager, id, *this); }
};

} // namespace

OBL_TEST_CASE("construction.start_build_creates_a_started_construction_job_and_reallocates")
{
    Fixture f;
    f.Building(7);
    OBL_CHECK(f.Build(7));
    OBL_CHECK_EQ(f.Count(), 1u);
    const Job& job = *f.Last();
    OBL_CHECK_EQ(job.m_buildingId, 7);
    OBL_CHECK_EQ(job.m_underConstruction, 1);
    OBL_CHECK_EQ(job.m_underDemolition, 0);
    OBL_CHECK_EQ(job.m_started, 1);
    OBL_CHECK_EQ(job.m_finished, 0);
    OBL_CHECK_EQ(job.m_constructionRemaining, 300.0f);
    OBL_CHECK_EQ(job.m_demolitionRemaining, 200.0f);
    OBL_CHECK_EQ(f.manager.m_constructionCount, 1);
    const std::vector<std::string> expected = {"grid 7", "points 7", "reallocate"};
    OBL_CHECK(f.calls == expected);
    OBL_CHECK(f.posted.empty());
}

OBL_TEST_CASE("construction.start_build_without_a_footprint_collects_no_work_points")
{
    Fixture f;
    f.Building(7, false);
    OBL_CHECK(f.Build(7));
    const std::vector<std::string> expected = {"grid 7", "reallocate"};
    OBL_CHECK(f.calls == expected);
    OBL_CHECK(!f.Last()->hasWorkPoints);
}

OBL_TEST_CASE("construction.start_build_during_the_start_up_counts_nothing_and_does_not_reallocate")
{
    Fixture f;
    f.Building(7);
    f.startup = true;
    OBL_CHECK(f.Build(7));
    OBL_CHECK_EQ(f.manager.m_constructionCount, 0);
    const std::vector<std::string> expected = {"grid 7", "points 7", "post"};
    OBL_CHECK(f.calls == expected);
    OBL_REQUIRE(f.posted.size() == 1);
    OBL_CHECK_EQ(f.posted[0].sender, 7);
    OBL_CHECK_EQ(f.posted[0].receiver, 7);
    OBL_CHECK_EQ(f.posted[0].type, MT_STATECHANGE);
    OBL_CHECK_EQ(f.posted[0].data, 0);
    OBL_CHECK_EQ(f.Last()->m_underConstruction, 1);
    OBL_CHECK_EQ(f.Last()->m_started, 1);
}

OBL_TEST_CASE("construction.start_build_of_a_jetty_counts_but_sends_the_state_change_instead_of_reallocating")
{
    Fixture f;
    f.Building(7).jetty = true;
    OBL_CHECK(f.Build(7));
    OBL_CHECK_EQ(f.manager.m_constructionCount, 1);
    const std::vector<std::string> expected = {"grid 7", "points 7", "post"};
    OBL_CHECK(f.calls == expected);
    OBL_CHECK(BehaviorFromKey("JETTY_2") == BuildingBehavior::Jetty && BehaviorFromKey("BIG_JETTY") == BuildingBehavior::Jetty);
    OBL_CHECK(BehaviorFromKey("JETT") == BuildingBehavior::None && BehaviorFromKey("jetty") == BuildingBehavior::None);
}

OBL_TEST_CASE("construction.start_build_marks_the_first_job_of_the_building_started_not_the_new_one")
{
    Fixture f;
    f.Building(7);
    Job& older = f.AddOldJob(7);
    OBL_CHECK(f.Build(7));
    OBL_CHECK_EQ(f.Count(), 2u);
    OBL_CHECK_EQ(older.m_started, 1); // FindJob finds the older job
    OBL_CHECK_EQ(older.m_underConstruction, 0);
    OBL_CHECK_EQ(f.Last()->m_started, 0); // the new job is only flagged as a construction
    OBL_CHECK_EQ(f.Last()->m_underConstruction, 1);
    OBL_CHECK(f.Last() != &older);
}

OBL_TEST_CASE("construction.start_build_appends_after_the_other_jobs")
{
    Fixture f;
    f.Building(7);
    f.Building(8);
    OBL_CHECK(f.Build(7));
    OBL_CHECK(f.Build(8));
    OBL_CHECK_EQ(f.Count(), 2u);
    OBL_CHECK_EQ(f.manager.jobs.front()->m_buildingId, 7);
    OBL_CHECK_EQ(f.Last()->m_buildingId, 8);
    OBL_CHECK_EQ(f.manager.m_constructionCount, 2);
}

OBL_TEST_CASE("construction.start_build_refuses_a_missing_object_or_definition")
{
    Fixture f;
    OBL_CHECK(!f.Build(7)); // no object
    f.buildings[7].hasDefinition = false;
    OBL_CHECK(!f.Build(7)); // no definition
    OBL_CHECK_EQ(f.Count(), 0u);
    OBL_CHECK_EQ(f.manager.m_constructionCount, 0);
    OBL_CHECK(f.calls.empty());
}

OBL_TEST_CASE("construction.start_demolish_creates_a_demolition_job_and_tells_the_world")
{
    Fixture f;
    f.Building(7);
    OBL_CHECK(f.Demolish(7));
    OBL_CHECK_EQ(f.Count(), 1u);
    const Job& job = *f.Last();
    OBL_CHECK_EQ(job.m_buildingId, 7);
    OBL_CHECK_EQ(job.m_underDemolition, 1);
    OBL_CHECK_EQ(job.m_underConstruction, 0);
    OBL_CHECK_EQ(job.m_started, 0); // the building's update starts it
    OBL_CHECK_EQ(f.manager.m_constructionCount, 1);
    const std::vector<std::string> expected = {
            "points 7", "humans", "repair 7", "cleaning 7", "footprint 12 34 0 3 7", "rebuild 100 200 64 80", "reallocate"};
    OBL_CHECK(f.calls == expected);
    OBL_REQUIRE(f.toHumans.size() == 1);
    OBL_CHECK_EQ(f.toHumans[0].sender, 7);
    OBL_CHECK_EQ(f.toHumans[0].receiver, -1);
    OBL_CHECK_EQ(f.toHumans[0].type, MT_BUILDING_BEING_REMOVED);
    OBL_CHECK_EQ(f.toHumans[0].data, 0);
    OBL_CHECK(f.posted.empty());
}

OBL_TEST_CASE("construction.start_demolish_counts_during_the_start_up_too_and_always_reallocates")
{
    Fixture f;
    f.Building(7);
    f.startup = true;
    OBL_CHECK(f.Demolish(7));
    OBL_CHECK_EQ(f.manager.m_constructionCount, 1);
    OBL_CHECK_EQ(f.calls.back(), std::string("reallocate"));
    OBL_CHECK(f.posted.empty());
}

OBL_TEST_CASE("construction.start_demolish_without_a_footprint_neither_collects_points_nor_frees_cells")
{
    Fixture f;
    f.Building(7, false);
    OBL_CHECK(f.Demolish(7));
    const std::vector<std::string> expected = {"humans", "repair 7", "cleaning 7", "rebuild 100 200 64 80", "reallocate"};
    OBL_CHECK(f.calls == expected);
}

OBL_TEST_CASE("construction.start_demolish_rebuild_rectangle_is_a_32_bit_difference")
{
    Fixture f;
    f.Building(7, false);
    f.extents[7] = {static_cast<int>(0x80000000u), 5, 0x7fffffff, 3}; // right - left wraps, bottom - top is negative
    OBL_CHECK(f.Demolish(7));
    const std::string rebuild = f.calls[f.calls.size() - 2];
    OBL_CHECK_EQ(rebuild, std::string("rebuild -2147483648 5 -1 -2"));
}

OBL_TEST_CASE("construction.start_demolish_keeps_an_older_job_of_the_same_building_and_flags_the_new_one")
{
    Fixture f;
    f.Building(7);
    Job& older = f.AddOldJob(7);
    older.m_underConstruction = 1;
    older.m_started = 1;
    f.manager.m_constructionCount = 1;
    OBL_CHECK(f.Demolish(7));
    OBL_CHECK_EQ(f.Count(), 2u);
    OBL_CHECK_EQ(older.m_underDemolition, 0);
    OBL_CHECK_EQ(f.Last()->m_underDemolition, 1);
    OBL_CHECK_EQ(f.manager.m_constructionCount, 2);
}

OBL_TEST_CASE("construction.start_demolish_refuses_a_missing_object_or_definition")
{
    Fixture f;
    OBL_CHECK(!f.Demolish(7));
    f.buildings[7].hasDefinition = false;
    OBL_CHECK(!f.Demolish(7));
    OBL_CHECK_EQ(f.Count(), 0u);
    OBL_CHECK_EQ(f.manager.m_constructionCount, 0);
    OBL_CHECK(f.toHumans.empty());
}
