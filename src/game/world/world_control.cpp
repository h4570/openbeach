#include <bit>
#include "game/game/startup_tables.h"
#include "game/world/world_control.h"
#include "game/game/diagnostics.h"
#include "game/objects/object_registry_api.h"
#include "game/feedback/feedback_manager.h"
#include "game/game/game_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "core/fatal.h"
#include "game/ui/map_view.h"
#include "game/world/world_state.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/world/world_control_logic.h"

#include <cstdint>
#include <limits>

namespace obl::world {
namespace {

using world_data::World;

// `call-drive` runs these two on the live world (the counters of the entry points below move).
test::CallCounter g_driveScript("world.call-script-function.drive");
test::CallCounter g_driveStaff("world.check-missing-staff-feedback.drive");
// Call counters (`call-report <name>`).
test::CallCounter g_speedCalls("world.set-game-speed-level");
test::CallCounter g_scriptCalls("world.call-script-function");
test::CallCounter g_staffCalls("world.check-missing-staff-feedback");
// Reported by name only: nothing counts into it.
test::CallCounter g_totalsJmp("world.update-building-totals-forward");

// ---- the game side of the Env contracts in src/game/world/world_control_logic.h ------------------------------------------------------------

class GameEnv {
public:
    explicit GameEnv(World& world) : m_world(world) {}

    uint32_t SpeedBits(int level) { return std::bit_cast<uint32_t>(game::GameSpeedTable()[level]); }
    void LockCamera() { (m_world.m_mapView)->LockCamera(); }
    void UnlockCamera() { (m_world.m_mapView)->UnlockCamera(); }
    void CompleteScenario() { game::game_api::CompleteCurrentScenario(); }
    void Fatal(const char* text) { game::FatalError(text); }
    int CountByClass(char cls) { return object::object_manager::CountByClass(m_world.m_objectManager, cls); }
    int Rand() { return game::Rand(); }
    void AddFeedback(const char* key, int32_t id, uint32_t type) { feedback::Feedback(m_world.m_feedbackManager).AddKey(key, id, type); }
    int DayLengthSeconds()
    {
        int32_t seconds = 0; // left uninitialised when the key is missing; it never is
        game::game_parameters::GetInt(game::game_parameters::Current(), &seconds, "iDAY_LENGTH_IN_SECONDS");
        return seconds;
    }

protected:
    World& m_world;
};

// ---- the entry points ---------------------------------------------------------------------------------------------------------------------

void SetGameSpeedLevelEntry(World* world, int level)
{
    g_speedCalls.Count();
    GameEnv env(*world);
    world_control::SetGameSpeedLevel(*world, level, env);
}

void CallScriptFunctionEntry(World* world, int function)
{
    g_scriptCalls.Count();
    GameEnv env(*world);
    world_control::CallScriptFunction(*world, function, env);
}

// `call-drive world.call-script-function <rounds>`: every function the scripts can call except CompleteScenario, in both orders of the pairs, with
// the World's flags and the camera lock restored afterwards.
void DriveScript(int rounds)
{
    World* world = game::GetWorld();
    if (!world || !world->m_mapView) {
        OBL_LOG("autotest: FAILED call-drive world.call-script-function: no world");
        return;
    }
    auto* view = static_cast<ui::MapView*>(world->m_mapView.get());
    const uint8_t paused = world->m_paused, forced = world->m_forcedPaused, breakdowns = world->m_breakDownsAllowed,
                  litter = world->m_allowLitter, locked = view->m_mapLocked;
    static const int kFunctions[] = {world_control::kFunctionPauseGame, world_control::kFunctionUnpauseGame,
            world_control::kFunctionLockCamera, world_control::kFunctionUnlockCamera, world_control::kFunctionBreakdownsOff,
            world_control::kFunctionBreakdownsOn, world_control::kFunctionNoLitter};
    for (int round = 0; round < rounds; ++round)
        for (int a : kFunctions)
            for (int b : kFunctions) {
                CallScriptFunctionEntry(world, a);
                CallScriptFunctionEntry(world, b);
            }
    world->m_paused = paused;
    world->m_forcedPaused = forced;
    world->m_breakDownsAllowed = breakdowns;
    world->m_allowLitter = litter;
    view->m_mapLocked = locked;
}

void CheckMissingStaffFeedbackEntry(World* world)
{
    g_staffCalls.Count();
    GameEnv env(*world);
    world_control::CheckMissingStaffFeedback(*world, env);
}

// `call-drive world.check-missing-staff-feedback <rounds>`: the deadline just behind, equal to, just before and far before the game time, NaN either
// way and a huge game time, over the staff the world has (a real run covers the other sets of missing classes). The announcements
// are real; only the World's time fields are restored.
void DriveStaff(int rounds)
{
    World* world = game::GetWorld();
    if (!world || !world->m_objectManager) {
        OBL_LOG("autotest: FAILED call-drive world.check-missing-staff-feedback: no world");
        return;
    }
    const float savedTime = world->m_gameTime;
    const float savedDeadline = world->m_nextStaffFeedbackTime;
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    const float times[] = {savedTime, 0.0f, 1.0e9f, kNaN, kInf, -5.0f};
    const float deadlines[] = {savedTime - 1.0f, savedTime, savedTime + 1.0f, -kInf, kNaN, 0.0f, 1.0e9f};
    for (int round = 0; round < rounds; ++round)
        for (float time : times)
            for (float deadline : deadlines) {
                world->m_gameTime = time;
                world->m_nextStaffFeedbackTime = deadline;
                CheckMissingStaffFeedbackEntry(world);
            }
    world->m_gameTime = savedTime;
    world->m_nextStaffFeedbackTime = savedDeadline;
}

} // namespace

void SetPowerTotals(World& world, float demand, float installed, float output)
{
    world.m_powerUse = demand;
    world.m_maxPower = installed;
    world.m_currentPower = output;
}
void SetBreakdownsAllowed(World& world, bool allowed)
{
    world.m_breakDownsAllowed = allowed;
}
void SetGameSpeedFactor(World& world, float factor)
{
    world.m_gameSpeed = factor;
}
void SetGameSpeedLevel(World& world, int level)
{
    SetGameSpeedLevelEntry(&world, level);
}
void CallScriptFunction(World& world, int function)
{
    CallScriptFunctionEntry(&world, function);
}
void CheckMissingStaffFeedback(World& world)
{
    CheckMissingStaffFeedbackEntry(&world);
}

bool RegisterWorldControlDrivers()
{
    g_driveScript.SetDriver(&DriveScript);
    g_driveStaff.SetDriver(&DriveStaff);
    return true;
}

} // namespace obl::world
