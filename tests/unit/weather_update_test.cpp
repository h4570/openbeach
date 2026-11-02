#include "game/weather/weather_state.h"
#include "unit_test.h"
#include "game/messages/message_state.h"

#include "game/weather/weather_update.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::weather;
using weather::Weather;
using weather::WeatherEntry;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

WeatherEntry Entry(int kind, int day, float hour, float softDrinks = 0.0f)
{
    WeatherEntry entry{};
    entry.m_kind = kind;
    entry.m_day = day;
    entry.m_hour = hour;
    entry.m_desireModifiers[9] = softDrinks;
    return entry;
}

std::unique_ptr<Weather> NewWeather()
{
    auto weather = std::make_unique<Weather>();
    weather->m_currentState = 1; // Normal
    weather->m_rainChannel = -1;
    return weather;
}

// Records every call as text, in order.
struct FakeEnv {
    using Message = obl::messages::Message;
    std::vector<WeatherEntry> entries;
    std::vector<int> guests;
    std::vector<std::string> calls;
    int nextChannel = 40;
    uint32_t guestListRequests = 0;

    WeatherEntry& Entry(uint32_t i) { return entries[i]; }
    uint32_t GuestCount()
    {
        ++guestListRequests;
        return static_cast<uint32_t>(guests.size());
    }
    int GuestId(uint32_t i) { return guests[i]; }
    void ApplyModifiers(uint32_t i) { calls.push_back("apply " + std::to_string(guests[i])); }
    void Post(obl::messages::Message& message)
    {
        calls.push_back("post to " + std::to_string(message.receiver) + " type " + std::to_string(message.type) + " from " +
                std::to_string(message.sender) + " flags " + std::to_string(message.flags) + " data " + std::to_string(message.data));
    }
    void UpdateLighting() { calls.push_back("lighting"); }
    void FadeOutRain(int channel) { calls.push_back("fade " + std::to_string(channel)); }
    int PlayRainLoop(const char* name)
    {
        calls.push_back(std::string("play ") + name);
        return nextChannel++;
    }
    void SetRainVolume(int channel) { calls.push_back("volume " + std::to_string(channel)); }
};

std::vector<std::string> Step(Weather& weather, FakeEnv& env, uint32_t day, float hour)
{
    env.calls.clear();
    weather.m_eventCount = static_cast<int>(env.entries.size());
    UpdateSchedule(weather, day, hour, env);
    return env.calls;
}

} // namespace

OBL_TEST_CASE("weather.entry_due_compares_the_day_unsigned_and_the_hour_like_fcomp")
{
    OBL_CHECK(EntryDue(Entry(1, 3, 20.0f), 4, 0.0f)); // an earlier day is due whatever the hour
    OBL_CHECK(!EntryDue(Entry(1, 5, 0.0f), 4, 23.0f)); // a later day never is
    OBL_CHECK(EntryDue(Entry(1, 4, 5.0f), 4, 5.5f)); // same day, entry hour below the world hour
    OBL_CHECK(!EntryDue(Entry(1, 4, 5.0f), 4, 5.0f)); // equal hour: not yet
    OBL_CHECK(!EntryDue(Entry(1, 4, 6.0f), 4, 5.0f));
    OBL_CHECK(!EntryDue(Entry(1, -2, 0.0f), 4, 0.0f)); // 0xfffffffe is a far future day, not a negative one
    OBL_CHECK(EntryDue(Entry(1, 4, 0.0f), 0xffffffffu, 0.0f)); // the world day 0xffffffff is later than everything
    OBL_CHECK(EntryDue(Entry(1, 4, kNaN), 4, 5.0f)); // a NaN gives the "below" outcome
    OBL_CHECK(EntryDue(Entry(1, 4, 5.0f), 4, kNaN));
}

OBL_TEST_CASE("weather.activation_moves_the_state_and_copies_the_modifiers")
{
    auto weather = NewWeather();
    weather->m_currentState = 2;
    weather->m_weatherTime = 1.0f;
    FakeEnv env;
    env.entries = {Entry(0, 7, 11.5f, 0.75f)};
    Step(*weather, env, 7, 12.0f);
    OBL_CHECK_EQ(weather->m_previousState, 2u);
    OBL_CHECK_EQ(weather->m_currentState, 0u);
    OBL_CHECK_EQ(weather->m_weatherTime, 11.5f);
    OBL_CHECK_EQ(weather->m_desireModifiers[9], 0.75f);
    OBL_CHECK_EQ(weather->m_desireModifiers[8], 0.0f);
    OBL_CHECK_EQ(weather->m_nextEvent, 1);
}

