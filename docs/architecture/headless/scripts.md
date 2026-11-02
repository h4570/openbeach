# Headless test scripts

[Headless overview and invocation](index.md).

## Test scripts
`--headless <file>` with anything but a `.replay` file runs a test script (`docs/testing/game-tests.md`): the launcher sets
`OBL_SCRIPT` as well, the script interpreter (`autotest.cpp`) installs as in the hidden game and runs at the start of every
`Game_Update`. The headless loop (`RunScript` in `headless_sim.cpp`) runs `Game_Update` frames until the script reaches
`quit` or aborts; `Game_Render` runs only where a state switch, a save load or a GUI reload needs it (the rule of `step`),
so a script typically renders twice (the start of the world). Report values are the hidden game's: the ported
`command_*` scripts log the same `autotest:` values in both modes, only wall-clock `await ... met after N ms` differs.
```
obl-sim: script C:\...\tests\command_hire_staff.txt
obl-sim: end frames=70 renders=2 passed=5 failed=0 refused=0 completed=1 ms=1327 sdl-subsystems=0
obl-sim: PASS
```
Exit code 0 = `quit` reached without FAILED lines, 6 = `quit` with FAILED lines, 5 = aborted (a missed `await`, a refused
directive).

**Refused directives.** Directives that need input devices, a presented frame, the GUI, audio or a window (`move`, `click`,
`shot`, `expect-shot`, `res`, `object-select`, `close-window`, `fatal-error`, the `gui-`, `sdl-`, `music-`, `sample(s)-`,
`spatial-`, `window-`, `display-`, `posted-` families and the input/cursor contracts; `RefusedHeadless` in `autotest.cpp`)
are refused when the script loads, and so are the renderer-only probes `map-blocks` and `map-hash*` in any directive (`expect`,
`await`...; `RenderOnlyProbe`): headless keeps no map view block cache, every block view is one scratch buffer. Each refused
line is logged `autotest: FAILED headless refuses <name> (line N)` when the loop
starts, and nothing of the script runs. Other probes stay readable. `ctest headless_refuse` checks it (`tests/unit/headless_refuse.txt`).
Not handled yet: a game that leaves by itself (`next-state 3`) keeps the loop running until the runner's timeout.

**Hidden-only reads.** Some state is not simulation and differs headless by design; tests that check it carry `@hidden` in
the manifest ([test-runner.md](../../testing/test-runner.md)): the map view blocks (`map-hash`, refused above:
`path_tiles_rebuild`, `map_view_rebuild/restore_native`, `sprite_overlay/bake/ground_native`, `scenery_placement_native`),
the draw counters of rendered frames (`map_view_culling_native`, HUD text `gui_variable`, `shark_draw_native`) and what
only the audio side calls (LoopSounds is built disabled: the building overlay offset query has no other
reader, so its counter sits in `loop_sounds`). Call counters of render paths count only the transition renders, so a
test mixing simulation and a draw counter is split: `shark_serial_native` (save/load, headless) and `shark_draw_native`.

**Script lists.** `--headless <name>.list` runs several scripts in one process (one path per line, `#` comments, relative to
the list); the ~0.3 s process and `Game_Construct` start is paid once. Each script reports its own `script`, `end` and
`PASS|FAILED` lines, then a summary `obl-sim: scripts=N passed= failed= aborted= ms=`; exit code 5 when any script aborted,
else 6 when any failed. Between two scripts the host (`LeaveForNextScript`) switches to the Intro state a fresh process starts in
(WorldTest's exit destroys the world and drops the message balloon) and restarts the game clock (a world stores the clock time it
was built at, `World::m_fGameStartTime`); `StartNextScript` (`autotest.cpp`) resets the interpreter and what a script reads
back: report values, PASS/FAILED counts and frames, `seed`/`fixed-dt`, `await`, `world-hash` numbering, the
`replay` directory, a scratch save slot left behind, the `fail-create` buildings (pointers into the world just destroyed;
a later breakdown script counted them and aborted on Linux), the command loop's totals and the call counters (`call-report`
counts the script's own calls; without the reset four scripts that expect exact counts failed in a Linux batch).
`ctest headless_batch` runs
`headless_save_reload` before and after another script's world with the same expected hashes. The 22 `@headless` scripts
batched log the same lines as alone (37 s against 61 s in Debug), except `object-report` fields the game never initializes
(unused price slots, a staff member's `direct-x/y` before any order), which show whatever the heap held. Other process-wide
state is not reset (module caches); a script that reads it belongs in its own process. So does a script that
counts what only the process start does, since the counters restart at zero for the next script: the header `# @own-process`
keeps `playlist_scan`, `fonts_construct` and `text_table` out of batches (they failed whenever they
were not first in a batch, which looked like flakiness under parallel load). A batched run that differs from the run alone is
leftover state of an earlier script: `building_breakdown` counted the previous script's `fail-create` buildings, and
`command_object_controls` saw a mechanic's job mask with heap bits (fixed in the constructor, `staff_construct_job_mask`).

**Runner.** Manifest entries tagged `@headless` run through `autotest.ps1 -Headless` (`runtests.ps1`, Config column
`Debug/H`), several per process as `Debug/HB` ([test-runner.md](../../testing/test-runner.md)); `runtests.ps1 -Hidden`
runs them in the hidden game instead. A script qualifies for `@headless` when it
passes in both hosts, also batched. `python tools/headless_inventory.py [--list]` sorts the rest by why they still need the
hidden game ([test-runner.md](../../testing/test-runner.md), "Per-change check"). `replay_*` stay in the hidden game:
with `ctest headless_sim` they show that the windowed and the headless game simulate the same.

**Time per test** (Debug, sequential `-Jobs 1`, the 21 `command_*` scripts without `command_record`): headless
61 s (2.4-3.2 s each, `command_set_event_price` 8.1 s) against 100 s in the hidden game (3.5-6.8 s, 11.2 s). The rest is
the floor of every game start: process and `Game_Construct` (~0.3 s), the world start of `boot-gameplay` (~1.4 s in both
modes; it builds the map view's block cache), `autotest.ps1` around it (~0.7 s). Rendering was the difference:
`await frames 60` takes ~70 ms headless against ~800 ms hidden. Script lists ("Script lists") save the process start; the
world start of each script remains (see the [platform inventory](platform.md)).

