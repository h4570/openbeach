#pragma once

#include "game/game/game.h"
#include "game/game/game_core.h"
#include "game/game/progress_file.h"
#include "game/world/scenario_load.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace obl::game_logic {

template<class Env> scenario_load::Campaign Campaign(Env& env)
{
    scenario_load::Campaign campaign;
    env.LoadCampaign(campaign);
    return campaign;
}

template<class Env> void ResetProgress(game::Game& game, Env& env)
{
    const scenario_load::Campaign campaign = Campaign(env);
    game_core::Reset(game.m_scenarioProgress, game.m_lastUnlockedScenario, campaign.initiallyAvailable, campaign.alwaysAvailable);
}

// progress.json first; sp.bin only seeds a user who has no progress.json yet (the next save rewrites it).
// A file that does not parse resets the progress.
template<class Env> void LoadProgress(game::Game& game, Env& env)
{
    ResetProgress(game, env);
    std::string text;
    if (env.ReadFile(progress_file::kFileName, text)) {
        if (!progress_file::FromJson(text, game.m_scenarioProgress)) ResetProgress(game, env);
    } else if (env.ReadFile(progress_file::kSeedFileName, text)) {
        if (!progress_file::FromSeedFile(text, game.m_scenarioProgress)) ResetProgress(game, env);
    }
    const auto count = static_cast<int32_t>(Campaign(env).campaign.size());
    game.m_lastUnlockedScenario = game_core::SelectAfterLoad(game.m_scenarioProgress, game.m_sandbox != 0, count);
}

template<class Env> void SaveProgress(const game::Game& game, Env& env)
{
    env.WriteFile(progress_file::kFileName, progress_file::ToJson(game.m_scenarioProgress));
}

template<class Env> void CompleteScenario(game::Game& game, Env& env)
{
    const auto count = static_cast<int32_t>(Campaign(env).campaign.size());
    game_core::Complete(game.m_scenarioProgress, game.m_lastUnlockedScenario, count);
    SaveProgress(game, env);
}

template<class Env> void SetSandbox(game::Game& game, uint8_t sandbox, Env& env)
{
    game.m_sandbox = sandbox;
    LoadProgress(game, env);
}

// The scenario screen's names in directory-scan order; nothing is read from an installed
// Data/Scenario tree. The names are ASCII, so widening is a plain cast.
inline void ScanScenarios(game::Game& game, const scenario_load::Campaign& campaign)
{
    game.m_scenarios.clear();
    for (const std::string& entry :
            game_core::ScenarioListNames(static_cast<int32_t>(campaign.campaign.size()), static_cast<int32_t>(campaign.sandboxes.size())))
        game.m_scenarios.emplace_back(entry.begin(), entry.end());
}

// Variable env (Game::GetVariable):
//   auto& Settings()                         scroll, sea, lighting and outline options
//   bool StaffSelected()                     the GUI context's staff slot is set
//   size_t Selected(const char* list)        selected rows of the named list box (0 without a selection)
//   int FirstSelected(const char* list)      the first selected row, after resetting the list's selection cursor
//   std::optional<int> GameSpeed()           the World's speed level, none between two worlds
//   int PlaylistManual()                     the music player's mode
//   void Unrecognized(const char* name)           a fatal error in the game
// Returns the decimal value the GUI shows, none for an unknown name or a playlist mode that is never written.
template<class Env> std::optional<int> GetVariable(const game::Game& game, const char* name, Env& env)
{
    auto& settings = env.Settings();
    if (!std::strcmp(name, "GAME_NUDGE")) return settings.m_nudgeScroll;
    if (!std::strcmp(name, "GAME_SEA")) return settings.m_seaEffects;
    if (!std::strcmp(name, "GAME_LIGHTS")) return settings.m_lightEffects;
    if (!std::strcmp(name, "GAME_KEYLINES")) return settings.m_keyline;
    if (!std::strcmp(name, "STAFF_MAKEALL_STATE") || !std::strcmp(name, "STAFF_DISMISS_STATE")) return env.StaffSelected() ? 1 : 0;
    if (!std::strcmp(name, "LOADSAVE_LOAD_STATE") || !std::strcmp(name, "LOADSAVE_LOADDELETE_STATE"))
        return env.Selected("LoadListBox") == 1 && !game.m_saveSlots.empty();
    if (!std::strcmp(name, "LOADSAVE_SAVE_STATE")) return env.Selected("SaveListBox") == 1;
    if (!std::strcmp(name, "LOADSAVE_SAVEDELETE_STATE")) {
        // Row 0 of the save list is the "new save" entry, which cannot be deleted.
        const size_t count = env.Selected("SaveListBox");
        int value = count == 1 && !game.m_saveSlots.empty();
        if (count && env.FirstSelected("SaveListBox") == 0) value = 0;
        return value;
    }
    if (!std::strcmp(name, "GAME_SCENARIO_SELECT")) return game.m_lastUnlockedScenario;
    if (!std::strncmp(name, "GAME_SCENARIO_SELECT_STATUS_", 28))
        return game_core::Selectable(game.m_scenarioProgress[std::atoi(name + 28)], game.m_sandbox != 0);
    // The GUI can query the speed after unloading the old world, before loading the next one.
    if (!std::strcmp(name, "GAME_GAME_SPEED")) return env.GameSpeed().value_or(0);
    if (!std::strncmp(name, "GAME_GAME_SPEED_STATUS_", 23)) return 1;
    if (!std::strcmp(name, "PLAYLIST_MODE")) {
        const int manual = env.PlaylistManual();
        if (manual != 0 && manual != 1) return std::nullopt;
        return manual == 0;
    }
    if (!std::strncmp(name, "PLAYLIST_MODE_STATUS_", 21)) return 1;
    env.Unrecognized(name);
    return std::nullopt;
}

// Variable env (the set-variable step): Settings() as above, plus
//   void SetSeaEffects(uint8_t) / SetKeyline(uint8_t)   the renderer's and the object manager's copy
//   void SetGameSpeed(int)                             a world command
//   void PlaylistCommand(int)                          8 manual, 9 shuffle
//   void Unrecognized(const char* name)
template<class Env> void SetVariable(game::Game& game, const char* name, int input, Env& env)
{
    auto& settings = env.Settings();
    if (!std::strcmp(name, "GAME_NUDGE"))
        settings.m_nudgeScroll = input != 0;
    else if (!std::strcmp(name, "GAME_SEA")) {
        settings.m_seaEffects = input != 0;
        env.SetSeaEffects(static_cast<uint8_t>(input != 0));
    } else if (!std::strcmp(name, "GAME_LIGHTS"))
        settings.m_lightEffects = input != 0;
    else if (!std::strcmp(name, "GAME_KEYLINES")) {
        settings.m_keyline = input != 0;
        env.SetKeyline(static_cast<uint8_t>(input != 0));
    } else if (!std::strcmp(name, "GAME_SCENARIO_SELECT"))
        game.m_lastUnlockedScenario = input;
    else if (!std::strcmp(name, "GAME_GAME_SPEED"))
        env.SetGameSpeed(input);
    else if (!std::strcmp(name, "PLAYLIST_MODE"))
        env.PlaylistCommand(input == 0 ? 9 : 8);
    else
        env.Unrecognized(name);
}

} // namespace obl::game_logic
