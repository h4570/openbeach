#pragma once

#include "game/feedback/feedback_vocabulary.h"
#include "game/world/world_state.h"

#include <cstdint>
#include <cstring>
#include <vector>

// The small control methods of the World class:
// world::SetGameSpeedLevel(level) the speed multiplier from the game's table and the level
// world::CallScriptFunction(function) the `Function <name>;` commands of the world script
// world::CheckMissingStaffFeedback() every three game hours: a "no <profession>" message for one missing staff class
// The calls into other objects stay behind an `Env` (the game side calls the game's own functions), so the order of events is unit-tested
// without the DLL. Each function lists the members of its Env.
namespace obl::world_control {

// ---- world::SetGameSpeedLevel -------------------------------------------------------------------------------------------------
// The values InitGameSpeedTable (a static initializer) copies into the game speed table.
inline constexpr float kGameSpeedDefaults[3] = {1.0f, 2.0f, 8.0f};

// uint32_t SpeedBits(int level) g_GameSpeedTable[level] (1.0, 2.0, 8.0), read as raw bits
// The level is not range-checked: the table is indexed with whatever the caller passes (its only caller is game_api::SetVariable).
template<class Env> void SetGameSpeedLevel(world::World& world, int level, Env& env)
{
    const uint32_t bits = env.SpeedBits(level);
    std::memcpy(&world.m_gameSpeed, &bits, sizeof(bits)); // a plain copy, not a float load: the bits are kept as they are
    world.m_gameSpeedLevel = level;
}

// ---- world::CallScriptFunction ------------------------------------------------------------------------------------------------
// The numbers the scripts' `Function` command passes (world_script_types.h; 7 = Debug is never passed: the interpreter skips it).
constexpr int kFunctionPauseGame = 0, kFunctionUnpauseGame = 1, kFunctionLockCamera = 2, kFunctionUnlockCamera = 3,
              kFunctionBreakdownsOff = 4, kFunctionBreakdownsOn = 5, kFunctionCompleteScenario = 6, kFunctionDebug = 7,
              kFunctionNoLitter = 8;
// The text of the FatalError for a number the switch does not know (7 and everything above 8, also negative numbers).
constexpr const char* kInvalidFunctionText = "The world script called an unrecognized function";

//   void LockCamera()  void UnlockCamera()   LockCamera/UnlockCamera of the map view
//   void CompleteScenario()                  the game's CompleteCurrentScenario
//   void Fatal(const char*)                  FatalError
template<class Env> void CallScriptFunction(world::World& world, int function, Env& env)
{
    switch (static_cast<uint32_t>(function) > 8 ? 7 : function) { // unsigned compare: negative numbers go to the default too
    case kFunctionPauseGame:
        world.m_paused = 1;
        world.m_forcedPaused = 1;
        break;
    case kFunctionUnpauseGame: world.m_paused = 0; break;
    case kFunctionLockCamera: env.LockCamera(); break;
    case kFunctionUnlockCamera: env.UnlockCamera(); break;
    case kFunctionBreakdownsOff: world.m_breakDownsAllowed = 0; break;
    case kFunctionBreakdownsOn: world.m_breakDownsAllowed = 1; break;
    case kFunctionCompleteScenario: env.CompleteScenario(); break;
    case kFunctionNoLitter: world.m_allowLitter = 0; break;
    default: env.Fatal(kInvalidFunctionText); break;
    }
}

// ---- world::CheckMissingStaffFeedback -----------------------------------------------------------------------------------------
// The staff classes the check counts, in the pack's order (feedback vocabulary, `missing_staff`), with the message each one shows when none
// exists (feedback::Feedback::AddKey type = the category of the message; the object id is -2, "no object").
constexpr int32_t kNoObject = -2;
constexpr float kOneOverTwentyFour = 0.041666668f; // 1/24 as a float
constexpr float kRepeatHours = 3.0f;

//   int CountByClass(char cls)                              the object manager's count of a class
//   int Rand()                                              the CRT rand
//   void AddFeedback(const char* key, int32_t id, uint32_t type)   feedback::Feedback::AddKey of the feedback manager
//   int DayLengthSeconds()                                  game_parameters::GetInt("iDAY_LENGTH_IN_SECONDS")
// Once the game time is past the check's deadline the five classes are counted; with `n` of them missing, `rand() % n` picks which of the missing ones is
// announced (one message), and the next check is set three game hours later (`day length * 1/24 * 3 + game time`, one float store of a
// 53-bit result). Nothing is drawn or announced when no class is missing, but the deadline still moves. A NaN deadline or game time never fires.
template<class Env> void CheckMissingStaffFeedback(world::World& world, Env& env)
{
    if (!(world.m_gameTime > world.m_nextStaffFeedbackTime)) // not above, or unordered
        return;
    const std::vector<feedback::MissingStaff>& checks = feedback::CurrentVocabulary().missingStaff;
    std::vector<int> counts(checks.size());
    int missing = 0;
    for (size_t i = 0; i < checks.size(); ++i) {
        counts[i] = env.CountByClass(static_cast<char>(checks[i].objectClass));
        if (counts[i] == 0) ++missing;
    }
    if (missing != 0) {
        int skip = env.Rand() % missing; // rand() is never negative
        for (size_t i = 0; i < checks.size(); ++i) {
            if (counts[i] != 0) continue;
            if (skip == 0) {
                env.AddFeedback(checks[i].key.c_str(), kNoObject, checks[i].type);
                break;
            }
            --skip;
        }
    }
    const double seconds = static_cast<double>(env.DayLengthSeconds());
    world.m_nextStaffFeedbackTime = static_cast<float>(
            seconds * static_cast<double>(kOneOverTwentyFour) * static_cast<double>(kRepeatHours) + static_cast<double>(world.m_gameTime));
}

} // namespace obl::world_control
