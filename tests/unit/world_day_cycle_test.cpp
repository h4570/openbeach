#include "unit_test.h"
#include "game/messages/message_state.h"
#include "game/world/world_state.h"

#include "game/world/world_day_cycle.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::day_cycle;

namespace {

struct FakeEnv {
    std::vector<std::string> calls;
    layout::World* world = nullptr;
    int arriveAtRoll = -1;

    void PostToBuildings(obl::messages::Message& message) { calls.push_back("buildings " + Describe(message)); }
    void PostToClass(int cls, obl::messages::Message& message) { calls.push_back("class" + std::to_string(cls) + " " + Describe(message)); }
    void PostToStaff(obl::messages::Message& message) { calls.push_back("staff " + Describe(message)); }
    void ResetFerryCursor() { calls.push_back("ferry"); }
    void RollDailyStats()
    {
        calls.push_back("roll");
        if (world) arriveAtRoll = world->m_todayGuestArrive[0] + world->m_todayGuestUnhappyDepart[1];
    }
    void SetNight(bool night) { calls.push_back(night ? "night" : "day"); }

    static std::string Describe(const obl::messages::Message& message)
    {
        return "type=" + std::to_string(message.type) + " sender=" + std::to_string(message.sender) +
                " data=" + std::to_string(message.data) + " flags=" + std::to_string(message.flags);
    }
};

std::unique_ptr<layout::World> NewWorld(float previous, float current, int day = 5)
{
    auto world = std::make_unique<layout::World>();
    world->m_lastGameDayTime = previous;
    world->m_gameDayTime = current;
    world->m_gameDay = day;
    return world;
}

std::vector<std::string> Run(layout::World& world)
{
    FakeEnv env;
    env.world = &world;
    UpdateDayCycle(world, env);
    return env.calls;
}

} // namespace

OBL_TEST_CASE("world_day_cycle.same_hour_does_nothing_but_store_the_hour")
{
    auto world = NewWorld(10.2f, 10.7f);
    OBL_CHECK(Run(*world).empty());
    OBL_CHECK_EQ(world->m_lastGameDayTime, 10.7f);
}

OBL_TEST_CASE("world_day_cycle.new_hour_goes_to_personnel_only")
{
    auto world = NewWorld(10.9f, 11.1f, 7);
    const std::vector<std::string> calls = Run(*world);
    OBL_REQUIRE(calls.size() == 1);
    OBL_CHECK_EQ(calls[0], std::string("staff type=85 sender=-2 data=7 flags=0"));
    OBL_CHECK_EQ(world->m_lastGameDayTime, 11.1f);
}

OBL_TEST_CASE("world_day_cycle.midnight_posts_new_day_then_new_hour_in_order")
{
    auto world = NewWorld(23.99f, 0.01f, 3);
    world->m_todayGuestArrive[0] = 4;
    world->m_todayGuestArrive[1] = 5;
    world->m_todayGuestDepart[1] = 6;
    world->m_todayGuestUnhappyDepart[0] = 7;
    world->m_todayGuestUnhappyDepart[1] = 8;
    world->m_totalGuestArrive[0] = 99; // totals stay
    const std::vector<std::string> calls = Run(*world);
    const std::vector<std::string> expected = {"buildings type=38 sender=-2 data=3 flags=0", "class6 type=38 sender=-2 data=3 flags=0",
            "class7 type=38 sender=-2 data=3 flags=0", "class8 type=38 sender=-2 data=3 flags=0", "class9 type=38 sender=-2 data=3 flags=0",
            "class10 type=38 sender=-2 data=3 flags=0", "class11 type=38 sender=-2 data=3 flags=0",
            "class12 type=38 sender=-2 data=3 flags=0", "class3 type=38 sender=-2 data=3 flags=0", "ferry", "roll",
            "staff type=85 sender=-2 data=3 flags=0"};
    OBL_CHECK(calls == expected);
    // the "today" counters are zero by the time the statistics roll
    OBL_CHECK_EQ(world->m_todayGuestArrive[0], 0);
    OBL_CHECK_EQ(world->m_todayGuestArrive[1], 0);
    OBL_CHECK_EQ(world->m_todayGuestDepart[0], 0);
    OBL_CHECK_EQ(world->m_todayGuestDepart[1], 0);
    OBL_CHECK_EQ(world->m_todayGuestUnhappyDepart[0], 0);
    OBL_CHECK_EQ(world->m_todayGuestUnhappyDepart[1], 0);
    OBL_CHECK_EQ(world->m_totalGuestArrive[0], 99);
}

