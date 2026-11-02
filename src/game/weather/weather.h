#pragma once

#include "game/weather/weather_state.h"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace obl::weather {

// Runtime weather adapters bind world-owned state to objects, sound and the renderer.
bool RegisterTestDrivers();

namespace runtime {
weather::Weather* Create();
void Destroy(weather::Weather* weatherState);
bool LoadJson(weather::Weather* weatherState, const nlohmann::json& in, std::string& error);

// The world owns the weather state.
void UpdateSchedule(weather::Weather* weatherState);
void InsertEntry(weather::Weather* weatherState, const weather::WeatherEntry& entry);
void UpdateLighting(weather::Weather* weatherState);
void CreateLightningBolt(weather::Weather* weatherState);
void DrawLightning(weather::Weather* weatherState);
void DrawRain(weather::Weather* weatherState);
// Takes no Weather: the rain drops are a global buffer, and the view is read from the map view.
void RandomizeRainDrops();
weather::RainDropBuffer& RainDrops();

void SetOverrideRed(weather::Weather* weatherState, float value);
void SetOverrideGreen(weather::Weather* weatherState, float value);
void SetOverrideBlue(weather::Weather* weatherState, float value);
double GetOverrideRed(weather::Weather* weatherState);
double GetOverrideGreen(weather::Weather* weatherState);
double GetOverrideBlue(weather::Weather* weatherState);
void SetThunderLevel(weather::Weather* weatherState, int level);
void SetLightningArea(weather::Weather* weatherState, int left, int top, int right, int bottom);
void SetLightningTargetByName(weather::Weather* weatherState, const char* key);
float* GetCurrentModifiers(weather::Weather* weatherState);

// Test seam (null in the game, like message_handler's probes): when set, UpdateSchedule calls it instead of the schedule step, and the
// probe runs the step itself through RunSchedule. The guest-desire probe logs each activation this way.
using ScheduleProbe = void (*)(weather::Weather*);
extern ScheduleProbe g_scheduleProbe;
void RunSchedule(weather::Weather* weatherState);

} // namespace runtime

// Script commands `lightning`, `lightning-target`, `lightning-state` (test runs only).
bool ExecuteCommand(const std::string& name, const std::vector<std::string>& args);

} // namespace obl::weather