OBL_TEST_CASE("weather.every_guest_gets_the_modifiers_and_bad_weather_only_from_light_rain_up")
{
    auto weather = NewWeather();
    FakeEnv env;
    env.guests = {11, 12};
    env.entries = {Entry(2, 1, 0.0f)};
    OBL_CHECK((Step(*weather, env, 1, 1.0f) == std::vector<std::string>{"apply 11", "apply 12", "lighting"}));
    OBL_CHECK_EQ(env.guestListRequests, 1u);

    auto rain = NewWeather();
    FakeEnv wet;
    wet.guests = {11, 12};
    wet.entries = {Entry(3, 1, 0.0f)};
    const std::vector<std::string> expected = {"apply 11", "post to 11 type 72 from -2 flags 0 data 0", "apply 12",
            "post to 12 type 72 from -2 flags 0 data 0", "lighting", "play rain_loop"};
    OBL_CHECK(Step(*rain, wet, 1, 1.0f) == expected);
}

OBL_TEST_CASE("weather.entries_come_due_in_order_and_the_first_that_is_not_stops_the_loop")
{
    auto weather = NewWeather();
    FakeEnv env;
    env.guests = {5};
    env.entries = {Entry(4, 2, 1.0f), Entry(0, 2, 3.0f), Entry(1, 2, 9.0f)};
    const std::vector<std::string> calls = Step(*weather, env, 2, 4.0f);
    OBL_CHECK_EQ(weather->m_nextEvent, 2);
    OBL_CHECK_EQ(weather->m_currentState, 0u);
    OBL_CHECK_EQ(weather->m_previousState, 4u);
    OBL_CHECK_EQ(env.guestListRequests, 2u); // the list is asked for once per activated entry
    size_t lightingCalls = 0;
    for (const std::string& call : calls) lightingCalls += call == "lighting";
    OBL_CHECK_EQ(lightingCalls, static_cast<size_t>(1)); // once per step, after the loop
    // nothing more is due on the next call
    OBL_CHECK((Step(*weather, env, 2, 4.0f) == std::vector<std::string>{"lighting"}));
}

OBL_TEST_CASE("weather.lighting_runs_with_no_entries_at_all")
{
    auto weather = NewWeather();
    FakeEnv env;
    OBL_CHECK((Step(*weather, env, 9, 9.0f) == std::vector<std::string>{"lighting"}));
}

OBL_TEST_CASE("weather.an_exhausted_schedule_starts_over_shifted_by_the_day_offset")
{
    auto weather = NewWeather();
    FakeEnv env;
    env.entries = {Entry(1, 3, 2.0f), Entry(1, 4, 2.0f)};
    weather->m_dayOffset = 5;
    weather->m_currentDayOffset = 10;
    Step(*weather, env, 3, 3.0f); // only the first is due: nothing restarts
    OBL_CHECK_EQ(weather->m_nextEvent, 1);
    OBL_CHECK_EQ(env.entries[0].m_day, 3);
    Step(*weather, env, 4, 3.0f); // the second one runs the schedule out
    OBL_CHECK_EQ(weather->m_nextEvent, 0);
    OBL_CHECK_EQ(weather->m_currentDayOffset, 15);
    OBL_CHECK_EQ(env.entries[0].m_day, 8);
    OBL_CHECK_EQ(env.entries[1].m_day, 9);
    // the shifted entries are due again five days later
    Step(*weather, env, 8, 3.0f);
    OBL_CHECK_EQ(weather->m_nextEvent, 1);
}

OBL_TEST_CASE("weather.without_a_day_offset_an_exhausted_schedule_stays_exhausted")
{
    auto weather = NewWeather();
    FakeEnv env;
    env.entries = {Entry(1, 3, 2.0f)};
    Step(*weather, env, 3, 3.0f);
    OBL_CHECK_EQ(weather->m_nextEvent, 1);
    OBL_CHECK_EQ(weather->m_currentDayOffset, 0);
    OBL_CHECK_EQ(env.entries[0].m_day, 3);
}

