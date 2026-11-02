# Scripted game tests

A test is a command script in `tests/<name>.txt` executed inside the hidden game (`--script`, interpreter in
`src/testing/autotest.cpp`; its header comment lists every command). Script mode skips the intro movie, runs without a
frame cap and starts at the first `Game_Update`.

`no_dgvoodoo.txt` enables the intro and checks completion without skipping, the full decoded frame count,
and the consumed audio seconds with the audio clock still pacing the video. `intro-movie-seconds` reports wall time;
it is diagnostic for full playback because an SDL audio device can drain slowly. The skip test still uses wall time
to check its wall-timed Escape injection.

## Validation scope
Validate in proportion to the change: run the affected tests only (`./tools/runtests.ps1 -Filter power*` or one script),
build when compiled code changed, add focused coverage for new behavior. Docs-only changes need no game tests. Run the
full suite only for broad changes (shared test infrastructure, frame timing, renderer, world or save lifecycle) or when
asked.

## Running one script
```powershell
./tools/autotest.ps1 tests/<script>.txt [-TimeoutSec 120] [-MaxFps n] [-GameDir DIR] [-UpdateGolden] [-Headless]
```
It starts the game hidden, waits for the script to quit, prints the relevant log lines, elapsed time and a PASS/FAILED
summary, and throws on any `FAILED`, `FatalError`, crash or timeout. Do not run two at once in the same `run/`.
`-Headless` runs it in the headless host instead (no window, frames, GUI input or audio; directives that need them are
refused): the way for simulation tests, tagged `@headless` in the manifest ([../architecture/headless/index.md](../architecture/headless/index.md),
"Test scripts").

## Script headers (comment lines, anywhere in the file)
| Header | Effect |
|---|---|
| `# @timeout s` | wall-clock limit (`-TimeoutSec` wins) |
| `# @env NAME=VALUE` | environment variable for the game (an already set variable wins; restored afterwards) |
| `# @maxfps n` | frame cap; only tests of the limiter need it |
| `# @gameini Section.Key=Value` | value written into `BeachLife.ini` before the start, on top of the baseline (800x600, no `[Dialog]`) |
| `# @audio device` | real audio device instead of `SDL_AUDIO_DRIVER=dummy` |
| `# @check python tools/x.py {run}` | oracle run after a passing game run; non-zero exit fails the test |
| `# @expect-exit code` | the game must end by itself with this exit code before `quit` (fatal-error paths) |
| `# @expect-log REGEX` | some log line must match (repeatable) |
| `# @config rel` / `debug` | configuration for this entry under the runner's `-Config Auto` |
| `# @own-process: why` | an `@headless` entry runs in its own process, not in a batch: the script counts what only the process start does (`Game_Construct`, the startup scans) |

After every run the whole `BeachLife.ini` is restored byte for byte and saves the run created are deleted, so a test never
sees what an earlier test or a manual session left behind.

## Writing tests
New tests use the fast flow, not clicks and wall-clock waits:
- `boot-gameplay [slot|sandbox [n]|scenario [n]]` goes straight from the intro into gameplay or a save.
  It also reloads gameplay within one script; `boot_gameplay_reload.txt` covers scenario 1 followed by scenario 3.
  The loading state suspends updates and drawing of the previous GUI while its world is absent;
  the GUI's `GAME_GAME_SPEED` query returns level 0 until the new world is available.
- `await gameplay|world-loaded|frames N|hour H|hours N|<probe> <op> <value> [timeout_ms]` waits for a condition; a missed
  timeout is `FAILED` and aborts the script. After `load-slot` use `await world-loaded`.
- Assertions: `expect <name> <value> [tol]`, `expect-range`, `expect-last <key> <value> [tol]` (a `key=number` of the latest
  `autotest:` report line), `expect-last-range`, `expect-same <keyA> <keyB>`.
  `expect-text <key> <UTF-8 value...>` checks a loaded text-table value; `expect settings-language <n>` checks the requested setting, `expect settings-shown-language <n>` the language on screen.
- Time-dependent tests start with `seed 1` + `fixed-dt 0.02` and advance game time with `step N`
  ([determinism.md](determinism.md)); without fixed time assert ranges, not exact values.
- World fixtures after `await gameplay`: `spawn-building <NAME> <tx> <ty>`, `remove-building [id]`, `set-cash <n>`,
  `set-hour <h>`, `spawn-guests <n>`; then `step` so start-up messages are processed.
- Speed: `speed <x>`, `speed-level <0..2>` (8x is the game's top level). Campaign scenarios stop at a day limit; a longer
  run first does `world-script-change STATE_WORLD_AFTERGAME`.
- `$key` in any directive's arguments (not `await`) is the value of a probe or report key when the directive runs, so ids
  found by one directive feed the next: `object-find building GENSTORE` then `object-report $id`.
- **Commands** (player input, [commands.md](../architecture/commands.md)): `command <text>` submits one command in its text
  form, e.g. `command set-price building=$id slot=0 price=3.25`; it runs at the start of the next gameplay frame, so `step 1`
  before checking. `command-report` gives `applied=`/`rejected=`/`dropped=`/`tick=`; `object-find` (`building|scenery <KEY>`, `staff <Class>`, `guest * [n]`), `place-find` (a spot
  where a footprint fits, `place-find surface <KEY>` a tile to paint, `place-find dozer` a tile to clear), `object-report <id>`
  (building and staff fields) and `tile-report <x> <y>` (grid flags) find and read the objects (`src/testing/commands.h`).
  Probes `income-today`, `demolish-income-today`, `costs-today`, `scenery-costs-today` read the day's money counters.
  Scenario 1 surface keys: `GRASS02` (30), `PATH1` (15). Prebuilt objects of scenario 1: HOTEL, POWERGEN, BUILDERYARD (6 of 8 builders), GENSTORE;
  `spawn-building` creates a construction site, which rejects the building commands.
- **Replays** ([replays.md](replays.md)): `record <file> [every N]` ... `record end` records the commands of a run with world-hash
  checkpoints; `replay <file>` boots the file's start state, feeds its commands and blocks until its last tick.
- Older commands still work: `wait ms [frames]`, `move x y`, `click x y [right]`, `down`/`up`, `shot name`, `res w h`, `log`, `quit`.
  Coordinates are game (renderer) pixels.
- A new test is a script plus one line in `tests/manifest.txt` ([test-runner.md](test-runner.md)); tests must not depend
  on files or ini state left by other tests.

## Crashes and hangs
In script mode an unhandled exception logs `autotest: CRASH` lines (code, address, registers, stack candidates) and
`autotest.ps1` reports `game crashed`. A watchdog ends a run whose frame loop stops for 20 s and logs samples of every
thread (`code=HANG`); a thread waiting on a lock in `ntdll` is not the loop, look for the lock owner in the other stacks.
The watchdog starts with the first beat; the headless hosts (replay, bot, chaos) beat once per `Game_Update`, a save load
beats too and allows 120 s. `OBL_HANG_MS` overrides the 20 s (the `headless_chaos_sandbox1_save` ctest uses 5000). Samples
spread over unrelated code (not one loop) mean the thread was busy, not stuck: look for a missing beat.
Addresses in the game are resolved from the linker map (`$env:LINK='/MAP'` at build time).
`./tools/test_crash_diagnostics.ps1` checks these paths with deliberate crashes (outside the manifest).
