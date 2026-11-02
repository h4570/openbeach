#include "unit_test.h"

#include "game/construction/construction_jobs.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

// Construction job rules (docs/specs/construction.md): the builder remove cost point routine.

using namespace obl;
using namespace obl::construction;

namespace {

struct Sent {
    bool delayed;
    int sender, receiver, type, data;
    float delay;
};

struct Fixture {
    Manager manager;
    std::vector<Job*> jobs; // in the order AddJob made them
    std::map<int, CostFacts> buildings; // building id -> definition facts; the key also means the object exists
    float frameTime = 0.02f;
    std::vector<Sent> sent;
    std::function<void(const Sent&)> onSend; // runs inside Post, like a builder's script

    // A started construction job of 1000 work units that has not been touched yet.
    Job& AddJob(int id, bool construction = true)
    {
        Job& job = *manager.jobs.emplace_back(std::make_unique<Job>());
        jobs.push_back(&job);
        job.m_buildingId = id;
        job.m_underConstruction = construction;
        job.m_underDemolition = !construction;
        job.m_started = 1;
        job.m_constructionRemaining = job.m_demolitionRemaining = 1000.0f;
        ++manager.m_constructionCount;
        CostFacts facts;
        facts.hasDefinition = true;
        facts.buildTime = facts.demolishTime = 1000;
        buildings[id] = facts;
        return job;
    }
    Builder& AddBuilder(int id, int building)
    {
        auto& entry = manager.m_builders[manager.m_builderCount++];
        entry.m_builderId = id;
        entry.m_buildingId = building;
        return entry;
    }

    // Env contract of construction::RemoveCostPoint.
    bool Building(int id, CostFacts* facts)
    {
        auto it = buildings.find(id);
        if (it == buildings.end()) return false;
        *facts = it->second;
        return true;
    }
    float FrameTime() { return frameTime; }
    void Post(int sender, int receiver, int type, int data) { Record({false, sender, receiver, type, data, 0.0f}); }
    void PostDelayed(int sender, int receiver, int type, int data, float delay) { Record({true, sender, receiver, type, data, delay}); }
    void Record(const Sent& message)
    {
        sent.push_back(message);
        if (onSend) onSend(message);
    }

    bool Run(int id) { return RemoveCostPoint(manager, id, *this); }
};

} // namespace

OBL_TEST_CASE("construction.cost_point_takes_frame_time_times_a_hundredth_off_a_construction_job")
{
    Fixture f;
    Job& job = f.AddJob(7);
    f.frameTime = 0.5f;
    OBL_CHECK(f.Run(7));
    const double expected = 1000.0 - static_cast<double>(0.5f) * static_cast<double>(0.01f);
    OBL_CHECK_EQ(job.m_constructionRemaining, static_cast<float>(expected));
    OBL_CHECK_EQ(job.m_constructionProgress, static_cast<float>(1.0 - expected / 1000.0));
    OBL_CHECK_EQ(job.m_demolitionRemaining, 1000.0f); // the other kind is not touched
    OBL_CHECK_EQ(job.m_demolitionProgress, 0.0f);
    OBL_CHECK_EQ(job.m_finished, 0);
    OBL_CHECK(f.sent.empty());
    OBL_CHECK_EQ(f.manager.m_constructionCount, 1);
}

OBL_TEST_CASE("construction.cost_point_uses_the_demolition_fields_without_the_construction_flag")
{
    Fixture f;
    Job& job = f.AddJob(7, false);
    f.buildings[7].demolishTime = 400;
    f.buildings[7].buildTime = 99999;
    f.frameTime = 10.0f;
    OBL_CHECK(f.Run(7));
    const double expected = 1000.0 - 10.0 * static_cast<double>(0.01f);
    OBL_CHECK_EQ(job.m_demolitionRemaining, static_cast<float>(expected));
    OBL_CHECK_EQ(job.m_demolitionProgress, static_cast<float>(1.0 - expected / 400.0)); // the demolition time is the total
    OBL_CHECK_EQ(job.m_constructionRemaining, 1000.0f);
    // A job with neither flag counts as a demolition too.
    Job& other = f.AddJob(8, false);
    other.m_underDemolition = 0;
    OBL_CHECK(f.Run(8));
    OBL_CHECK(other.m_demolitionRemaining < 1000.0f);
    OBL_CHECK_EQ(other.m_constructionRemaining, 1000.0f);
}