OBL_TEST_CASE("weather.rain_loop_follows_the_weather_kind")
{
    auto weather = NewWeather();
    FakeEnv env;
    auto kind = [&](int next) {
        env.entries = {Entry(next, 1, 0.0f)};
        weather->m_nextEvent = 0;
        weather->m_eventCount = 1;
        env.guests.clear();
        return Step(*weather, env, 1, 1.0f);
    };
    // Normal -> light rain: no channel yet (-1), so no fade, then the light loop
    OBL_CHECK((kind(3) == std::vector<std::string>{"lighting", "play rain_loop"}));
    OBL_CHECK_EQ(weather->m_rainKind, 2);
    OBL_CHECK_EQ(weather->m_rainChannel, 40);
    // light rain again: only the volume is set
    OBL_CHECK((kind(3) == std::vector<std::string>{"lighting", "volume 40"}));
    // heavy rain: the light loop fades out, the heavy one starts
    OBL_CHECK((kind(4) == std::vector<std::string>{"lighting", "fade 40", "play rain_heavy"}));
    OBL_CHECK_EQ(weather->m_rainKind, 3);
    OBL_CHECK_EQ(weather->m_rainChannel, 41);
    // a storm keeps the heavy loop
    OBL_CHECK((kind(5) == std::vector<std::string>{"lighting", "volume 41"}));
    // clear sky: fade out, kind 0, the channel number stays
    OBL_CHECK((kind(1) == std::vector<std::string>{"lighting", "fade 41"}));
    OBL_CHECK_EQ(weather->m_rainKind, 0);
    OBL_CHECK_EQ(weather->m_rainChannel, 41);
    // and nothing happens while it stays dry
    OBL_CHECK((kind(0) == std::vector<std::string>{"lighting"}));
    OBL_CHECK((kind(2) == std::vector<std::string>{"lighting"}));
    // a kind outside 0..5 counts as dry as well
    OBL_CHECK((kind(3) == std::vector<std::string>{"lighting", "fade 41", "play rain_loop"}));
    OBL_CHECK((kind(9) == std::vector<std::string>{"lighting", "fade 42"}));
    OBL_CHECK_EQ(weather->m_rainKind, 0);
    OBL_CHECK((kind(-1) == std::vector<std::string>{"lighting"}));
}

OBL_TEST_CASE("weather.a_loop_that_is_not_running_is_not_faded")
{
    auto weather = NewWeather();
    weather->m_rainKind = 2;
    weather->m_rainChannel = -1; // the loop never got a channel
    FakeEnv env;
    env.entries = {Entry(1, 1, 0.0f)};
    OBL_CHECK((Step(*weather, env, 1, 1.0f) == std::vector<std::string>{"lighting"}));
    OBL_CHECK_EQ(weather->m_rainKind, 0);
}

OBL_TEST_CASE("weather.insert_goes_before_the_first_element_that_does_not_sort_before_it")
{
    FakeEnv env;
    env.entries = {Entry(1, 3, 5.0f), Entry(1, 3, 9.0f), Entry(1, 4, 1.0f)};
    OBL_CHECK_EQ(InsertPosition(Entry(0, 2, 20.0f), 3, env), 0u); // an earlier day
    OBL_CHECK_EQ(InsertPosition(Entry(0, 3, 1.0f), 3, env), 0u); // same day, earlier hour
    OBL_CHECK_EQ(InsertPosition(Entry(0, 3, 5.0f), 3, env), 0u); // an equal time goes in front of the old one
    OBL_CHECK_EQ(InsertPosition(Entry(0, 3, 7.0f), 3, env), 1u);
    OBL_CHECK_EQ(InsertPosition(Entry(0, 3, 9.0f), 3, env), 1u);
    OBL_CHECK_EQ(InsertPosition(Entry(0, 3, 9.5f), 3, env), 2u);
    OBL_CHECK_EQ(InsertPosition(Entry(0, 4, 1.0f), 3, env), 2u);
    OBL_CHECK_EQ(InsertPosition(Entry(0, 4, 1.5f), 3, env), 3u); // after everything
    OBL_CHECK_EQ(InsertPosition(Entry(0, 3, 5.0f), 0, env), 0u); // an empty deque
}

OBL_TEST_CASE("weather.insert_orders_days_as_unsigned_numbers_and_nan_hours_as_earlier")
{
    FakeEnv env;
    env.entries = {Entry(1, 3, 5.0f), Entry(1, -2, 0.0f)}; // day 0xfffffffe is last
    OBL_CHECK_EQ(InsertPosition(Entry(0, 100, 0.0f), 2, env), 1u);
    OBL_CHECK_EQ(InsertPosition(Entry(0, -1, 0.0f), 2, env), 2u);
    OBL_CHECK_EQ(InsertPosition(Entry(0, 3, kNaN), 2, env), 1u); // an unordered compare keeps walking
    env.entries = {Entry(1, 3, kNaN), Entry(1, 3, 6.0f)};
    OBL_CHECK_EQ(InsertPosition(Entry(0, 3, 1.0f), 2, env), 1u); // a NaN element counts as earlier, 6.0 is not
}

