// RepairManager field table, RemoveJob and CountUrgentJobs (src/game/repair/repair_lifecycle.h) on the native manager.

#include "unit_test.h"

#include "game/repair/repair_lifecycle.h"

#include <cstdint>
#include <vector>

using namespace obl::repair;

namespace {

Manager Sample()
{
    Manager m;
    m.buildingJobs = {{11, 0, 2.0f, 12.5f, 0, 0}, {12, 3, 4.0f, 40.0f, 1, 2}};
    m.sceneryJobs = {{31, 1, 8.0f, 41.25f, 0, 1}};
    m.activeJobs = {{21, 1, 0, 2}, {22, 0, 1, 1}};
    m.breakdownCount = 7;
    m.vandalismCount = 4;
    m.activeCount = 2;
    return m;
}

// Counts what a visitor sees, to pin that the snapshot-style consumers can skip the scratch distance.
struct Census {
    int lists = 0, ints = 0, uints = 0, bools = 0, floats = 0, scratch = 0;
    template<class Seq, class Callable> void List(const char*, const Seq& list, Callable callable)
    {
        ++lists;
        for (const auto& element : list) callable(*this, element);
    }
    void Int(const char*, int32_t) { ++ints; }
    void UInt(const char*, uint32_t) { ++uints; }
    void Bool(const char*, uint8_t) { ++bools; }
    void Float(const char*, float) { ++floats; }
    void Scratch(const char*, float) { ++scratch; }
};

} // namespace

OBL_TEST_CASE("repair_lifecycle.a_new_manager_has_three_empty_lists_and_zero_counters")
{
    Manager manager;
    OBL_CHECK(manager.buildingJobs.empty());
    OBL_CHECK(manager.sceneryJobs.empty());
    OBL_CHECK(manager.activeJobs.empty());
    OBL_CHECK_EQ(manager.breakdownCount, 0);
    OBL_CHECK_EQ(manager.vandalismCount, 0);
    OBL_CHECK_EQ(manager.activeCount, 0);
}

OBL_TEST_CASE("repair_lifecycle.field_table_marks_only_the_distance_as_scratch")
{
    const Manager manager = Sample();
    Census census;
    VisitFields(manager, census);
    OBL_CHECK_EQ(census.lists, 3);
    OBL_CHECK_EQ(census.scratch, 3); // one per pending job
    OBL_CHECK_EQ(census.floats, 3);
    OBL_CHECK_EQ(census.uints, 3);
    OBL_CHECK_EQ(census.ints, 3 * 2 + 2 * 2 + 3);
    OBL_CHECK_EQ(census.bools, 3 + 2 * 2);
}

OBL_TEST_CASE("repair_lifecycle.remove_job_takes_the_first_pending_request_building_list_first")
{
    Manager manager = Sample();
    manager.sceneryJobs.push_back({12, 5, 1.0f, 2.0f, 0, 1}); // the same object queued in both lists
    RemoveJob(manager, 12);
    OBL_CHECK_EQ(manager.buildingJobs.size(), size_t{1});
    OBL_CHECK_EQ(manager.buildingJobs.front().id, 11);
    OBL_CHECK_EQ(manager.sceneryJobs.size(), size_t{2}); // the scenery entry stays: the building hit ended the search
    RemoveJob(manager, 12);
    OBL_CHECK_EQ(manager.sceneryJobs.size(), size_t{1});
    OBL_CHECK_EQ(manager.sceneryJobs.front().id, 31);
    RemoveJob(manager, 21); // an active job is not a pending request
    RemoveJob(manager, 999);
    OBL_CHECK_EQ(manager.buildingJobs.size(), size_t{1});
    OBL_CHECK_EQ(manager.activeJobs.size(), size_t{2});
    OBL_CHECK_EQ(manager.breakdownCount, 7);
    OBL_CHECK_EQ(manager.vandalismCount, 4);
}

OBL_TEST_CASE("repair_lifecycle.count_urgent_jobs_is_breakdowns_plus_all_scenery_jobs")
{
    Manager manager = Sample();
    manager.buildingJobs.push_back({13, 0, 0.0f, 1.0f, 1, 0}); // urgent flag, but an inspection: not counted
    manager.buildingJobs.push_back({14, 0, 0.0f, 1.0f, 0, 2}); // a breakdown without the flag: counted
    manager.sceneryJobs.push_back({32, 0, 0.0f, 1.0f, 0, 3});
    OBL_CHECK_EQ(CountUrgentJobs(manager), 4u);
    OBL_CHECK_EQ(CountUrgentJobs(Manager{}), 0u);
}
