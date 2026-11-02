#include "unit_test.h"
#include "game/weather/weather_lifecycle.h"

#include <vector>

using namespace obl;
namespace {
struct Env {
    weather::Weather* global = nullptr;
    int randomized = 0, lit = 0;
    std::vector<void*> deleted;
    void SetGlobal(weather::Weather* value) { global = value; }
    void ResetShared() { global = nullptr; }
    void RandomizeRainDrops() { ++randomized; }
    void UpdateLighting(weather::Weather& value)
    {
        OBL_CHECK_EQ(value.m_currentState, 1u);
        OBL_CHECK_EQ(value.m_previousLightIndex, 23u);
        ++lit;
    }
    void Delete(void* value) { deleted.push_back(value); }
};
} // namespace
OBL_TEST_CASE("weather_lifecycle.construct_state")
{
    weather::Weather value;
    Env env;
    weather::Parameters parameters;
    for (int i = 0; i < 24; ++i) parameters.lightKeys[i] = {static_cast<float>(i), static_cast<uint32_t>(i * 123)};
    for (int i = 0; i < 14; ++i) parameters.stateColors[i] = i * 456;
    OBL_CHECK_EQ(weather::ConstructWeather(value, env, parameters), &value);
    OBL_CHECK_EQ(env.global, &value);
    OBL_CHECK_EQ(env.randomized, 1);
    OBL_CHECK_EQ(env.lit, 1);
    OBL_CHECK_EQ(value.m_rainChannel, -1);
    OBL_CHECK_EQ(value.m_strikeBuildingId, -1);
    OBL_CHECK_EQ(value.m_schedule.size(), size_t{0});
    for (int i = 0; i < 24; ++i) {
        OBL_CHECK_EQ(value.m_lightKeys[i].m_hour, static_cast<float>(i));
        OBL_CHECK_EQ(value.m_lightKeys[i].m_color, parameters.lightKeys[i].m_color);
    }
    for (int i = 0; i < 14; ++i) OBL_CHECK_EQ(value.m_stateColors[i], parameters.stateColors[i]);
    for (float modifier : value.m_desireModifiers) OBL_CHECK_EQ(modifier, 0.0f);
}
OBL_TEST_CASE("weather_lifecycle.destroy_owned_schedule_and_bitmap")
{
    weather::Weather value;
    Env env;
    weather::Parameters parameters;
    for (int i = 0; i < 24; ++i) parameters.lightKeys[i] = {static_cast<float>(i), static_cast<uint32_t>(i * 123)};
    for (int i = 0; i < 14; ++i) parameters.stateColors[i] = i * 456;
    weather::ConstructWeather(value, env, parameters);
    for (int i = 0; i < 70; ++i) {
        auto entry = std::make_unique<weather::WeatherEntry>();
        entry->m_day = i;
        value.m_schedule.push_back(std::move(entry));
    }
    OBL_CHECK_EQ(weather::lifecycle::ScheduleElement(value.m_schedule, 40)->m_day, 40);
    int bitmap = 0;
    value.m_lightningLightmap = &bitmap;
    weather::DestructWeather(value, env);
    OBL_CHECK_EQ(value.m_schedule.size(), size_t{0});
    OBL_CHECK_EQ(env.deleted.size(), size_t{1});
    OBL_CHECK_EQ(env.deleted[0], &bitmap);
    OBL_CHECK(value.m_lightningLightmap == nullptr);
    OBL_CHECK(env.global == nullptr);
}

OBL_TEST_CASE("weather_parameters.refuse_missing_row")
{
    nlohmann::ordered_json document = {{"format", "obl.weather_parameters"}, {"version", 2},
            {"light_keys", nlohmann::ordered_json::array()}, {"state_colors", std::vector<int>(14, 0)}};
    for (int i = 0; i < 24; ++i) document["light_keys"].push_back({i, i * 100});
    weather::Parameters parameters;
    std::string error;
    OBL_CHECK(weather::ReadParameters(document, parameters, error));
    OBL_CHECK(!weather::ReadParameters(nlohmann::ordered_json::parse("{", nullptr, false), parameters, error));
    const auto valid = document;
    document["version"] = 1;
    OBL_CHECK(!weather::ReadParameters(document, parameters, error));
    document = valid;
    document["state_colors"].erase(7);
    OBL_CHECK(!weather::ReadParameters(document, parameters, error));
    document = valid;
    document["light_keys"][12][0] = 11;
    OBL_CHECK(!weather::ReadParameters(document, parameters, error));
    document = valid;
    document["light_keys"].erase(12);
    OBL_CHECK(!weather::ReadParameters(document, parameters, error));
    OBL_CHECK_EQ(parameters.lightKeys[12].m_color, 1200u);
}