namespace {

struct InsertEnv {
    using Message = obl::messages::Message;
    std::vector<WeatherEntry> entries;
    std::vector<std::string> calls;
    WeatherEntry heapRecord{};

    uint32_t Count() { return static_cast<uint32_t>(entries.size()); }
    WeatherEntry& Entry(uint32_t i) { return entries[i]; }
    WeatherEntry* NewEntry(const WeatherEntry& entry)
    {
        calls.push_back("new");
        heapRecord = entry;
        return &heapRecord;
    }
    void InsertAt(uint32_t position, WeatherEntry* stored)
    {
        calls.push_back("insert at " + std::to_string(position));
        entries.insert(entries.begin() + position, *stored);
    }
};

} // namespace

OBL_TEST_CASE("weather.insert_entry_allocates_first_inserts_and_counts")
{
    auto weather = NewWeather();
    InsertEnv env;
    env.entries = {Entry(1, 3, 5.0f)};
    weather->m_eventCount = 1;
    InsertEntry(*weather, Entry(4, 3, 2.0f, 0.5f), env);
    OBL_CHECK((env.calls == std::vector<std::string>{"new", "insert at 0"}));
    OBL_CHECK_EQ(weather->m_eventCount, 2);
    OBL_CHECK_EQ(env.entries[0].m_kind, 4);
    OBL_CHECK_EQ(env.entries[0].m_desireModifiers[9], 0.5f);
    OBL_CHECK_EQ(weather->m_nextEvent, 0); // the index of the next entry is not adjusted
}

OBL_TEST_CASE("weather.insert_into_an_empty_schedule")
{
    auto weather = NewWeather();
    InsertEnv env;
    InsertEntry(*weather, Entry(2, 0, 0.0f), env);
    OBL_CHECK((env.calls == std::vector<std::string>{"new", "insert at 0"}));
    OBL_CHECK_EQ(weather->m_eventCount, 1);
}

OBL_TEST_CASE("weather.rain_drops_draw_y_first_then_x_and_reduce_unsigned")
{
    auto drops = std::make_unique<weather::RainDropBuffer>();
    std::memset(drops.get(), 0xee, sizeof(weather::RainDropBuffer));
    std::vector<int> draws;
    int counter = 0;
    RandomizeRainDrops(*drops, 800, 600, [&] {
        draws.push_back(counter);
        return counter++ * 997;
    });
    OBL_CHECK_EQ(draws.size(), static_cast<size_t>(2 * weather::kRainDropCount));
    // drop 0 takes draw 0 for y (mod 600) and draw 1 for x (mod 800)
    OBL_CHECK_EQ(drops->m_xy[0][1], 0u);
    OBL_CHECK_EQ(drops->m_xy[0][0], 997u % 800u);
    OBL_CHECK_EQ(drops->m_xy[1][1], (2u * 997u) % 600u);
    OBL_CHECK_EQ(drops->m_xy[1][0], (3u * 997u) % 800u);
    const uint32_t last = 2 * weather::kRainDropCount - 2;
    OBL_CHECK_EQ(drops->m_xy[weather::kRainDropCount - 1][1], (last * 997u) % 600u);
    OBL_CHECK_EQ(drops->m_xy[weather::kRainDropCount - 1][0], ((last + 1) * 997u) % 800u);
}

OBL_TEST_CASE("weather.rain_drops_in_a_one_pixel_view_are_all_zero")
{
    auto drops = std::make_unique<weather::RainDropBuffer>();
    std::memset(drops.get(), 0xee, sizeof(weather::RainDropBuffer));
    RandomizeRainDrops(*drops, 1, 1, [] { return 123456; });
    OBL_CHECK_EQ(drops->m_xy[0][0], 0u);
    OBL_CHECK_EQ(drops->m_xy[weather::kRainDropCount - 1][1], 0u);
}

OBL_TEST_CASE("weather.rain_drops_leave_an_empty_view_alone")
{
    auto drops = std::make_unique<weather::RainDropBuffer>();
    std::memset(drops.get(), 0xee, sizeof(weather::RainDropBuffer));
    int draws = 0;
    RandomizeRainDrops(*drops, 0, 600, [&] { return ++draws; });
    RandomizeRainDrops(*drops, 800, 0, [&] { return ++draws; });
    OBL_CHECK_EQ(draws, 0);
    OBL_CHECK_EQ(drops->m_xy[5][0], 0xeeeeeeeeu);
}
