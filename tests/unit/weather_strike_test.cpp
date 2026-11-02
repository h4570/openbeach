#include "game/objects/object_base.h"
#include "game/weather/weather_state.h"
#include "unit_test.h"

#include "fake_strike_env.h"
#include "game/weather/weather_control.h"
#include "game/weather/weather_lighting.h"
#include "game/weather/weather_strike.h"

#include <climits>
#include <cstring>
#include <memory>
#include <vector>

using namespace obl;
using namespace obl::weather;
using object::ObjectType;
using weather::Weather;

namespace {

// The bolt Env: the bitmap sits between guard bytes, rand() comes from a function.
struct FakeBoltEnv {
    static constexpr size_t kGuard = 4096;
    std::vector<uint8_t> memory;
    uint32_t allocated = 0;
    int32_t drift = 0;
    size_t calls = 0;
    int (*source)(size_t) = [](size_t) { return 0; };

    void* NewBitmap(uint32_t bytes)
    {
        allocated = bytes;
        memory.assign(bytes + 2 * kGuard, 0xab);
        return memory.data() + kGuard;
    }
    int Rand() { return source(calls++); }
    int32_t& Drift() { return drift; }
    const uint8_t* Bitmap() const { return memory.data() + kGuard; }
    bool GuardsIntact() const
    {
        for (size_t i = 0; i < kGuard; ++i)
            if (memory[i] != 0xab || memory[memory.size() - 1 - i] != 0xab) return false;
        return true;
    }
};

std::unique_ptr<Weather> NewWeather()
{
    auto weather = std::make_unique<Weather>();
    return weather;
}

// The run of bolt bytes in one bitmap row: {first column, length}, {-1, 0} for none.
std::pair<int, int> Run(const FakeBoltEnv& env, int bitmapRow)
{
    const uint8_t* row = env.Bitmap() + bitmapRow * strike::kBoltRowBytes;
    int first = -1, length = 0;
    for (int c = 0; c < static_cast<int>(strike::kBoltRowBytes); ++c)
        if (row[c] == strike::kBoltPixel) {
            if (first < 0) first = c;
            ++length;
        }
    return {first, length};
}

void MakeObject(object::Base& object, ObjectType type, uint32_t id, int state)
{
    object.m_type = type;
    object.m_id = id;
    object.m_state = state;
}

// A storm with strikes armed at time 100, a 10 x 60 area at (100, 200).
std::unique_ptr<Weather> StormWeather()
{
    auto weather = NewWeather();
    weather->m_currentState = weather->m_previousState = 5;
    weather->m_strikes = 1;
    weather->m_nextStrikeTime = 100.0f;
    weather->m_strikeLevel = 7;
    weather->m_strikeArea = {100, 200, 110, 260};
    weather->m_strikePoint[2] = 42.0f;
    weather->m_flashColor = 0x1234;
    return weather;
}

} // namespace

OBL_TEST_CASE("weather_strike.bolt_with_rand_zero_is_two_straight_segments")
{
    auto weather = NewWeather();
    FakeBoltEnv env;
    CreateLightningBolt(*weather, 20, env);
    OBL_CHECK_EQ(env.allocated, 20u * 200u);
    OBL_CHECK(weather->m_lightningLightmap == env.Bitmap());
    OBL_CHECK(env.GuardsIntact());
    // segment 1: rows 1..10 from column 100 to the target 50 (slope -5); the drift loses 1 a row (-2 * (row/40 + 0.5) truncated) down to -15
    // row 1 is the last bitmap row: column 95, drift -1, width trunc(1 / 10 * 15) = 1
    OBL_CHECK(Run(env, 19) == std::make_pair(94, 1));
    OBL_CHECK(Run(env, 18) == std::make_pair(88, 3)); // row 2: column 90, drift -2, width 3
    // segment 2: rows 11..20 aim at 50 again (slope 0); from row 15 on the drift is clamped at -15; the top row is 30 wide
    OBL_CHECK(Run(env, 0) == std::make_pair(35, 30));
    OBL_CHECK_EQ(env.drift, -15);
    OBL_CHECK_EQ(env.calls, 24u); // two draws per segment, one per row
}

OBL_TEST_CASE("weather_strike.bolt_of_no_rows_draws_nothing")
{
    auto weather = NewWeather();
    FakeBoltEnv env;
    env.drift = 7;
    CreateLightningBolt(*weather, 0, env);
    OBL_CHECK_EQ(env.allocated, 0u);
    OBL_CHECK_EQ(env.calls, 0u);
    OBL_CHECK_EQ(env.drift, 7);
    OBL_CHECK(env.GuardsIntact());
}

