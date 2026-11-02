# Headless replay lifecycle

[Headless overview and invocation](index.md). Replay syntax: [commands.md](../commands.md).

## What a replay run does (`src/testing/headless_sim.cpp`)
1. The C++ WinMain (`platform_main.cpp`) builds `Game` with a null HWND instead of creating the SDL window, calls
   `RunHeadless` instead of the message loop and returns its result. No `CoInitialize`, no intro movie, no frame cap.
2. The replay header: `start scenario <n>` or `s<n>` (campaign scenario n) or `sandbox<n>` (sandbox island n), as
   `boot-gameplay scenario|sandbox n`; or `start save <path>` (absolute or relative to the game directory). An `obl.save`
   (JSON, detected by content) loads through `load-obl-save`, an old binary `.dat` from its bytes in
   memory (the slot list reads only `obl.save`); neither needs a slot copy, and both load at the
   next main-menu frame like `boot-gameplay <N>`.
   `hash-schema` must be the build's (2).
3. `seed`, then the fixed frame time, then the new game (`BootNewGame`, shared with `boot-gameplay`); `Game_Update` runs until
   the world is settled (the `world-hash` boundary). Scenario 1 takes 7 frames.
4. Tick T: the world hash where the replay has a checkpoint at T (and always at 0 and at `end`), the commands of T through
   `SubmitCommand`, one `Game_Update`. Each update must advance the command loop's tick by one in gameplay; anything else
   fails the run. `Game_Render` never runs.

The tick, checkpoint and command semantics are those of the game's command loop, so a script that does the same in the
windowed game (`seed`, `fixed-dt`, `boot-gameplay`, `await gameplay`, `command ...`, `step N`, `world-hash`) produces the same
hashes: `headless_s1.replay`'s checkpoints were taken that way. The script's `await gameplay` frames render; the match shows
rendering does not feed back into the simulation (for scenario 1 with these commands).

The own L0 replays (`tests/replays/l0_commands`, `l0_construction`, `l0_gui`, `l0_rejected`, [replays.md](../../testing/replays.md))
run headless as `ctest headless_replay_l0_*` with `OBL_PACK=l0` and need no imported data; `headless_mismatch` (and the windowed
`mismatch`) are cut from `l0_commands` and run with the same environment. `tools/obl_sim_tests.py` sets the variable for the
`l0_` stems.

`ctest headless_save` (manifest `headless_save.ctest`) starts `tests/replays/headless_save_s1.replay` from the save that
`ctest headless_save_fixture` writes headless (scenario 1 after 300 ticks, `serial-save testlogs/headless/save_s1.obl.save`, the save panel's writer);
its hashes were taken in the hidden game by a fresh process loading the same save (`boot-gameplay 9000`), and re-recorded
headless when the frame that loads a save stopped updating the world: the loaded world's tick 0 is the frame after the load
(`Game_Update`; a save and reload is now a no-op, checked by `ctest headless_chaos_sandbox1_save`).

**A save start is a fresh-process load, and both simulate the same as a load after play** (`ctest headless_save_reload`:
`serial-save`, `serial-load` in one process reach `headless_save_s1.replay`'s hashes). Two things keep them equal; without them they differ from tick
150 to 300 in FeedbackManager's notices (pending list against history) through the GUI manager, which lives as long as the
process:
- `GuiManager_Construct` leaves mode 4 (default) current without its mode object. A save loaded before the manager's first
  update (a fresh process loading at once; in the game the menu frames switched to mode 6 first) requested mode 4 again, so
  `SwitchMode` built nothing; the default mode's update is what scrolls the message balloon, so the balloon never freed and
  every later notice waited. `SwitchMode` now also builds the current mode's missing object.
- A balloon still scrolling when the world was left kept scrolling in the next world and held back its first notice.
  `LeaveGame` (WorldTest exit and destructor) now drops it (`gui_manager_messages::DropMessage`).

The saved bytes differ between processes (a fixed-size text field carries leftover heap bytes) while the loaded worlds hash the same.

