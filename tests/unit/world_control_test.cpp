#include "unit_test.h"
#include "game/world/world_state.h"

#include "game/world/world_control_logic.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_control;

namespace {

std::unique_ptr<layout::World> NewWorld()
{
    auto world = std::make_unique<layout::World>();
    return world;
}

struct SpeedEnv {
    uint32_t table[4] = {0x3f800000u, 0x40000000u, 0x41000000u, 0x7fc00001u}; // 1, 2, 8 and a NaN with a payload
    uint32_t SpeedBits(int level) { return table[level]; }
};

struct ScriptEnv {
    std::vector<std::string> calls;
    void LockCamera() { calls.push_back("lock"); }
    void UnlockCamera() { calls.push_back("unlock"); }
    void CompleteScenario() { calls.push_back("complete"); }
    void Fatal(const char* text) { calls.push_back(std::string("fatal:") + text); }
};

struct StaffEnv {
    int counts[16] = {};
    std::vector<int> draws; // values rand() returns, in order
    size_t drawn = 0;
    int dayLength = 600;
    std::vector<std::string> calls;

    int CountByClass(char cls)
    {
        calls.push_back("count" + std::to_string(cls));
        return counts[static_cast<unsigned char>(cls)];
    }
    int Rand()
    {
        calls.push_back("rand");
        return draws.at(drawn++);
    }
    void AddFeedback(const char* key, int32_t id, uint32_t type)
    {
        calls.push_back(std::string("add ") + key + " " + std::to_string(id) + " " + std::to_string(type));
    }
    int DayLengthSeconds()
    {
        calls.push_back("day");
        return dayLength;
    }
};

// All five classes staffed except those listed.
void Staff(StaffEnv& env, std::initializer_list<int> missing)
{
    for (const obl::feedback::MissingStaff& check : obl::feedback::CurrentVocabulary().missingStaff)
        env.counts[static_cast<int>(check.objectClass)] = 2;
    for (int cls : missing) env.counts[cls] = 0;
}

} // namespace

OBL_TEST_CASE("world_control.speed_level_copies_the_table_bits_and_the_level")
{
    auto world = NewWorld();
    SpeedEnv env;
    SetGameSpeedLevel(*world, 2, env);
    OBL_CHECK_EQ(world->m_gameSpeed, 8.0f);
    OBL_CHECK_EQ(world->m_gameSpeedLevel, 2);
    SetGameSpeedLevel(*world, 0, env);
    OBL_CHECK_EQ(world->m_gameSpeed, 1.0f);
    OBL_CHECK_EQ(world->m_gameSpeedLevel, 0);
    SetGameSpeedLevel(*world, 3, env);
    uint32_t bits;
    std::memcpy(&bits, &world->m_gameSpeed, sizeof(bits));
    OBL_CHECK_EQ(bits, 0x7fc00001u);
}