OBL_TEST_CASE("world_day_cycle.counters_are_zero_before_the_roll_and_the_roll_runs_once")
{
    auto world = NewWorld(23.5f, 0.2f);
    world->m_todayGuestArrive[0] = 3;
    world->m_todayGuestUnhappyDepart[1] = 9;
    FakeEnv env;
    env.world = world.get();
    UpdateDayCycle(*world, env);
    OBL_CHECK_EQ(env.arriveAtRoll, 0);
    int rolls = 0;
    for (const std::string& call : env.calls) rolls += call == "roll";
    OBL_CHECK_EQ(rolls, 1);
}

OBL_TEST_CASE("world_day_cycle.hour_is_truncated_toward_zero_like_truncation")
{
    OBL_CHECK_EQ(Truncate(23.99f), 23);
    OBL_CHECK_EQ(Truncate(-0.5f), 0);
    OBL_CHECK_EQ(Truncate(std::numeric_limits<float>::quiet_NaN()), INT32_MIN);
    OBL_CHECK_EQ(Truncate(3e9f), INT32_MIN);
    OBL_CHECK_EQ(Truncate(-2147483648.0f), INT32_MIN);
    auto world = NewWorld(12.9f, 12.1f);
    OBL_CHECK(Run(*world).empty()); // the integer hour is the same: nothing happens even though the float went back
    auto backwards = NewWorld(13.0f, 12.5f);
    const std::vector<std::string> calls = Run(*backwards);
    // a lower integer hour reads as a day change: buildings + 8 classes, ferry, roll, then the hour message
    OBL_CHECK_EQ(calls.size(), static_cast<size_t>(12));
}

OBL_TEST_CASE("world_day_cycle.music_switches_at_nineteen_and_seven")
{
    auto evening = NewWorld(18.99f, 19.01f);
    OBL_CHECK(Run(*evening).back() == "night");
    auto morning = NewWorld(6.99f, 7.01f);
    OBL_CHECK(Run(*morning).back() == "day");
    auto exact = NewWorld(18.5f, 19.0f);
    OBL_CHECK(Run(*exact).back() == "night");
    auto already = NewWorld(19.2f, 19.8f);
    for (const std::string& call : Run(*already)) OBL_CHECK(call != "night" && call != "day");
    auto midday = NewWorld(11.0f, 11.5f);
    OBL_CHECK(Run(*midday).empty());
}

OBL_TEST_CASE("world_day_cycle.a_big_jump_still_switches_once")
{
    // 5:00 -> 20:00 (set-hour): only the 19:00 test fires; the 7:00 one is the `else` of it.
    auto world = NewWorld(5.0f, 20.0f);
    const std::vector<std::string> calls = Run(*world);
    int switches = 0;
    for (const std::string& call : calls) switches += call == "night" || call == "day";
    OBL_CHECK_EQ(switches, 1);
    OBL_CHECK(calls.back() == "night");
    // 5:00 -> 10:00 reaches 7:00 only.
    auto morning = NewWorld(5.0f, 10.0f);
    OBL_CHECK(Run(*morning).back() == "day");
}

OBL_TEST_CASE("world_day_cycle.nan_hours_follow_the_fpu_flags")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // previous NaN: "below" both limits; the current hour decides.
    auto world = NewWorld(nan, 20.0f);
    const std::vector<std::string> calls = Run(*world);
    OBL_CHECK(calls.back() == "night");
    // current NaN counts as below: no switch.
    auto current = NewWorld(5.0f, nan);
    for (const std::string& call : Run(*current)) OBL_CHECK(call != "night" && call != "day");
}