OBL_TEST_CASE("weather_strike.bolt_keeps_inside_the_bitmap_and_the_drift_in_range")
{
    // a rand() that walks through the whole 0..32767 range
    for (int32_t height : {1, 2, 9, 10, 11, 59, 60, 61, 600, 768, 1080, 2160})
        for (int start : {-15, 0, 15}) {
            auto weather = NewWeather();
            FakeBoltEnv env;
            env.drift = start;
            env.source = [](size_t i) { return static_cast<int>((i * 7919u + 13u) % 32768u); };
            CreateLightningBolt(*weather, height, env);
            OBL_CHECK(env.GuardsIntact());
            OBL_CHECK(env.drift >= -15 && env.drift <= 15);
            // every row has its run, as wide as 30 * row / height (row 1 = bitmap row height - 1)
            for (int row = 1; row <= height; ++row) {
                const int width = static_cast<int>(
                        static_cast<double>(static_cast<float>(row)) / (static_cast<double>(static_cast<float>(height)) * 0.5) * 15.0);
                OBL_CHECK_EQ(Run(env, height - row).second, width);
            }
        }
}

OBL_TEST_CASE("weather_strike.bolt_replaces_the_old_bitmap_without_freeing_it")
{
    auto weather = NewWeather();
    uint8_t old = 0;
    weather->m_lightningLightmap = &old;
    FakeBoltEnv env;
    CreateLightningBolt(*weather, 3, env);
    OBL_CHECK(weather->m_lightningLightmap == env.Bitmap());
    OBL_CHECK_EQ(old, 0);
}

OBL_TEST_CASE("weather_strike.random_point_flash_and_next_strike")
{
    auto weather = StormWeather();
    unit::FakeStrikeEnv env;
    env.rands = {25, 7};
    Strike(*weather, 123.5f, env);
    OBL_CHECK_EQ(weather->m_strikePoint[0], 105.0f); // 25 % 10 + 100
    OBL_CHECK_EQ(weather->m_strikePoint[1], 207.0f); // 7 % 60 + 200
    OBL_CHECK_EQ(weather->m_strikePoint[2], 42.0f); // z stays
    OBL_CHECK_EQ(env.randCalls, 2u);
    OBL_CHECK_EQ(env.bolts, 1);
    OBL_CHECK(env.targets.empty());
    OBL_CHECK_EQ(env.nearRadius, 4);
    OBL_CHECK_EQ(weather->m_hasStrikeTarget, 0);
    OBL_CHECK_EQ(weather->m_lightFlash, 1);
    OBL_CHECK_EQ(weather->m_flashTime, 123.5f);
    OBL_CHECK_EQ(weather->m_flashColor, 0);
    OBL_CHECK_EQ(weather->m_strikes, 1);
    OBL_CHECK_EQ(weather->m_nextStrikeTime, NextStrikeTime(7, 123.5f));
    OBL_CHECK_EQ(weather->m_nextStrikeTime, static_cast<float>(3.0 * static_cast<double>(0.7f) + 123.5 + 3.0));
    // no channel holds the thunderclap: it is played and set to 0.8
    OBL_REQUIRE(env.sound.size() == 3u);
    OBL_CHECK_EQ(env.sound[0], std::string("find thunderclap"));
    OBL_CHECK_EQ(env.sound[1], std::string("play thunderclap"));
    OBL_CHECK_EQ(env.sound[2], std::string("volume 7 ") + std::to_string(0.8f));
}

OBL_TEST_CASE("weather_strike.level_zero_schedules_no_next_strike")
{
    auto weather = StormWeather();
    weather->m_strikeLevel = 0;
    unit::FakeStrikeEnv env;
    env.findResult = 3;
    Strike(*weather, 50.0f, env);
    OBL_CHECK_EQ(weather->m_strikes, 0);
    OBL_CHECK_EQ(weather->m_nextStrikeTime, 100.0f);
    OBL_REQUIRE(env.sound.size() == 2u);
    OBL_CHECK_EQ(env.sound[1], std::string("start 3")); // a channel has it: restarted
}