OBL_TEST_CASE("world_control.script_functions_set_the_flags_they_name")
{
    auto world = NewWorld();
    ScriptEnv env;
    world->m_breakDownsAllowed = 1;
    world->m_allowLitter = 1;
    CallScriptFunction(*world, kFunctionPauseGame, env);
    OBL_CHECK_EQ(world->m_paused, 1);
    OBL_CHECK_EQ(world->m_forcedPaused, 1);
    CallScriptFunction(*world, kFunctionUnpauseGame, env);
    OBL_CHECK_EQ(world->m_paused, 0);
    OBL_CHECK_EQ(world->m_forcedPaused, 1); // only the pause flag is released
    CallScriptFunction(*world, kFunctionBreakdownsOff, env);
    OBL_CHECK_EQ(world->m_breakDownsAllowed, 0);
    CallScriptFunction(*world, kFunctionBreakdownsOn, env);
    OBL_CHECK_EQ(world->m_breakDownsAllowed, 1);
    CallScriptFunction(*world, kFunctionNoLitter, env);
    OBL_CHECK_EQ(world->m_allowLitter, 0);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("world_control.script_functions_call_the_game_for_camera_and_scenario")
{
    auto world = NewWorld();
    ScriptEnv env;
    CallScriptFunction(*world, kFunctionLockCamera, env);
    CallScriptFunction(*world, kFunctionUnlockCamera, env);
    CallScriptFunction(*world, kFunctionCompleteScenario, env);
    const std::vector<std::string> expected = {"lock", "unlock", "complete"};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("world_control.unrecognized_script_functions_are_fatal")
{
    for (int function : {7, 9, 100, -1, std::numeric_limits<int>::min()}) {
        auto world = NewWorld();
        ScriptEnv env;
        CallScriptFunction(*world, function, env);
        OBL_REQUIRE(env.calls.size() == 1);
        OBL_CHECK_EQ(env.calls[0], std::string("fatal:") + kInvalidFunctionText);
        OBL_CHECK_EQ(world->m_paused, 0); // nothing else is touched
    }
}

OBL_TEST_CASE("world_control.staff_check_waits_for_the_deadline")
{
    auto world = NewWorld();
    StaffEnv env;
    Staff(env, {});
    world->m_gameTime = 100.0f;
    world->m_nextStaffFeedbackTime = 100.0f; // equal: not yet
    CheckMissingStaffFeedback(*world, env);
    OBL_CHECK(env.calls.empty());
    OBL_CHECK_EQ(world->m_nextStaffFeedbackTime, 100.0f);
    world->m_nextStaffFeedbackTime = std::numeric_limits<float>::quiet_NaN();
    CheckMissingStaffFeedback(*world, env);
    OBL_CHECK(env.calls.empty());
    world->m_nextStaffFeedbackTime = 99.0f;
    world->m_gameTime = std::numeric_limits<float>::quiet_NaN();
    CheckMissingStaffFeedback(*world, env);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("world_control.staff_check_with_everyone_present_only_moves_the_deadline")
{
    auto world = NewWorld();
    StaffEnv env;
    Staff(env, {});
    world->m_gameTime = 1000.0f;
    world->m_nextStaffFeedbackTime = 10.0f;
    CheckMissingStaffFeedback(*world, env);
    const std::vector<std::string> expected = {"count11", "count9", "count8", "count12", "count7", "day"};
    OBL_CHECK(env.calls == expected);
    OBL_CHECK_EQ(env.drawn, size_t(0)); // no draw when nobody is missing
    // 600 s * (1/24 as float) * 3 + 1000
    OBL_CHECK_EQ(world->m_nextStaffFeedbackTime, static_cast<float>(600.0 * 0.041666668f * 3.0f + 1000.0));
}

OBL_TEST_CASE("world_control.staff_check_draws_among_the_missing_classes_in_the_fixed_order")
{
    // Missing: cleaners (11), mechanics (8) and lifeguards (7), in the check order cleaners, reps, mechanics, security, lifeguards.
    struct Case {
        int draw;
        const char* expected;
    };
    const Case cases[] = {{0, "add FEEDBACK_NO_CLEANERS -2 12"}, {3, "add FEEDBACK_NO_CLEANERS -2 12"},
            {1, "add FEEDBACK_NO_MECHANICS -2 9"}, {4, "add FEEDBACK_NO_MECHANICS -2 9"}, {2, "add FEEDBACK_NO_LIFEGUARDS -2 8"},
            {5, "add FEEDBACK_NO_LIFEGUARDS -2 8"}};
    for (const Case& test : cases) {
        auto world = NewWorld();
        StaffEnv env;
        Staff(env, {11, 8, 7});
        env.draws = {test.draw};
        world->m_gameTime = 50.0f;
        CheckMissingStaffFeedback(*world, env);
        OBL_REQUIRE(env.calls.size() == 8);
        OBL_CHECK_EQ(env.calls[5], std::string("rand"));
        OBL_CHECK_EQ(env.calls[6], std::string(test.expected));
        OBL_CHECK_EQ(env.calls[7], std::string("day"));
    }
}

OBL_TEST_CASE("world_control.staff_check_announces_reps_and_security_with_their_categories")
{
    for (int cls : {9, 12}) {
        auto world = NewWorld();
        StaffEnv env;
        Staff(env, {cls});
        env.draws = {12345};
        world->m_gameTime = 50.0f;
        CheckMissingStaffFeedback(*world, env);
        OBL_REQUIRE(env.calls.size() == 8);
        OBL_CHECK_EQ(env.calls[6], cls == 9 ? std::string("add FEEDBACK_NO_REPS -2 10") : std::string("add FEEDBACK_NO_SECURITY -2 13"));
    }
}

OBL_TEST_CASE("world_control.staff_check_deadline_uses_a_double_product_stored_once")
{
    auto world = NewWorld();
    StaffEnv env;
    Staff(env, {});
    env.dayLength = 777;
    world->m_gameTime = 123456.789f;
    world->m_nextStaffFeedbackTime = 0.0f;
    CheckMissingStaffFeedback(*world, env);
    const double expected = 777.0 * static_cast<double>(0.041666668f) * 3.0 + static_cast<double>(123456.789f);
    OBL_CHECK_EQ(world->m_nextStaffFeedbackTime, static_cast<float>(expected));
}
