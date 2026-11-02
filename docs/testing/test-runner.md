# Test runner (`runtests.ps1`)

`./tools/runtests.ps1` runs the entries of `tests/manifest.txt` hidden and prints a PASS/FAIL table in manifest order with
configuration and time per entry; exit code 1 on any failure. Logs: `run/testlogs/<entry>.log`.

```powershell
./tools/runtests.ps1                      # default level, half the logical cores (max 8) as game slots
./tools/runtests.ps1 -Jobs 10             # full run on a quiet machine
./tools/runtests.ps1 -Filter power* -Jobs 4   # selected entries
./tools/runtests.ps1 -Full                # also entries tagged @full
./tools/runtests.ps1 -Hidden -Filter command_*   # @headless entries in the hidden game instead (time comparison)
./tools/runtests.ps1 -Headless -Jobs 4    # only the @headless entries
./tools/runtests.ps1 -List -Filter x*     # list selected entries, no build or game
./tools/runtests.ps1 -Config Debug        # force one configuration (default Auto)
./tools/runtests.ps1 -NoBuild             # skip the up-to-date check and build
./tools/runtests.ps1 -UpdateGolden -Filter golden*   # record golden images instead of comparing
./tools/runtests.ps1 -CleanSlots          # delete run-slots/
```
Failures: Grep `FAILED|CRASH|mismatch` in the entry's log and read around the first hit.

Before running game entries, `runtests.ps1` replaces `run/packs/l0/` with `data/packs/l0/`, including with
`-NoBuild`. This refresh happens before parallel slots are prepared, so L0 source edits reach `l0_boot`; files removed from the source pack are removed from the installed copy too.

Each test process starts with fresh scenario progress: the runners remove `progress.json` (and a legacy `sp.bin`), so the game applies its built-in
fresh-install baseline. Parallel slots never copy the user's progress from `run/`. Direct `autotest.ps1` and Linux
`obl_sim_tests.py` runs snapshot and restore the existing file, including on failure; any progress the test writes is
discarded. Tests needing unlocked scenarios must set their progress themselves. A headless script batch shares this
baseline for its process; progress-dependent scripts must use `@own-process`.

Scripted and headless hosts do not persist settings (`settings_store::Save` checks `OBL_SCRIPT` and `OBL_HEADLESS`),
including direct launcher/ctest runs outside `autotest.ps1`. Music panel close, GUI options and game shutdown normally
save the current settings; playlist fixture edits or an L0 language selection must not become the next test's defaults.
A direct `music_panel.txt` run reproduced an INI rewrite without the runner's restoration. The runners still snapshot
and restore the whole file for their temporary `@gameini` overrides and failure cleanup.

Before applying overrides, `autotest.ps1` validates the installation baseline: version 1, language 5 and either no saved
playlist count (use the built-in soundtrack), or 11 complete saved track entries. An empty or partial saved playlist fails
with `Damaged test INI` and the offending keys, before misleading GUI call-count or tick-0 hash errors.
Deliberate test settings belong in `@gameini` headers.
`./tests/tools/test_ini_hygiene.ps1` covers these diagnostics and checks every INI byte after a direct hidden music-panel
run, without a runner snapshot masking a persistent write. INI serialization itself remains covered by settings unit tests.

## Levels
| Level | When | What |
|---|---|---|
| per change | every commit | `build.ps1 -Test` + `-Filter` for the change and its direct neighbours, `-Jobs 4` |
| default | broad changes, `-Jobs 10` | every entry except `@full` |
| full | `-Full`, on request | the whole manifest |

**Host.** Every game test runs in the standalone host `OpenBeach.exe`.

## Manifest entries
- `<script>.txt`: one game run. `<x>.ctest`: a ctest test (no game slot).
- `<script>.golden`: one run compared with `tests/golden-hashes/<script>.tsv` ([world-hashes.md](world-hashes.md)).
- Suffix `@l0`: the entry needs no imported data (own L0 pack or no game data).
  `-Pack public` runs only these (the set public CI runs); `-Pack private`, the default, runs every entry.
  Untagged entries are Private checks against the imported pack. `world_hash_golden.ps1 -Tables l0` maps a fixture
  `<family>_s<N>` to its L0 variant `<family>_l0_s<N>` (`golden_hash_l0_s1`); the private tables are never re-recorded.
