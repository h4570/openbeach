#include "game/game/game_logic.h"
#include "unit_test.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {
using obl::game::Game;
namespace logic = obl::game_logic;

// A three-scenario campaign with two sandboxes: entry 0 always open, entry 1 open at the start.
struct FakeEnv {
    std::map<std::string, std::string> files;
    int campaignLoads = 0;
    bool LoadCampaign(obl::scenario_load::Campaign& campaign)
    {
        ++campaignLoads;
        campaign.campaign = {"s1", "s2", "s3"};
        campaign.sandboxes = {"b1", "b2"};
        campaign.initiallyAvailable = {1};
        campaign.alwaysAvailable = {0};
        return true;
    }
    bool ReadFile(const char* name, std::string& out)
    {
        const auto it = files.find(name);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
    void WriteFile(const char* name, const std::string& text) { files[name] = text; }
};

struct FakeSettings {
    uint8_t m_nudgeScroll = 0, m_seaEffects = 0, m_lightEffects = 0, m_keyline = 0;
};

struct FakeVariables {
    FakeSettings settings;
    bool staff = false;
    std::map<std::string, std::vector<int>> lists;
    bool cursorReset = false;
    std::optional<int> speed;
    int manual = 0;
    std::vector<std::string> calls;
    FakeSettings& Settings() { return settings; }
    bool StaffSelected() const { return staff; }
    size_t Selected(const char* name) { return lists[name].size(); }
    int FirstSelected(const char* name)
    {
        cursorReset = true;
        return lists[name].front();
    }
    std::optional<int> GameSpeed() const { return speed; }
    int PlaylistManual() const { return manual; }
    void SetSeaEffects(uint8_t on) { calls.push_back("sea " + std::to_string(on)); }
    void SetKeyline(uint8_t on) { calls.push_back("keyline " + std::to_string(on)); }
    void SetGameSpeed(int level) { calls.push_back("speed " + std::to_string(level)); }
    void PlaylistCommand(int command) { calls.push_back("playlist " + std::to_string(command)); }
    void Unrecognized(const char* name) { calls.push_back(std::string("unrecognized ") + name); }
};

// By value: CHECK_EQ binds its operand to a reference, and `GetVariable(...).value()` would refer into the returned
// optional, which dies at the end of that declaration (GCC -O2 read 0 from the reused stack).
int Value(std::optional<int> value)
{
    return value.value();
}
} // namespace

OBL_TEST_CASE("Game logic resets progress from the campaign without a progress file")
{
    Game game{};
    FakeEnv env;
    logic::LoadProgress(game, env);
    OBL_CHECK_EQ(game.m_scenarioProgress[0], 4);
    OBL_CHECK_EQ(game.m_scenarioProgress[1], 2);
    OBL_CHECK_EQ(game.m_scenarioProgress[2], 1);
    OBL_CHECK_EQ(game.m_scenarioProgress[11], 1);
    OBL_CHECK_EQ(game.m_lastUnlockedScenario, 0);
}

OBL_TEST_CASE("Game logic saves completion and loads it back with the next scenario selected")
{
    Game game{};
    FakeEnv env;
    logic::LoadProgress(game, env);
    game.m_lastUnlockedScenario = 1;
    logic::CompleteScenario(game, env);
    OBL_CHECK(env.files.count(obl::progress_file::kFileName) == 1);
    OBL_CHECK_EQ(game.m_scenarioProgress[1], 10);
    OBL_CHECK_EQ(game.m_scenarioProgress[2], 2);

    Game loaded{};
    logic::LoadProgress(loaded, env);
    for (int i = 0; i < 12; ++i) OBL_CHECK_EQ(loaded.m_scenarioProgress[i], game.m_scenarioProgress[i]);
    OBL_CHECK_EQ(loaded.m_lastUnlockedScenario, 2);
    // Sandbox mode always starts at the first entry.
    logic::SetSandbox(loaded, 1, env);
    OBL_CHECK_EQ(loaded.m_sandbox, 1);
    OBL_CHECK_EQ(loaded.m_lastUnlockedScenario, 0);
}

OBL_TEST_CASE("Game logic resets progress from a broken progress file")
{
    Game game{};
    FakeEnv env;
    env.files[obl::progress_file::kFileName] = "not json";
    logic::LoadProgress(game, env);
    OBL_CHECK_EQ(game.m_scenarioProgress[0], 4);
    OBL_CHECK_EQ(game.m_scenarioProgress[1], 2);
}

