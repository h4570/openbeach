#include "unit_test.h"

#include "game/objects/building_staff_visibility.h"

#include <map>
#include <string>
#include <vector>

using namespace obl;

namespace {

// A fake world: object id -> class, visibility; tables -> waiter; a show with an active flag. Every call is logged in order.
struct FakeEnv {
    std::map<int32_t, int> classes;
    std::map<int32_t, bool> visible;
    std::map<int32_t, int32_t> waiterOfTable; // present only for tables whose definition has plates
    bool events = false;
    int activeEvent = -1;
    std::vector<std::string> log;

    int ClassOf(int32_t id)
    {
        auto it = classes.find(id);
        return it == classes.end() ? -1 : it->second;
    }
    void SetVisible(int32_t id, bool v)
    {
        visible[id] = v;
        log.push_back("vis " + std::to_string(id) + (v ? "=1" : "=0"));
    }
    bool WaiterOf(int32_t table, int32_t& waiter)
    {
        auto it = waiterOfTable.find(table);
        if (it == waiterOfTable.end()) return false;
        waiter = it->second;
        return true;
    }
    bool HasEvents() { return events; }
    int ActiveEventId() { return activeEvent; }
    void HideEventActors() { log.push_back("hide-actors"); }
    void CancelEvent() { log.push_back("cancel"); }
    void ShowEventActors() { log.push_back("show-actors"); }
};

struct Fixture {
    FakeEnv env;
    int32_t staff[4] = {10, 11, 12, 13};
    uint8_t busy[4] = {1, 1, 1, 1};
    int32_t nonServe[2] = {20, 21};
    int32_t tables[2] = {30, 31};
    StaffLists lists;

    Fixture()
    {
        env.classes = {{10, 6}, {11, 3}, {12, 6}, {13, 9}, {20, 14}, {21, 8}, {30, 5}, {31, 5}, {40, 6}};
        for (auto& entry : env.classes) env.visible[entry.first] = true;
        lists.serving = 3; // 13 is beyond the count
        lists.staff = staff;
        lists.busy = busy;
        lists.nonServe = 2;
        lists.nonServeIds = nonServe;
        lists.tables = 2;
        lists.tableIds = tables;
    }
};

} // namespace

OBL_TEST_CASE("building_staff_visibility.staff_classes")
{
    for (int c = 0; c < 0x20; ++c) {
        const bool expected = c == 6 || c == 0x0e || c == 0x0f || c == 0x10 || c == 0x11 || c == 0x12;
        OBL_CHECK_EQ(IsStaffClass(c), expected);
    }
    OBL_CHECK(!IsStaffClass(-1));
}

OBL_TEST_CASE("building_staff_visibility.hide_serving_staff_clears_busy_and_visibility_of_matching_classes_only")
{
    Fixture f;
    HideStaffAndEvents(f.env, f.lists);
    OBL_CHECK(!f.env.visible[10]);
    OBL_CHECK(f.env.visible[11]); // a guest class in the list: untouched
    OBL_CHECK(!f.env.visible[12]);
    OBL_CHECK(f.env.visible[13]); // beyond `serving`
    OBL_CHECK_EQ(int(f.busy[0]), 0);
    OBL_CHECK_EQ(int(f.busy[1]), 1); // the busy flag follows the class test, too
    OBL_CHECK_EQ(int(f.busy[2]), 0);
    OBL_CHECK_EQ(int(f.busy[3]), 1);
}

OBL_TEST_CASE("building_staff_visibility.non_serving_performers_follow_the_class_test_without_busy_flags")
{
    Fixture f;
    HideStaffAndEvents(f.env, f.lists);
    OBL_CHECK(!f.env.visible[20]); // Cabaret
    OBL_CHECK(f.env.visible[21]); // Mechanic
    ShowStaffAndEvents(f.env, f.lists);
    OBL_CHECK(f.env.visible[20]);
    OBL_CHECK(f.env.visible[21]);
}

OBL_TEST_CASE("building_staff_visibility.show_does_not_touch_the_busy_flags")
{
    Fixture f;
    f.busy[0] = 0;
    f.env.visible[10] = false;
    ShowStaffAndEvents(f.env, f.lists);
    OBL_CHECK(f.env.visible[10]);
    OBL_CHECK_EQ(int(f.busy[0]), 0);
    OBL_CHECK_EQ(int(f.busy[1]), 1);
}

OBL_TEST_CASE("building_staff_visibility.table_waiters_only_for_tables_with_plates")
{
    Fixture f;
    f.env.waiterOfTable[30] = 40; // table 31 has no plates
    HideStaffAndEvents(f.env, f.lists);
    OBL_CHECK(!f.env.visible[40]);
    OBL_CHECK(f.env.visible[30]); // the table itself is not hidden by this function
    ShowStaffAndEvents(f.env, f.lists);
    OBL_CHECK(f.env.visible[40]);
}

OBL_TEST_CASE("building_staff_visibility.missing_waiter_object_is_skipped")
{
    Fixture f;
    f.env.waiterOfTable[30] = 99; // not a living object
    HideStaffAndEvents(f.env, f.lists);
    OBL_CHECK(f.env.visible.count(99) == 0);
}

OBL_TEST_CASE("building_staff_visibility.hide_cancels_an_active_show_after_the_visibility_pass")
{
    Fixture f;
    f.env.events = true;
    f.env.activeEvent = 3;
    HideStaffAndEvents(f.env, f.lists);
    OBL_REQUIRE(f.env.log.size() >= 2);
    OBL_CHECK_EQ(f.env.log[f.env.log.size() - 2], std::string("hide-actors"));
    OBL_CHECK_EQ(f.env.log.back(), std::string("cancel"));
}

OBL_TEST_CASE("building_staff_visibility.hide_leaves_an_idle_or_missing_show_alone")
{
    Fixture f;
    f.env.events = true;
    f.env.activeEvent = -1;
    HideStaffAndEvents(f.env, f.lists);
    for (const std::string& line : f.env.log) OBL_CHECK(line.rfind("vis", 0) == 0);
    Fixture g;
    g.env.events = false;
    g.env.activeEvent = 3;
    HideStaffAndEvents(g.env, g.lists);
    for (const std::string& line : g.env.log) OBL_CHECK(line.rfind("vis", 0) == 0);
}

OBL_TEST_CASE("building_staff_visibility.show_shows_the_actors_of_any_show_without_asking")
{
    Fixture f;
    f.env.events = true;
    f.env.activeEvent = -1;
    ShowStaffAndEvents(f.env, f.lists);
    OBL_CHECK_EQ(f.env.log.back(), std::string("show-actors"));
    Fixture g;
    ShowStaffAndEvents(g.env, g.lists);
    for (const std::string& line : g.env.log) OBL_CHECK(line.rfind("vis", 0) == 0);
}

OBL_TEST_CASE("building_staff_visibility.empty_lists_do_nothing")
{
    Fixture f;
    f.lists.serving = 0;
    f.lists.nonServe = 0;
    f.lists.tables = 0;
    f.env.waiterOfTable[30] = 40;
    HideStaffAndEvents(f.env, f.lists);
    OBL_CHECK(f.env.log.empty());
    OBL_CHECK_EQ(int(f.busy[0]), 1);
    f.lists.tables = -3; // a negative count is "none", like the signed loop
    HideStaffAndEvents(f.env, f.lists);
    OBL_CHECK(f.env.log.empty());
}
