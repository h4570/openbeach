#pragma once

#include "core/char16.h"
#include <cstdint>

namespace obl::game {
struct GameParameters;
struct GameClock; // game/game/game.h
} // namespace obl::game
namespace obl::scenario_load {
struct Campaign; // game/world/scenario_load.h
}

namespace obl::game {
struct Game; // game/game/game.h

// The whole hour and the quarter (0/15/30/45) of a day time, counted in `call-report GameCoreCalls`.
int GameTimeWholeHour(float hour);
int GameTimeQuarterMinutes(float hour);
// The campaign order and start availability from `packs/base` (scenario_load::LoadCampaign); stops the game when it cannot be read.
void LoadCampaignOrFatal(scenario_load::Campaign& out);

// An outer wrapper of the frame update or render (the test driver, the frame limiter): game_api::Update/Render call `wrapper`,
// and `*body` receives the frame body it calls in turn. Registration fails if the slot already has a wrapper.
using GameFrame = void (*)(void* game);
bool WrapGameUpdate(GameFrame wrapper, GameFrame* body);
bool WrapGameRender(GameFrame wrapper, GameFrame* body);
// Called with the renderer after the frame's present (the autotest's screenshots and golden capture).
using PresentObserver = void (*)(void* renderer);
void SetGameRenderPresentObserver(PresentObserver observer);
// The clock update works the same way; the test's fixed frame time wraps it.
using GameClockStep = void (*)(game::GameClock* clock, uint32_t tick);
bool WrapGameClockUpdate(GameClockStep wrapper, GameClockStep* body);
} // namespace obl::game

namespace obl::game::game_api {

// The context's Game, built and then checked (`call-report GameCoreConstruct`); Destroy saves the progress, ends the members
// and frees it (no Game is a no-op). `instance` and `window` are the host's opaque module and window handles (window null: headless).
game::Game* Create(void* instance, void* window);
// Create in two halves around the intro movie, for a host whose main loop runs the movie frames (IntroMovieFrame,
// game_host.h) between them; Create runs the movie to its end itself.
void BeginCreate(void* instance, void* window);
game::Game* FinishCreate();
void Destroy();
// One frame step; an outer wrapper registered through WrapGameUpdate/WrapGameRender runs first.
void Update(game::Game* game);
void Render(game::Game* game);
void OnActivate();
void OnDeactivate();

void SetNextState(int state);
void ChangeResolution(int width, int height);
bool LoadGuiScript(const char* script);
void DrawFullscreenTga(const char* path);
// The GAME_* GUI variables of the options windows (decimal text out, integer in).
void GetVariable(const char* name, Char16* out);
void SetVariable(const char* name, const int* value);
float Version();

bool ReturnToMainMenu();
void RequestReturnToMainMenu();
void CancelReturnToMainMenu();
void SetSandboxMode(bool sandbox);
void LoadScenarioProgress();
void ResetScenarioProgress();
void SaveScenarioProgress();
// The current scenario won (the scenario-end check): its progress marked and the next one unlocked, then saved.
void CompleteCurrentScenario();
int ScenarioCount();
const Char16* Scenario(int index);
bool IsScenarioCompleted(int index);
bool IsScenarioAlwaysAvailable(int index);
// The Game's clock from zero, as after construction (the headless script list between two scripts).
void RestartClock();
void InitSpeedTable();
// The profile timer around WinMain's update and render; Stop returns the elapsed ticks / 1000 for GameClock::m_updateMs/m_renderMs.
void StartProfileTimer();
int StopProfileTimerMs();
void StartStopProfileTimer();
// The `call-drive GameCoreClocks` and `GameCoreSpeed` drivers.
bool RegisterTestDrivers();

} // namespace obl::game::game_api

namespace obl::game::game_parameters {
void* Construct(void* list, const char* path);
void Destruct(void* list);
// The entries of a parameter list (built from rules/game_parameters.json) by type as C++
// calls: false and `out` untouched when the key is missing; a string is the stored pointer. They count `call-report GameParameters.calls`.
bool GetBool(const void* list, uint8_t* out, const char* key);
bool GetInt(const void* list, int32_t* out, const char* key);
bool GetString(const void* list, const char** out, const char* key);
// The Game's parameter list (game::Context::gameParameters); null outside the Game's lifetime. The Game's construction sets it
// before the entries are read and clears it when the list is destroyed.
const void* Current();
void SetCurrent(game::GameParameters* list);
} // namespace obl::game::game_parameters
