#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace obl::game {
struct Game; // game/game/game.h
}

namespace obl::test {

// What the menu's new-game buttons do: sandbox or campaign mode, island/scenario `island` (1-based), then the LoadGame state, which
// enters gameplay on a later game_api::Update.
void BootNewGame(game::Game& game, bool sandbox, int island);

// Commands that drive the game flow: boot-gameplay, speed, speed-level, speed-table.
bool ExecuteFlowCommand(const std::string& name, const std::vector<std::string>& args);
// The scenario number of the last `boot-gameplay scenario <n>`; 0 when the last boot was a sandbox or a save, or none ran.
int LastScenarioBoot();

enum class AwaitResult { Done, Waiting, Failed };

// One step of `await <condition> [timeout_ms]`; call every frame until it stops returning Waiting.
// Failed means the timeout expired (the FAILED line is already logged) and the script must stop.
AwaitResult StepAwait(const std::vector<std::string>& args, uint32_t now);

// Called at the start of every game_api::Update, before the script runs.
void OnFrame();

// A save load was requested from inside gameplay: `await gameplay|world-loaded` must wait for the new world, not
// pass on the frames of the old one that still run before the state switch.
void NotifyWorldReloading();

// Before the next script of a headless script list: no await in progress, no scenario boot seen.
void ResetFlow();

} // namespace obl::test