OBL_TEST_CASE("construction.cost_point_progress_comes_from_the_unrounded_remaining_work")
{
    // remaining - step is not a float: the stored value is rounded, the progress uses the double.
    Fixture f;
    Job& job = f.AddJob(7);
    job.m_constructionRemaining = 16777216.0f;
    f.buildings[7].buildTime = 3;
    f.frameTime = 100.0f;
    const double left = 16777216.0 - static_cast<double>(100.0f) * static_cast<double>(0.01f);
    OBL_CHECK(f.Run(7));
    OBL_CHECK_EQ(job.m_constructionRemaining, static_cast<float>(left));
    OBL_CHECK_EQ(job.m_constructionProgress, static_cast<float>(1.0 - left / 3.0));
    OBL_CHECK(static_cast<double>(job.m_constructionRemaining) != left);
}

OBL_TEST_CASE("construction.cost_point_a_total_of_zero_or_above_two_to_the_31_converts_as_unsigned")
{
    Fixture f;
    Job& job = f.AddJob(7);
    f.buildings[7].buildTime = 0;
    OBL_CHECK(f.Run(7));
    OBL_CHECK(std::isinf(job.m_constructionProgress) && job.m_constructionProgress < 0); // 1 - positive / 0
    Job& other = f.AddJob(8);
    f.buildings[8].buildTime = 0x80000000u;
    OBL_CHECK(f.Run(8));
    OBL_CHECK_EQ(other.m_constructionProgress,
            static_cast<float>(1.0 - (1000.0 - static_cast<double>(0.02f) * static_cast<double>(0.01f)) / 2147483648.0));
}

OBL_TEST_CASE("construction.cost_point_does_nothing_without_object_job_or_started_flag")
{
    Fixture f;
    Job& job = f.AddJob(7);
    f.buildings.erase(7); // a job whose building object is gone
    OBL_CHECK(f.Run(7));
    OBL_CHECK_EQ(job.m_constructionRemaining, 1000.0f);
    OBL_CHECK(f.Run(99)); // no object, no job
    f.buildings[99] = CostFacts{};
    f.buildings[99].hasDefinition = true;
    OBL_CHECK(f.Run(99)); // an object without a job
    Job& idle = f.AddJob(8);
    idle.m_started = 0;
    OBL_CHECK(f.Run(8));
    OBL_CHECK_EQ(idle.m_constructionRemaining, 1000.0f);
    OBL_CHECK(f.sent.empty());
    OBL_CHECK_EQ(f.manager.m_constructionCount, 2);
    idle.m_started = 0x80; // any non-zero byte counts as started
    OBL_CHECK(f.Run(8));
    OBL_CHECK(idle.m_constructionRemaining < 1000.0f);
}

OBL_TEST_CASE("construction.cost_point_a_finished_job_is_worked_but_ends_nothing_again")
{
    Fixture f;
    Job& job = f.AddJob(7);
    job.m_constructionRemaining = 0.0f;
    job.m_finished = 1;
    f.AddBuilder(1, 7);
    OBL_CHECK(f.Run(7));
    OBL_CHECK(job.m_constructionRemaining < 0.0f);
    OBL_CHECK(f.sent.empty());
    OBL_CHECK_EQ(f.manager.m_constructionCount, 1);
}

OBL_TEST_CASE("construction.cost_point_the_end_is_reached_at_zero_remaining_work_and_on_nan_not_above")
{
    Fixture f;
    Job& exact = f.AddJob(1);
    exact.m_constructionRemaining = 0.0f;
    Job& above = f.AddJob(2);
    above.m_constructionRemaining = 0.1f;
    Job& nan = f.AddJob(3);
    nan.m_constructionRemaining = std::numeric_limits<float>::quiet_NaN();
    f.AddBuilder(1, 100); // nobody works on these jobs: only the building message goes out
    OBL_CHECK(f.Run(2));
    OBL_CHECK_EQ(above.m_finished, 0);
    f.frameTime = 0.0f; // nothing taken off: the stored 0.0f is exactly "not above 0"
    OBL_CHECK(f.Run(1));
    OBL_CHECK_EQ(exact.m_finished, 1);
    OBL_CHECK_EQ(exact.m_constructionRemaining, 0.0f);
    f.frameTime = 0.02f;
    OBL_CHECK(f.Run(3));
    OBL_CHECK_EQ(nan.m_finished, 1);
    OBL_CHECK_EQ(f.sent.size(), 2u);
    OBL_CHECK_EQ(f.manager.m_constructionCount, 1);
}