- Suffix `@full`: only with `-Full` or a filter naming the entry exactly (golden hashes of the other scenarios).
- Suffix `@headless`: the script runs in the headless host (`autotest.ps1 -Headless`, Config column `Debug/H`;
  [../architecture/headless/scripts.md](../architecture/headless/scripts.md), "Test scripts"). `-Hidden` runs these in the hidden game.
  Entries without an `@env`, `@gameini`, `@expect-exit`, `@check`, `@expect-log`, `@audio` or `@own-process` header share a process
  (Config `Debug/HB`, "Script lists" there; `@own-process` marks a script that counts calls made at process start, which a
  batched script's `call-report` does not see): `Group-HeadlessBatches` (`testrun.ps1`) makes `batch:` schedule lines of about
  15 s of tests (all of them, at most 12 per process, with `-Jobs 1`), one configuration (`@config`) per batch; `Invoke-HeadlessBatch` splits the log per script into
  the usual result row and `run/testlogs/<name>.log`, and reruns alone a script the process did not finish (crash, hang).
  The Seconds column of a batched script is its own time in the process. The 22 `command_*` scripts: 26 s with `-Jobs 1`
  against 61 s one process each.
- Suffix `@hidden`: checked and deliberately hidden-only (the script tests drawing, the window, input or audio, which the
  headless host does not have); it runs like an untagged entry. The script header says why (`# Hidden-only: ...`). An entry
  with both `@headless` and `@hidden` is refused. Untagged game entries are not reviewed yet.
- Scenario templates: `tests/<family>_sN.txt` with `{scenario}` placeholders; an entry `<family>_s7.txt` without its own
  file is generated into the slot's `testscripts/`.

## Per-change check
Target: under about 2 minutes. Measured on a 16-thread machine, Debug, `-NoBuild`, every `@headless` entry:

| Run | Time |
|---|---|
| `-Headless -Jobs 8` (default slot count there) | 95 s |
| `-Headless -Jobs 4` | 144 s |
| `-Headless -Jobs 1` (all batched, one process at a time) | 475 s |
| `-Headless -Filter building_* -Jobs 4` (19 entries, a typical change) | 19 s |

So the whole headless set fits the target on 8 slots, and a filtered change check takes seconds. The longest entries
(`world_script_lifecycle_native`, `furniture_paths`, `construction_queries_native`, `script_program_lifecycle`: 12-14 s
each) bound a parallel run. What still needs the hidden game (`python tools/headless_inventory.py`):
- `@hidden` (13): map view pixels and draw counters of rendered frames (`architecture/headless/scripts.md`, "Hidden-only reads") and
  the three `long_game` soaks, kept as the windowed whole-game check.
- `golden` (15) and `replay` (9): long windowed hash runs; the
  `replay_*` pairs prove windowed and headless simulate the same.
- Refused directives: `frame` (35: `shot`/`expect-shot` golden images, `res`, display modes), `input` (29: mouse, keyboard,
  cursor and `object-select` contracts), `gui` (27: `gui-*` dialogs and controls driven through the GUI), `audio` (20: music,
  samples, spatial sound), `window` (7: SDL window, fatal-error and exit paths, the WinMain host), `map-view` (1:
  `map_view_blocks_native`). These test the platform layer; they stay hidden-game tests until that layer is portable.

**Simulation families.** The `replay`, `map`, `world`, `building`, `path` and `spatial` entries run in obl-sim, so both
platforms check them. The scenario 1 objectives dialog needs no dismissal headless (the world advances under it), and the
GUI save (`save-panel`/`save-confirm`/`load-slot last`) is `serial-save`/`serial-load`/`serial-cleanup` there. Left in the
hidden game, by reason:
- `replay_*` (9): each reference replay already runs in obl-sim (`headless_replay_*` ctest, `obl_sim_tests.py` replay
  entries, `headless_mismatch`); the hidden script is the windowed half of the windowed-equals-headless check.
- golden images: `building_events`, `building_events_custom_times` (also the only GUI save-panel coverage; their simulation
  runs headless in the twins `building_events_load` and `building_events_custom_times_load`), `map_view_effects_native`.
- renderer: `map_view_culling/rebuild/restore/blocks_native`, `path_tiles_rebuild` (map view block hashes, draw counters).
- input/window: `world_view_native` (SDL hotkeys, camera keys, the Shift+Alt+4 cash key, `World_Draw`).
- audio: `spatial_native`, `spatial_device_failure`, `spatial_mix` (spatial sound is the platform audio layer).

## Configuration per entry
`-Config Auto` runs `.golden` entries (long deterministic runs) on RelWithDebInfo and the rest on Debug, where
RTC checks and asserts catch more; `# @config rel|debug` in a script overrides it. The runner builds only the configurations
the selected entries need and rebuilds when the binaries are older than the sources.

## Game slots
More than three game entries run in parallel slots `run-slots/<n>/`, built from `run/` with hardlinks for read-only data
and copies of everything the game writes (`BeachLife.ini`, `openbeach.ini`, `progress.json`, an empty `SaveGames/`, own log
and captures); `golden/` is a junction to `run/golden`. Every slot is reset before every test, so results do not depend on
the slot or the previous test. Do not change files in `run/` during a run.

Flow: the longest entries start first (times from `run/testlogs/durations.json`); worker processes claim entries and run
them through `autotest.ps1` in their slot. ctest entries run beside them in up to 3 ctest processes of their own (longest
first, on RelWithDebInfo except `unit`); the host entries (`headless_*`, `standalone_*`) play in `run/` itself, where the
instance lock allows one game, so they form one serial lane. All 38 ctest entries take ~190 s that way (1318 s serial in
Debug before). Several ctest entries without game entries (`-Filter unit*`) use this path too unless `-Jobs 1` is given. An entry that failed under parallel load
is retried once alone in `run/` and reported as `PASS (passed on retry ...)` when it passes (the failed run's log stays as
`run/testlogs/<entry>.parallel.log`); `# @timeout` limits are scaled
by `-TimeoutScale` (default 2) in parallel mode.

`autotest.ps1` starts a hidden launcher asynchronously and waits on its retained process handle with the
`@timeout` deadline, including host startup. The launcher waits for its child and preserves the child's exit code.
On timeout or launch failure, cleanup stops that launcher and finds standalone hosts through CIM command lines,
matching both this checkout's executable and the exact game directory. Other slots and checkouts keep running;
ini and environment overrides are restored after cleanup. Launcher output is in the game directory's
`launcher.stdout.log` and `launcher.stderr.log`.

Runner self-test: `./tools/test_runner_slots.ps1` (slot isolation, ini hygiene, result order; outside the manifest).
The no-game timeout regression is `./tests/tools/test_autotest_timeout.ps1`: startup hangs, trap reports followed
by hung hosts in parallel, exit codes, cleanup isolation and restoration. It uses a small compiled fixture;
each worker has an outer deadline so a runner regression fails instead of hanging the self-test.
