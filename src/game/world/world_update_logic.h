#pragma once

#include "core/key_code.h"

#include "game/ui/view_state.h"
#include "game/world/world_state.h"

#include <cstdint>

// world::Update(World*): the per-frame dispatcher of the world.
// The step order is fixed below; every step is a call through an `Env` (the game side calls the real step), so the order and the time arithmetic are unit-tested without the DLL:
//   void ApplyZoomRequest()  void ResetViewport()  float FrameDelta()  int DayLengthSeconds()
//   void UpdateBuildingTotals()  void UpdateWeather()  void UpdateEvents()  void RebuildProviders()
//   void UpdateObjects()  void CheckMissingStaffFeedback()  void UpdateFeedback()  void UpdateDayCycle()
//   void UpdateMessages()  void ProcessDeleted()  void CallGridFunction()  void UpdateCameraInput()  void UpdateHotkeys()
//   bool KeyHeld(int code)  bool KeyPressed(int code)  void UpdateMapCamera()  void RunScriptUpdateList()  void SetFullViewport()
namespace obl::world_update {

constexpr float kMaxStep = 4.0f; // a frame never advances the game clock by more than 4 s
constexpr float kHoursPerDay = 24.0f;
constexpr float kCheatCash = 5000.0f;

// The remaining time and the day length are kept in wider registers (53-bit precision, no rounding to float) through the
// subtraction loop, and rounds once when it stores the hour. `while (length < rest)` is false for NaN , and a zero
// or negative length never lets `rest` fall: the loop does not end (kept; `cap` only exists so a unit test can observe it). Returns false
// when the cap stopped the loop.
inline bool SplitDays(double rest, double length, int& days, double& hourRest, uint64_t cap = UINT64_MAX)
{
    uint32_t counter = 0;
    uint64_t guard = 0;
    while (length < rest) {
        if (guard++ >= cap) {
            days = static_cast<int>(counter);
            hourRest = rest;
            return false;
        }
        ++counter;
        rest -= length;
    }
    days = static_cast<int>(counter);
    hourRest = rest;
    return true;
}

template<class Env> void Update(world::World& world, ui::ViewState& view, Env& env)
{
    env.ApplyZoomRequest();
    env.ResetViewport();

    world.m_previousDayTime = world.m_gameDayTime;
    if (world.m_paused == 0)
        world.m_gameDeltaTime = env.FrameDelta() * world.m_gameSpeed;
    else
        world.m_gameDeltaTime = 0.0f;
    if (world.m_gameDeltaTime > kMaxStep) world.m_gameDeltaTime = kMaxStep;
    world.m_gameTime = static_cast<float>(static_cast<double>(world.m_gameDeltaTime) + static_cast<double>(world.m_gameTime));

    const double length = static_cast<double>(env.DayLengthSeconds());
    int days = 0;
    double rest = 0.0;
    world.m_gameDay = 0;
    SplitDays(static_cast<double>(world.m_gameTime), length, days, rest);
    world.m_gameDay = days;
    world.m_gameDayTime = static_cast<float>((static_cast<double>(kHoursPerDay) / length) * rest);

    env.UpdateBuildingTotals();
    env.UpdateWeather();
    env.UpdateEvents();
    env.RebuildProviders();
    if (world.m_paused == 0) env.UpdateObjects();
    env.CheckMissingStaffFeedback();
    env.UpdateFeedback();
    env.UpdateDayCycle();
    env.UpdateMessages();
    env.ProcessDeleted();
    env.CallGridFunction();
    env.UpdateCameraInput();
    if (view.m_hotkeysDisabled == 0) env.UpdateHotkeys();

    // Developer keys, Left Shift + Left Alt + '4' (cash) / 'T' (a flag nothing reads); the short-circuit order is fixed.
    if (env.KeyHeld(obl::input::KeyCode::LeftShift) && env.KeyPressed(obl::input::KeyCode::Digit4) &&
            env.KeyHeld(obl::input::KeyCode::LeftAlt))
        world.m_player->m_cash = static_cast<float>(static_cast<double>(world.m_player->m_cash) + static_cast<double>(kCheatCash));
    if (env.KeyHeld(obl::input::KeyCode::LeftShift) && env.KeyPressed(obl::input::KeyCode::T) && env.KeyHeld(obl::input::KeyCode::LeftAlt))
        view.m_devToggle = view.m_devToggle == 0 ? 1 : 0;

    env.UpdateMapCamera();
    if (world.m_worldScript != nullptr) env.RunScriptUpdateList();
    env.SetFullViewport();
}

} // namespace obl::world_update