OBL_TEST_CASE("weather_strike.target_building_or_the_area_when_it_is_gone")
{
    auto weather = StormWeather();
    weather->m_hasStrikeTarget = 1;
    weather->m_strikeBuildingId = 17;
    unit::FakeStrikeEnv env;
    env.points[17] = {1000.0f, 2000.0f, 5.0f};
    Strike(*weather, 1.0f, env);
    OBL_CHECK_EQ(weather->m_strikePoint[0], 1000.0f);
    OBL_CHECK_EQ(weather->m_strikePoint[1], 2000.0f);
    OBL_CHECK_EQ(weather->m_strikePoint[2], 5.0f);
    OBL_REQUIRE(env.targets.size() == 1u);
    OBL_CHECK_EQ(env.targets[0], 17);
    OBL_CHECK_EQ(env.randCalls, 0u);
    OBL_CHECK_EQ(weather->m_hasStrikeTarget, 0);

    auto gone = StormWeather();
    gone->m_hasStrikeTarget = 1;
    gone->m_strikeBuildingId = 18;
    unit::FakeStrikeEnv other;
    other.rands = {3, 4};
    Strike(*gone, 1.0f, other);
    OBL_CHECK(other.targets.empty());
    OBL_CHECK_EQ(gone->m_strikePoint[0], 103.0f);
    OBL_CHECK_EQ(other.randCalls, 2u);
}

OBL_TEST_CASE("weather_strike.reversed_area_divides_by_a_negative_span")
{
    auto weather = StormWeather();
    weather->m_strikeArea = {110, 260, 100, 200}; // right < left: rand % -10 keeps the sign of rand
    unit::FakeStrikeEnv env;
    env.rands = {25, 70};
    Strike(*weather, 1.0f, env);
    OBL_CHECK_EQ(weather->m_strikePoint[0], 115.0f);
    OBL_CHECK_EQ(weather->m_strikePoint[1], 270.0f);
}

OBL_TEST_CASE("weather_strike.knocks_out_guests_heading_for_a_desire")
{
    auto weather = StormWeather();
    unit::FakeStrikeEnv env;
    object::Base heading{};
    MakeObject(heading, ObjectType::Guest, 31, strike::kStateHeadingDesire);
    object::Base idle{};
    MakeObject(idle, ObjectType::Guest, 32, 12);
    object::Base staff{};
    MakeObject(staff, ObjectType::Cleaner, 33, strike::kStateHeadingDesire);
    env.near = {&idle, &heading, &staff};
    env.pointTile = {40, 50};
    Strike(*weather, 1.0f, env);
    OBL_CHECK_EQ(env.nearTile.x, 40);
    OBL_CHECK_EQ(env.nearTile.y, 50);
    OBL_REQUIRE(env.actions.size() == 1u);
    OBL_CHECK(env.actions[0].first == &heading);
    OBL_CHECK_EQ(env.actions[0].second, 0xb);
    OBL_REQUIRE(env.posts.size() == 2u);
    OBL_CHECK_EQ(env.posts[0].sender, -2);
    OBL_CHECK_EQ(env.posts[0].receiver, 31);
    OBL_CHECK_EQ(env.posts[0].type, static_cast<int>(obl::messages::MT_STATECHANGE));
    OBL_CHECK_EQ(env.posts[0].flags, 0);
    OBL_CHECK_EQ(env.posts[0].data, 33); // STATE_GUEST_KNOCKED_OUT
    OBL_CHECK_EQ(env.posts[1].receiver, 31);
    OBL_CHECK_EQ(env.posts[1].type, static_cast<int>(obl::messages::MT_MOVESTOP));
    OBL_CHECK_EQ(env.posts[1].data, 0);
}

OBL_TEST_CASE("weather_strike.damages_the_first_building_whose_footprint_holds_the_tile")
{
    // footprints: rows of GridHeight tiles stepping x from the building tile (the tile itself is not tested), GridWidth rows stepping y
    object::Base a{}, b{}, c{};
    MakeObject(a, ObjectType::Building, 1, 0);
    MakeObject(b, ObjectType::Building, 2, 0);
    MakeObject(c, ObjectType::Building, 3, 0);
    auto run = [&](weather::StrikeTile hit) {
        auto weather = StormWeather();
        unit::FakeStrikeEnv env;
        env.pointTile = hit;
        env.buildings = {{&a, 2, 3}, {&b, 2, 3}, {&c, 1, 1}};
        env.tiles[&a] = {10, 10};
        env.tiles[&b] = {11, 10}; // overlaps a
        env.tiles[&c] = {11, 9}; // its single tile (12, 9)
        Strike(*weather, 1.0f, env);
        return env.damaged;
    };
    OBL_CHECK(run({12, 11}) == std::vector<const object::Base*>{&a}); // in a and in b: the walk stops after the first hit
    OBL_CHECK(run({14, 10}) == std::vector<const object::Base*>{&b}); // b only
    OBL_CHECK(run({10, 10}).empty()); // a's own tile is not part of the walk
    OBL_CHECK(run({12, 9}) == std::vector<const object::Base*>{&c});
    OBL_CHECK(run({13, 12}).empty()); // beyond a's second row
}

