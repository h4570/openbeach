#include "unit_test.h"
#include "game/events/event_lifecycle.h"
#include <map>
#include <string>
#include <vector>

namespace {
// Hands the field table fixed values, like the obl.save reader does: an active list of `active` copies of index 0, the day offset 5.
struct Feed {
    int active = 0;
    void Int(const char* name, int32_t& field) { field = std::string(name) == "current_day_offset" ? 5 : 1; }
    void Active(const char*, const char*, std::vector<int>& list) { list.assign(static_cast<size_t>(active), 0); }
};

// The world-bound part of the lifecycle: the global, the beer availability the load sets and the schedule the save does not write.
struct Env {
    obl::disaster::Manager state{};
    bool global = false;
    std::map<int, bool> availability;
    auto& State() { return state; }
    auto& Event(size_t i) { return state.schedule[i]; }
    void SetGlobal(bool value) { global = value; }
    void Available(int beer, bool value) { availability[beer] = value; }
};

obl::disaster::Event Make(int type, uint32_t day, int beer)
{
    obl::disaster::Event event{};
    event.type = type;
    event.day = day;
    event.beerType = beer;
    return event;
}
} // namespace

OBL_TEST_CASE("event_manager.lifecycle construct sets the global and zeroes the counters")
{
    using namespace obl::disaster;
    Env env;
    env.state.nextEvent = 3;
    env.state.dayOffset = 2;
    env.state.curDayOffset = 9;
    Construct(env);
    OBL_CHECK(env.global);
    OBL_CHECK_EQ(env.state.nextEvent, 0);
    OBL_CHECK_EQ(env.state.numEvents, 0);
    OBL_CHECK_EQ(env.state.numCurEvents, 0);
    OBL_CHECK_EQ(env.state.dayOffset, 0);
    OBL_CHECK_EQ(env.state.curDayOffset, 0);
    Destruct(env);
    OBL_CHECK(!env.global);
}

OBL_TEST_CASE("event_manager.lifecycle load restores active indices beer flags and shifted days")
{
    using namespace obl::disaster;
    Env target;
    target.state.schedule = {Make(2, 8, 3), Event{}};
    target.state.active = {1};
    target.availability[3] = true;
    Feed feed{40};
    LoadFields(target, feed);
    OBL_CHECK_EQ(target.state.active.size(), size_t(40));
    OBL_CHECK(!target.availability[3]);
    OBL_CHECK_EQ(target.state.numEvents, 1);
    OBL_CHECK_EQ(target.state.schedule[0].day, 13u); // only the first numEvents entries shift
    OBL_CHECK_EQ(target.state.schedule[1].day, 0u);
    feed.active = 0;
    LoadFields(target, feed);
    OBL_CHECK(target.state.active.empty());
    OBL_CHECK_EQ(target.state.schedule[0].day, 18u);
}