OBL_TEST_CASE("Game logic lists campaign and sandbox scenarios in name order")
{
    Game game{};
    FakeEnv env;
    game.m_scenarios.push_back(u"stale");
    logic::ScanScenarios(game, logic::Campaign(env));
    OBL_REQUIRE(game.m_scenarios.size() == 5);
    OBL_CHECK(game.m_scenarios[0] == u"SandScenario1");
    OBL_CHECK(game.m_scenarios[2] == u"Scenario1");
    OBL_CHECK(game.m_scenarios[4] == u"Scenario3");
}

OBL_TEST_CASE("Game logic answers the GUI variables")
{
    Game game{};
    FakeVariables env;
    env.settings.m_seaEffects = 1;
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "GAME_SEA", env)), 1);
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "GAME_NUDGE", env)), 0);
    env.staff = true;
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "STAFF_DISMISS_STATE", env)), 1);
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "GAME_GAME_SPEED", env)), 0);
    env.speed = 3;
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "GAME_GAME_SPEED", env)), 3);
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "GAME_GAME_SPEED_STATUS_2", env)), 1);
    game.m_scenarioProgress[4] = 2;
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "GAME_SCENARIO_SELECT_STATUS_4", env)), 1);
    game.m_scenarioProgress[4] = 1;
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "GAME_SCENARIO_SELECT_STATUS_4", env)), 0);
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "PLAYLIST_MODE", env)), 1);
    env.manual = 2;
    OBL_CHECK(!logic::GetVariable(game, "PLAYLIST_MODE", env));
    OBL_CHECK(env.calls.empty());
    OBL_CHECK(!logic::GetVariable(game, "NO_SUCH", env));
    OBL_REQUIRE(env.calls.size() == 1);
    OBL_CHECK(env.calls[0] == "unrecognized NO_SUCH");
}

OBL_TEST_CASE("Game logic enables load and save buttons from the list selections")
{
    Game game{};
    FakeVariables env;
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "LOADSAVE_SAVE_STATE", env)), 0);
    env.lists["LoadListBox"] = {2};
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "LOADSAVE_LOAD_STATE", env)), 0); // no save slots
    game.m_saveSlots.push_back(std::make_unique<obl::save_slots::Slot>());
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "LOADSAVE_LOAD_STATE", env)), 1);
    // Row 0 of the save list (the new-save entry) cannot be deleted.
    env.lists["SaveListBox"] = {0};
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "LOADSAVE_SAVE_STATE", env)), 1);
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "LOADSAVE_SAVEDELETE_STATE", env)), 0);
    OBL_CHECK(env.cursorReset);
    env.lists["SaveListBox"] = {3};
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "LOADSAVE_SAVEDELETE_STATE", env)), 1);
    env.lists["SaveListBox"] = {3, 4};
    OBL_CHECK_EQ(Value(logic::GetVariable(game, "LOADSAVE_SAVEDELETE_STATE", env)), 0);
}

OBL_TEST_CASE("Game logic stores GUI variables and forwards them")
{
    Game game{};
    FakeVariables env;
    logic::SetVariable(game, "GAME_SEA", 5, env);
    logic::SetVariable(game, "GAME_KEYLINES", 0, env);
    logic::SetVariable(game, "GAME_LIGHTS", 1, env);
    logic::SetVariable(game, "GAME_SCENARIO_SELECT", 7, env);
    logic::SetVariable(game, "GAME_GAME_SPEED", 2, env);
    logic::SetVariable(game, "PLAYLIST_MODE", 0, env);
    logic::SetVariable(game, "PLAYLIST_MODE", 1, env);
    logic::SetVariable(game, "BOGUS", 1, env);
    OBL_CHECK_EQ(env.settings.m_seaEffects, 1);
    OBL_CHECK_EQ(env.settings.m_keyline, 0);
    OBL_CHECK_EQ(env.settings.m_lightEffects, 1);
    OBL_CHECK_EQ(game.m_lastUnlockedScenario, 7);
    const std::vector<std::string> expected = {"sea 1", "keyline 0", "speed 2", "playlist 9", "playlist 8", "unrecognized BOGUS"};
    OBL_CHECK(env.calls == expected);
}