OBL_TEST_CASE("construction.cost_point_ending_a_construction_tells_the_builders_then_the_building")
{
    Fixture f;
    Job& job = f.AddJob(7);
    Job& other = f.AddJob(8);
    job.m_constructionRemaining = 0.0f;
    f.AddBuilder(11, 7);
    f.AddBuilder(12, 8); // another job
    f.AddBuilder(13, -1);
    f.AddBuilder(14, 7);
    f.AddBuilder(11, 7);
    OBL_CHECK(f.Run(7));
    OBL_CHECK_EQ(job.m_finished, 1);
    OBL_CHECK_EQ(other.m_finished, 0);
    OBL_CHECK_EQ(f.sent.size(), 4u);
    const int builders[] = {11, 14, 11};
    for (int i = 0; i < 3; ++i) {
        OBL_CHECK(!f.sent[i].delayed);
        OBL_CHECK_EQ(f.sent[i].sender, builders[i]);
        OBL_CHECK_EQ(f.sent[i].receiver, builders[i]);
        OBL_CHECK_EQ(f.sent[i].type, 95);
        OBL_CHECK_EQ(f.sent[i].data, 0);
    }
    OBL_CHECK(!f.sent[3].delayed); // a construction tells the building at once
    OBL_CHECK_EQ(f.sent[3].sender, 7);
    OBL_CHECK_EQ(f.sent[3].receiver, 7);
    OBL_CHECK_EQ(f.sent[3].type, 98);
    OBL_CHECK_EQ(f.sent[3].data, 0x28);
    OBL_CHECK_EQ(f.manager.m_constructionCount, 1);
}

OBL_TEST_CASE("construction.cost_point_ending_a_demolition_delays_the_building_message_in_water")
{
    Fixture f;
    Job& dry = f.AddJob(7, false);
    Job& wet = f.AddJob(8, false);
    dry.m_demolitionRemaining = wet.m_demolitionRemaining = 0.0f;
    f.buildings[7].buildOn = 0x3; // land and beach
    f.buildings[8].buildOn = 0x4; // shallow water
    f.AddBuilder(11, 8);
    OBL_CHECK(f.Run(7));
    OBL_CHECK_EQ(f.sent.size(), 1u);
    OBL_CHECK(f.sent[0].delayed);
    OBL_CHECK_EQ(f.sent[0].type, 98);
    OBL_CHECK_EQ(f.sent[0].sender, 7);
    OBL_CHECK_EQ(f.sent[0].receiver, 7);
    OBL_CHECK_EQ(f.sent[0].data, 0x2e);
    OBL_CHECK_EQ(f.sent[0].delay, 0.0f);
    OBL_CHECK(f.Run(8));
    OBL_CHECK_EQ(f.sent.size(), 3u);
    OBL_CHECK_EQ(f.sent[1].type, 95); // the builder first
    OBL_CHECK(f.sent[2].delayed);
    OBL_CHECK_EQ(f.sent[2].delay, 11.0f);
    f.buildings[7].buildOn = 0x8; // deep sea
    dry.m_finished = 0;
    OBL_CHECK(f.Run(7));
    OBL_CHECK_EQ(f.sent.back().delay, 11.0f);
    OBL_CHECK_EQ(f.manager.m_constructionCount, -1); // two jobs, three endings: the counter is a plain int and goes negative
}

OBL_TEST_CASE("construction.cost_point_reads_the_builder_count_and_the_job_kind_after_the_messages")
{
    // A builder's script answers MT_CONSTRUCTION_FINISHED: here it turns the job into a demolition, adds a builder entry for the same job (it gets
    // the message too: the count is read every time round) and the end then follows the demolition branch.
    Fixture f;
    Job& job = f.AddJob(7);
    job.m_constructionRemaining = 0.0f;
    f.buildings[7].buildOn = 0x4;
    f.AddBuilder(11, 7);
    bool once = false;
    f.onSend = [&](const Sent& message) {
        if (message.type != 95 || once) return;
        once = true;
        job.m_underConstruction = 0;
        f.AddBuilder(12, 7);
    };
    OBL_CHECK(f.Run(7));
    OBL_CHECK_EQ(f.sent.size(), 3u);
    OBL_CHECK_EQ(f.sent[1].sender, 12);
    OBL_CHECK(f.sent[2].delayed);
    OBL_CHECK_EQ(f.sent[2].data, 0x2e);
    OBL_CHECK_EQ(f.sent[2].delay, 11.0f);
}

OBL_TEST_CASE("construction.cost_point_refuses_where_it_would_crash")
{
    Fixture f;
    Job& job = f.AddJob(7);
    f.buildings[7].hasDefinition = false;
    OBL_CHECK(!f.Run(7)); // a started job of a building without definition
    OBL_CHECK_EQ(job.m_constructionRemaining, 1000.0f);
    f.buildings[7].hasDefinition = true;
    job.m_constructionRemaining = 0.0f;
    f.manager.m_builderCount = 51;
    OBL_CHECK(!f.Run(7)); // a builder count that runs over the table
    f.manager.m_builderCount = -1;
    job.m_finished = 0;
    OBL_CHECK(!f.Run(7));
    OBL_CHECK(f.sent.empty());
}
