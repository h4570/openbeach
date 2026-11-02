# Linux windowed game tests

The windowed game scripts of `tests/manifest.txt` (the lines without `@headless`) on the Linux windowed `OpenBeach`
(`-DOBL_LINUX_WINDOWED=ON`, [linux-host.md](../architecture/linux-host.md)).

## Running
```
python3 tools/obl_sim_tests.py --windowed [--game-dir DIR] [--sim BUILD/bin/OpenBeach] [--filter PATTERN ...] [--full]
```
Without `--sim` it builds `obl-sim` and `OpenBeach` with `tools/obl_sim_linux.sh -DOBL_LINUX_WINDOWED=ON`. Each
script runs in its own process, `OpenBeach --game-dir DIR --script <script> --hidden`: `--hidden` selects SDL's
offscreen video driver (`hidden_window_sdl.cpp`), and the runner sets `SDL_AUDIODRIVER=dummy` unless the environment
already names a driver, so neither a display nor a sound device is needed. Judging, ini/save hygiene, `@gameini`, `@env`,
`@check` and worker game directories are the headless runner's; logs go to `<game dir>/testlogs/linux-windowed/`.
`@full` scripts run only with `--full`. Left out: `# @windows-only` scripts and those with
`# @linux-windowed-skip <reason>`.

From WSL use a game directory on the Linux file system: over `/mnt/c` the game start alone takes about a minute
(sprite reads through 9P), and most scripts then miss their `@timeout`. A copy of `run/` without `tmp/`, `testlogs/`,
`world-hashes/`, `replay-record/` and `captures/` is about 2 GB:
```
rsync -a --delete --exclude tmp --exclude testlogs --exclude world-hashes --exclude replay-record --exclude captures \
    /mnt/c/<checkout>/run/ ~/obl-game/
OBL_SIM_BUILD=~/obl-win OBL_SIM_SKIP_CONVERSION=1 bash tools/obl_sim_linux.sh -DOBL_LINUX_WINDOWED=ON
python3 tools/obl_sim_tests.py --windowed --game-dir ~/obl-game --sim ~/obl-win/bin/OpenBeach
```
The `@check` oracles need `python3-pil` (the WSL image has no pip; their frame checks passed when run from Windows on the
Linux captures). CI (job `windowed`, [ci.md](../build/ci.md)) runs the same set; it has no `run/golden/`, so every
`expect-shot` there logs SKIP. The private set takes its references from the data package and fails on a SKIP.

## Skipped scripts
`# @linux-windowed-skip <reason>`, all of one kind:
- "Win32 test tool or probe": the command or probe exists only in a Windows test tool (`test_tools/cursor.cpp`,
  `crash.cpp`, `expected_exit.cpp`, `windowed_display.cpp`, `profile.cpp`; `test_tool_stubs.cpp` answers for them) or
  reads Win32 window state (class, style, capture, monitor). `test_tools/presenter.cpp` is built into the Linux windowed
  host: posted keys become pushed SDL events, mouse bursts posted ones (the next pump pushes them), `window-size` is `SDL_SetWindowSize`, and of the window
  probes only the client size, title and windowed flags answer.

A real host difference gets no tag: it is fixed.

## Host differences
- Map cache: `ui::CreateMemoryMapping` was a stub off Windows, so every gameplay boot stopped with "Could not build the
  in-memory map cache"; non-Windows hosts now keep the cache in one heap block (`src/platform/memory_mapping.cpp`).
- `quit` and `close-window` post the close request with `PostSdlGameWindowClose` off Windows (Windows posts `WM_CLOSE`).
  It is a flag the next `PumpSdlEvents` takes, not a pushed SDL event: the input adapter drains the window events during
  the frame, before the next pump, and the game never ended.
- `expect-shot` read and wrote its PNGs through SDL with the game's `\` paths; they go through `WidePath` now.
- The renderer presented nothing off Windows (a guard from the headless-only Linux build); `sdl-verify` waited forever.
- Window placement on resolution changes was Windows-only, so the window kept its first size. On the offscreen driver
  (a fixed 1024x768 display) the window is placed on the 1920x1080 desktop the resolution list is built for.
- The offscreen driver's GL surface keeps the window's first size, so after a resize the read-back no longer matched;
  the presenter uses SDL's software renderer there. That renderer's nearest sampling resolves exact ties (every third
  pixel at 1.5x) to the left/upper pixel, so `sdl-verify` accepts either neighbour at a tie.
- `posted-mouse-burst` pushed its SDL events at once, so the same frame's input drain took them, a frame earlier than
  posted Windows messages; in `input_messages*` the intro then ended a frame earlier and its state switch cleared the
  second press. The burst is posted now (`PostSdlEvent`: the next pump pushes it).
- The GUI constructor corpus writes `heap:?` without a heap block size (Linux); the checker then compares null /
  non-null only.
- The ground/overlay map fixtures (`sprite_ground`, `sprite_overlay`) drew their random stream in an unsequenced
  expression, which clang and MSVC evaluate in different orders.
- The runner now follows `autotest.ps1`: `@maxfps` becomes `OBL_MAXFPS`, the old `[Dialog]` section goes before the
  script's `@gameini` values (not after), and the legacy `sp.bin` is hidden with `progress.json` (it unlocked every
  island in the sandbox menu).
- Not a host difference: `gui_album` clicked the camera button again before its 0.1 s click and recovery ended, which
  a fast uncapped host reaches within a frame; the script waits 0.1 s on each hover. `frame_limit` judges
  `limiter-paced-fps` (the limiter's rate without the time frames overran their deadline), so a starved host
  (a 16-job run measured 27 FPS wall-clock) no longer fails it.
- Not a host difference either: a busy runner ran every script about 1.7x slower. Scenario 1 opens
  `Objectives` by itself soon after the start, and the long first frames reached it before `await gameplay`, so
  `gui_script*` run on `fixed-dt 0.02`; `resolution_height` (about 1000 frames at up to 1280x960) got a 120 s budget.
- The offscreen driver has no pointer: on focus gain the input adapter took SDL's mouse state (0, 0) as the cursor, and
  edge scrolling moved the camera up-left by about 60 pixels a frame. The position is not restored on that driver.

- Frames at zoom 0 differed in the two top-left pixels: the doubling present blended the first scene pixel with the cell
  before the scene buffer, whatever each host's heap held there. It blends with itself now.

## Playlist count
A run can save `Sound.PlayListCount=12` where another wrote 11. This is not a scan difference: the playlist starts from the saved `PlayListTrack<N>` entries of `BeachLife.ini` (`playlist::ScanFolder`), and `music/` is
empty, so the count is whatever the game directory's ini held; without saved entries the twelve built-in soundtrack tracks
are added (`AddSoundtrack`, shared code). Scripted runs never save the ini.
