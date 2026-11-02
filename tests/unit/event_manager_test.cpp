#include "unit_test.h"
#include "game/events/event_logic.h"
#include <string>
#include <vector>
#include <limits>

namespace {
struct Env {
    obl::disaster::Manager state{};
    std::vector<std::string> effects;
    uint32_t day = 0;
    float hour = 10;
    int draws = 0, beer = 2;
    auto& State() { return state; }
    auto& Event(size_t i) { return state.schedule[i]; }
    int Active(size_t i) { return state.active[i]; }
    void Insert(size_t i, const obl::disaster::Event& e) { state.schedule.insert(state.schedule.begin() + i, e); }
    void AddActive(int i) { state.active.push_back(i); }
    void RemoveActive(size_t i)
    {
        state.active[i] = state.active.back();
        state.active.pop_back();
    }
    uint32_t Day() { return day; }
    float Hour() { return hour; }
    int Rand() { return ++draws; }
    void InvalidInsertion() { throw 1; }
    size_t BuildingCount() { return 1; }
    size_t SharkCount() { return 2; }
    int BuildingBeer(size_t) { return beer; }
    void Beer(size_t, int b)
    {
        beer = b;
        effects.push_back("beer");
    }
    void Strength(size_t, float) { effects.push_back("strength"); }
    void Water(float f) { effects.push_back(f == 0 ? "clear-water" : "water"); }
    void Available(int, bool b) { effects.push_back(b ? "available" : "unavailable"); }
    void BeerMessage(int, bool b) { effects.push_back(b ? "restored" : "shortage"); }
    void Thunder(int n) { effects.push_back(n ? "thunder" : "clear-thunder"); }
    void Area(const int*) { effects.push_back("area"); }
    void Target(const char*) { effects.push_back("target"); }
    void Finished(size_t, int i) { effects.push_back("finish:" + std::to_string(i)); }
    void Shark(int, const int*, float x, float y)
    {
        effects.push_back("shark:" + std::to_string(static_cast<int>(x)) + ":" + std::to_string(static_cast<int>(y)));
    }
    void Add(int type, float time = 10, float duration = 1, uint32_t eventDay = 0)
    {
        obl::disaster::Event e{};
        e.type = type;
        e.time = time;
        e.duration = duration;
        e.day = eventDay;
        e.disasterValue = 0.6f;
        e.scale = 2;
        e.rect[0] = 10;
        e.rect[1] = 20;
        e.rect[2] = 20;
        e.rect[3] = 30;
        e.beerType = 2;
        e.replacementBeer = 3;
        e.replacementStrength = 1;
        e.target = "POWERGEN";
        obl::disaster::AddEvent(*this, e);
    }
};
} // namespace
OBL_TEST_CASE("event_manager.stable_append_and_middle_insertion_failure")
{
    Env env;
    env.Add(1, 10, 1, 1);
    env.Add(2, 10, 1, 1);
    env.Add(3, 12, 1, 2);
    env.Add(4, 9, 1, UINT32_MAX);
    OBL_CHECK_EQ(env.state.schedule[0].type, 1);
    OBL_CHECK_EQ(env.state.schedule[1].type, 2);
    OBL_CHECK_EQ(env.state.schedule[2].type, 3);
    OBL_CHECK_EQ(env.state.schedule[3].type, 4);
    OBL_CHECK_EQ(env.state.numEvents, 4);
    bool failed = false;
    try {
        env.Add(0, 9);
    } catch (int) {
        failed = true;
    }
    OBL_CHECK(failed);
    OBL_CHECK_EQ(env.state.schedule.size(), 4u);
}
OBL_TEST_CASE("event_manager.strict_start_end_and_midnight_carry")
{
    Env env;
    env.Add(1, 10, 25);
    obl::disaster::Update(env);
    OBL_CHECK_EQ(env.state.nextEvent, 0);
    env.hour = 10.01f;
    obl::disaster::Update(env);
    OBL_CHECK_EQ(env.state.active.size(), 1u);
    OBL_CHECK_EQ(env.effects.front(), "water");
    env.day = 1;
    env.hour = 11;
    obl::disaster::Update(env);
    OBL_CHECK_EQ(env.state.active.size(), 1u);
    env.hour = 11.01f;
    obl::disaster::Update(env);
    OBL_CHECK_EQ(env.state.active.size(), 0u);
    OBL_CHECK_EQ(env.effects.back(), "clear-water");
}
OBL_TEST_CASE("event_manager.swap_removal_and_repeat_after_all_finished")
{
    Env env;
    env.Add(1);
    env.Add(3);
    env.Add(1);
    env.state.dayOffset = 2;
    env.hour = 10.1f;
    obl::disaster::Update(env);
    OBL_CHECK_EQ(env.state.active.size(), 3u);
    env.effects.clear();
    env.hour = 11.1f;
    obl::disaster::Update(env);
    OBL_CHECK_EQ(env.effects.size(), 3u);
    OBL_CHECK_EQ(env.effects[0], "clear-water");
    OBL_CHECK_EQ(env.effects[1], "clear-water");
    OBL_CHECK_EQ(env.effects[2], "clear-thunder");
    OBL_CHECK_EQ(env.state.nextEvent, 0);
    OBL_CHECK_EQ(env.state.curDayOffset, 2);
    OBL_CHECK_EQ(env.state.schedule[0].day, 2u);
}
OBL_TEST_CASE("event_manager.effects_rng_and_non_active_strike")
{
    Env env;
    env.Add(0);
    env.Add(2);
    env.Add(3);
    env.Add(4);
    env.hour = 10.1f;
    obl::disaster::Update(env);
    OBL_CHECK_EQ(env.draws, 4);
    OBL_CHECK_EQ(env.effects[0], "shark:11:22");
    OBL_CHECK_EQ(env.effects[1], "shark:13:24");
    OBL_CHECK_EQ(env.effects[2], "beer");
    OBL_CHECK_EQ(env.effects[3], "strength");
    OBL_CHECK_EQ(env.effects[4], "unavailable");
    OBL_CHECK_EQ(env.effects[5], "shortage");
    OBL_CHECK_EQ(env.effects.back(), "target");
    OBL_CHECK_EQ(env.state.active.size(), 3u);
    env.effects.clear();
    env.hour = 11.1f;
    obl::disaster::Update(env);
    OBL_CHECK_EQ(env.effects[0], "finish:0");
    OBL_CHECK_EQ(env.effects[1], "finish:0");
    OBL_CHECK_EQ(env.effects[2], "clear-thunder");
    OBL_CHECK_EQ(env.effects[3], "available");
    OBL_CHECK_EQ(env.effects[4], "restored");
}
OBL_TEST_CASE("event_manager.unordered_start_and_end")
{
    Env env;
    env.Add(1);
    env.hour = std::numeric_limits<float>::quiet_NaN();
    obl::disaster::Update(env);
    OBL_CHECK(env.state.active.empty());
    env.hour = 10.1f;
    obl::disaster::Update(env);
    OBL_CHECK_EQ(env.state.active.size(), 1u);
    env.state.schedule[0].duration = std::numeric_limits<float>::quiet_NaN();
    obl::disaster::Update(env);
    OBL_CHECK(env.state.active.empty());
    OBL_CHECK_EQ(env.effects.back(), "clear-water");
}
