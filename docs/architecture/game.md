# `Game` and the state machine

This module owns the top-level `Game` object (`src/game/game/game.h`), the `Settings` it owns, and the seven game states.
`obl::game::Context` (`game_context.h`) supplies shared services to state transitions and world creation.

## What the `Game` holds
- **The state machine:** the current and next state (`Game_SetNextState` writes only the next one; the update switches)
  and `m_states`, a `std::array<std::unique_ptr<GameState>, 7>` in `GameStateId` order (WorldTest, MainMenu, Intro,
  ExitGame, ResetGame, LoadGame, GameWon).
- **Owned subsystems:** cursor, input, clock (`GameClock`), profile timer, renderer, debug font, text table, GUI manager,
  sound, `Settings`, sprite bank.
- **Front-end state:** scenario list and progress, sandbox flag, save-slot list and selection (`save_slots.cpp`),
  return-to-menu, GUI reload and suspend flags.

Neither the `Game` nor the states are in a save or the world snapshot. Changes to the World's lifetime still run the
replays as a regression net.

## Facade
`src/game/game/game_api.h` (namespace `game::game_api`). Bodies are in `game.cpp` and act on the context's Game
(Construct/Destruct/Update/Render take the Game); each call counts `GameCoreCalls`. The parameter list's bodies are in
`game_parameters.cpp` (`GameParameters.calls`), the states' live env in `game_states.cpp` (`GameStates.calls`).
`game_core::` is the pure progress logic. `game_api::RegisterTestDrivers` registers the `GameCoreClocks` and `GameCoreSpeed`
drivers at start-up. Save-panel operations are `ui::save_panel::` in `save_slots.h` (`obl::save_slots` is the pure slot
logic).

`Update`/`Render` run the outer wrapper registered with `WrapGameUpdate`/`WrapGameRender` (test driver, fixed frame time).
`Update` calls `save_panel::LoadPendingSave`/`UpdateLoadInfoBox` on the Game it updates.

## Settings
`game::Settings` (`src/game/game/settings_store.h`, bodies in `settings_store.cpp`) reads `[Beach Life Options]` in
`BeachLife.ini` over the pure `settings_ini.h`. It holds `std::string` ini path and provider,
`std::vector<playlist::SavedTrack>` playlist and `std::vector<settings::DialogPosition>` dialog positions; defaults are
member initializers. The operations are `Save`, `SetResolution`, `GetResolution`, `ClearDialogPositions`,
`StoreDialogPosition`, `FindDialogPosition`, `ClearPlaylist`, `AddPlaylistTrack` and `ResetPlaylist`. Readers call
`settings_store::Current()` (`Exists()` for probes before boot).

## States
- `game::GameState` (`src/game/game/game_state_base.h`) has virtual Update/Render/PostRender/Enter/Exit/GetGuiScript/
  ChangeResolution; the defaults do nothing, `Exit` clears `m_entered`.
- The six small states are env template classes in `src/game/game/game_states.h`; the pure functions in
  `game_states_logic.h` are templates on the state type (`game_states_test` uses a probe and instantiates the classes with
  a recording env). `game_states.cpp` supplies the live effects. GameWon keeps its page widgets in a `std::vector`.
  `CreateGameStates` builds all seven in a fixed order.
- `WorldTestState` (`world_test_state.h`) owns the World through `std::unique_ptr` with a `world::Destroy` deleter;
  `FinishLoad` and `m_loadingSave` are what the save loader calls and sets.

## Members
- Scenario list: `std::vector<std::u16string> m_scenarios` (the names of the scenario screen, UTF-16 like the GUI text);
  `game_api::Scenario` returns the string's buffer. Progress is `int m_scenarioProgress[12]` (the pure `game_core` and
  `progress_file` functions take the array).
- Save slots: `save_slots::Slot` (`src/game/ui/save_slots.h`) records owned by
  `std::vector<std::unique_ptr<save_slots::Slot>> m_saveSlots`; `m_selectedSaveSlot` is a `const Slot*` into it. The
  `save_slots.h` sorts copy their elements, so `save_slots.cpp` sorts the raw pointers and hands them back (`Reorder`). The
  fixtures' `GameCopy` gives the copy its own empty slot list and frees it in `Release`; the copy's other members stay the
  live Game's and are never destroyed.
- Clock and profile timer: `GameClock m_clock` and `ProfileTimer m_profileTimer` are value members; `GetGameClock()`
  returns the Game's clock. The update calls the clock update directly, through the wrapper `WrapGameClockUpdate` registers
  (the test's fixed frame time). The hosts use `game_api::StartProfileTimer`/`StopProfileTimerMs`/`StartStopProfileTimer`/
  `RestartClock`.
- Platform handles: the Game keeps none. Construction takes them from the host, passes them to the renderer, input, cursor
  and intro movie, and registers the window for error boxes (`SetErrorWindow`).

## Victory credits
Victory pages use `gui_content::CreditRows()`: the pack's credit rows followed by OpenBeach's own localized block. If GUI
content is not loaded, that view contains only the own block. `own_credits::Merge({})` produces a complete standalone roll.

The 800x600 page layout has three 224-pixel columns at x=40, 288 and 536, with twelve 30-pixel rows starting at y=160. Each
page holds 36 entries; the page count is computed from the content (at least one). Row fonts, keys and colors come from the
pack and own rows; roll-specific group gaps do not affect the page grid. Background dialogs cycle through the pack's nine
`GameWon` backgrounds. The final page waits for a click, and the preceding pages advance on a timer.