OBL_TEST_CASE("weather_strike.vandalises_repairable_scenery_nearby")
{
    auto weather = StormWeather();
    unit::FakeStrikeEnv env;
    env.pointTile = {100, 100};
    object::Base close{}, edge{}, occupied{}, fixed{}, building{}, corner{};
    MakeObject(close, ObjectType::Scenery, 1, 0);
    MakeObject(edge, ObjectType::Scenery, 2, 0);
    MakeObject(occupied, ObjectType::Scenery, 3, strike::kStateOccupied);
    MakeObject(fixed, ObjectType::Scenery, 4, 0);
    MakeObject(building, ObjectType::Building, 5, 0);
    MakeObject(corner, ObjectType::Scenery, 6, 0);
    env.others = {&close, &edge, &occupied, &fixed, &building, &corner};
    env.tiles[&close] = {97, 103};
    env.tiles[&edge] = {104, 100};
    env.tiles[&occupied] = {100, 100};
    env.tiles[&fixed] = {100, 100};
    env.tiles[&building] = {100, 100};
    env.tiles[&corner] = {103, 97};
    env.repairable = {&close, &edge, &occupied, &corner, &building};
    Strike(*weather, 1.0f, env);
    OBL_CHECK(env.vandalised == (std::vector<const object::Base*>{&close, &corner}));
}

OBL_TEST_CASE("weather_strike.tile_distance_is_the_absolute_difference")
{
    OBL_CHECK_EQ(lighting::TileDistance(3, 7), 4);
    OBL_CHECK_EQ(lighting::TileDistance(7, 3), 4);
    OBL_CHECK_EQ(lighting::TileDistance(INT_MIN, 0), INT_MIN); // stays negative, so it counts as near
    OBL_CHECK_EQ(lighting::TileDistance(INT_MAX, -1), INT_MIN);
}

OBL_TEST_CASE("weather_strike.update_lighting_runs_a_due_strike_before_the_tint")
{
    auto weather = StormWeather();
    for (uint32_t i = 0; i < 24; ++i) {
        weather->m_lightKeys[i].m_hour = static_cast<float>(i);
        weather->m_lightKeys[i].m_color = 0x40'40'40;
    }
    weather->m_previousLightIndex = 23;
    struct Env : unit::FakeStrikeEnv {
        int draws = 0;
        void DrawLightning() { ++draws; }
        void ReleaseLightmap(void*) {}
    } env;
    OBL_CHECK(!LightingUnsupported(*weather, 12.5f, 150.0f, false));
    UpdateLighting(*weather, 12.5f, 150.0f, true, env);
    OBL_CHECK_EQ(env.bolts, 1);
    OBL_CHECK_EQ(weather->m_lightFlash, 1);
    OBL_CHECK_EQ(weather->m_flashTime, 150.0f);
    OBL_CHECK_EQ(weather->m_strikes, 1);
    // the flash of this very frame doubles red and green: 64 * (1/255) * 2 * 31 = 15.56
    OBL_CHECK_EQ((weather->m_toneTables[0][31] >> 10) & 31, 15);

    // the next frame before the next strike: no new strike
    const int bolts = env.bolts;
    UpdateLighting(*weather, 12.5f, 151.0f, true, env);
    OBL_CHECK_EQ(env.bolts, bolts);
}

OBL_TEST_CASE("weather_strike.an_empty_area_is_unsupported_lighting")
{
    auto weather = StormWeather();
    weather->m_strikeArea = {100, 200, 100, 260};
    OBL_CHECK(LightingUnsupported(*weather, 12.5f, 150.0f, false));
    OBL_CHECK(!LightingUnsupported(*weather, 12.5f, 50.0f, false)); // not due yet
    weather->m_hasStrikeTarget = 1;
    OBL_CHECK(!LightingUnsupported(*weather, 12.5f, 150.0f, true)); // the target building is struck, no point drawn
    OBL_CHECK(LightingUnsupported(*weather, 12.5f, 150.0f, false)); // the target is gone
    weather->m_strikeArea = {100, 200, 110, 200}; // no height
    OBL_CHECK(LightingUnsupported(*weather, 12.5f, 150.0f, false));
}

OBL_TEST_CASE("weather_strike.next_strike_matches_the_thunder_level_schedule")
{
    for (int level = 0; level <= 10; ++level) {
        auto weather = NewWeather();
        SetThunderLevel(*weather, level, 77.25f);
        OBL_CHECK_EQ(weather->m_nextStrikeTime, NextStrikeTime(level, 77.25f));
    }
}
