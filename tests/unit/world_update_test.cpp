#include "unit_test.h"
#include "game/world/world_state.h"

#include "game/world/world_update_logic.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_update;

namespace {

struct Fixture {
    std::unique_ptr<world_data::World> world = std::make_unique<world_data::World>();
    world_data::Player player{};
    ui::ViewState view;
    Fixture()
    {
        world->m_player.reset(&player);
        world->m_gameSpeed = 1.0f;
        player.m_cash = 100.0f;
    }
};

struct Env {
    std::vector<std::string> calls;
    std::vector<int> held, pressed;
    float dt = 0.5f;
    int dayLength = 100;

    static bool Has(const std::vector<int>& keys, int dik)
    {
        for (int k : keys)
            if (k == dik) return true;
        return false;
    }
    void Log(const char* text) { calls.push_back(text); }

    void ApplyZoomRequest() { Log("zoom"); }
    void ResetViewport() { Log("reset"); }
    float FrameDelta() { return dt; }
    int DayLengthSeconds() { return dayLength; }
    void UpdateBuildingTotals() { Log("totals"); }
    void UpdateWeather() { Log("weather"); }
    void UpdateEvents() { Log("events"); }
    void RebuildProviders() { Log("providers"); }
    void UpdateObjects() { Log("objects"); }
    void CheckMissingStaffFeedback() { Log("staff"); }
    void UpdateFeedback() { Log("feedback"); }
    void UpdateDayCycle() { Log("daycycle"); }
    void UpdateMessages() { Log("messages"); }
    void ProcessDeleted() { Log("deleted"); }
    void CallGridFunction() { Log("grid"); }
    void UpdateCameraInput() { Log("camera"); }
    void UpdateHotkeys() { Log("hotkeys"); }
    bool KeyHeld(int dik) { return Has(held, dik); }
    bool KeyPressed(int dik) { return Has(pressed, dik); }
    void UpdateMapCamera() { Log("mapcamera"); }
    void RunScriptUpdateList() { Log("script"); }
    void SetFullViewport() { Log("viewport"); }
};

bool Is(const Env& env, std::initializer_list<const char*> expected)
{
    if (env.calls.size() != expected.size()) return false;
    size_t i = 0;
    for (const char* text : expected)
        if (env.calls[i++] != text) return false;
    return true;
}

} // namespace

OBL_TEST_CASE("world_update.step_order")
{
    Fixture f;
    Env env;
    world_script::WorldScript* script = reinterpret_cast<world_script::WorldScript*>(1);
    f.world->m_worldScript = script;
    Update(*f.world, f.view, env);
    OBL_CHECK(Is(env,
            {"zoom", "reset", "totals", "weather", "events", "providers", "objects", "staff", "feedback", "daycycle", "messages", "deleted",
                    "grid", "camera", "hotkeys", "mapcamera", "script", "viewport"}));
}

OBL_TEST_CASE("world_update.pause_skips_objects_and_time")
{
    Fixture f;
    Env env;
    f.world->m_paused = 1;
    f.world->m_gameTime = 12.0f;
    Update(*f.world, f.view, env);
    OBL_CHECK_EQ(f.world->m_gameDeltaTime, 0.0f);
    OBL_CHECK_EQ(f.world->m_gameTime, 12.0f);
    for (const std::string& call : env.calls) OBL_CHECK(call != "objects");
    OBL_CHECK_EQ(env.calls.size(), 16u); // no objects, no script list (null script)
}

OBL_TEST_CASE("world_update.hotkeys_disabled_and_no_script")
{
    Fixture f;
    Env env;
    f.view.m_hotkeysDisabled = 1;
    Update(*f.world, f.view, env);
    for (const std::string& call : env.calls) OBL_CHECK(call != "hotkeys" && call != "script");
}

OBL_TEST_CASE("world_update.step_is_clamped_to_four_seconds")
{
    Fixture f;
    Env env;
    env.dt = 1.0f;
    f.world->m_gameSpeed = 8.0f;
    Update(*f.world, f.view, env);
    OBL_CHECK_EQ(f.world->m_gameDeltaTime, 4.0f);
    OBL_CHECK_EQ(f.world->m_gameTime, 4.0f);
    f.world->m_gameSpeed = NAN;
    f.world->m_gameTime = 0.0f;
    Update(*f.world, f.view, env);
    OBL_CHECK(std::isnan(f.world->m_gameDeltaTime));
}

OBL_TEST_CASE("world_update.day_and_hour_from_the_clock")
{
    Fixture f;
    Env env;
    env.dayLength = 120;
    f.world->m_gameTime = 289.5f; // + 0.5 step = 290 s = two days and 50 s
    f.world->m_gameDayTime = 3.0f;
    Update(*f.world, f.view, env);
    OBL_CHECK_EQ(f.world->m_gameDay, 2);
    OBL_CHECK_EQ(f.world->m_gameDayTime, 10.0f); // 24 / 120 * 50
}

OBL_TEST_CASE("world_update.last_hour_bits_are_copied")
{
    Fixture f;
    Env env;
    f.world->m_gameDayTime = 3.0f;
    Update(*f.world, f.view, env);
    uint32_t bits;
    std::memcpy(&bits, &f.world->m_previousDayTime, 4);
    OBL_CHECK_EQ(bits, 0x40400000u);
}

OBL_TEST_CASE("world_update.zero_day_length_never_ends")
{
    int days = 0;
    double rest = 0.0;
    OBL_CHECK(!SplitDays(5.0, 0.0, days, rest, 1000));
    OBL_CHECK_EQ(days, 1000);
    OBL_CHECK(!SplitDays(5.0, -3.0, days, rest, 50));
    OBL_CHECK(SplitDays(0.0, 0.0, days, rest, 50)); // nothing to subtract: the loop is skipped
    OBL_CHECK_EQ(days, 0);
    OBL_CHECK(SplitDays(std::nan(""), 10.0, days, rest, 50)); // NaN time: `length < rest` is false
    OBL_CHECK(SplitDays(250.0, 100.0, days, rest, 50));
    OBL_CHECK_EQ(days, 2);
    OBL_CHECK_EQ(rest, 50.0);
}

OBL_TEST_CASE("world_update.developer_keys")
{
    Fixture f;
    Env env;
    env.held = {obl::input::KeyCode::LeftShift, obl::input::KeyCode::LeftAlt};
    env.pressed = {obl::input::KeyCode::Digit4};
    Update(*f.world, f.view, env);
    OBL_CHECK_EQ(f.player.m_cash, 5100.0f);
    OBL_CHECK_EQ(f.view.m_devToggle, 0);
    env.pressed = {obl::input::KeyCode::T};
    Update(*f.world, f.view, env);
    OBL_CHECK_EQ(f.view.m_devToggle, 1);
    Update(*f.world, f.view, env);
    OBL_CHECK_EQ(f.view.m_devToggle, 0);
    env.held = {obl::input::KeyCode::LeftShift}; // Alt missing
    env.pressed = {obl::input::KeyCode::Digit4, obl::input::KeyCode::T};
    Update(*f.world, f.view, env);
    OBL_CHECK_EQ(f.player.m_cash, 5100.0f);
    OBL_CHECK_EQ(f.view.m_devToggle, 0);
}
