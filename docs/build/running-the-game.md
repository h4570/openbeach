# Running the game

## Executables
```powershell
./build/x64/bin/Debug/OpenBeach.exe   [--no-wait] [--hidden] [--game-dir DIR] [--script FILE | --headless REPLAY] [-- game args]
```
- `OpenBeach.exe` is the whole game: one process that loads the converted data of the working copy.
- It reads `run/` by default (`--game-dir` selects another copy) and logs to `run/openbeach.log`.
- RelWithDebInfo (`./build.ps1 -Config release`, `build/x64/bin/RelWithDebInfo/`) plays smoother than Debug.
- `--headless <replay>` runs a replay file without window, renderer output, audio or input and prints the world hashes
  ([../architecture/headless/index.md](../architecture/headless/index.md)); `--script <file>` runs a test script ([../testing/game-tests.md](../testing/game-tests.md)).

## Hidden runs (do not disturb the user's desktop)
Automated runs should be hidden: `--hidden` or env `OBL_HIDDEN=1` keeps the window at the bottom of the
z-order, unfocused, without a taskbar or alt-tab entry (off-screen windows present nothing, so they are not used).
```powershell
./tools/capture.ps1 [-At 5,35] [-OutDir ...]   # hidden start, window screenshots at the given seconds, then stop
```
Capture restores every byte of `run/BeachLife.ini` after stopping the game, including on capture or launch failure.
Its output can be piped or redirected: the native
process writes to `launcher.stdout.log` and `launcher.stderr.log` in the capture output directory instead of holding
the caller's output handles. A launcher that does not return within 30 seconds fails and is stopped.
Run `./tests/tools/test_capture.ps1` for the no-game pipeline, redirection and launch-failure regression check.

Scripted tests run hidden automatically ([../testing/game-tests.md](../testing/game-tests.md)).

## Stopping the game
Do not kill the game by process name: other runs may be active on the same machine. Use `Stop-RunGame` / `Get-RunGame`
from `tools/common.ps1`; they match the process by the path of `<game dir>\BeachLife.exe` (or the standalone host of that
directory). The instance lock file is named by a hash of the game directory, so several copies can run side by side.

## Start-up sequence
Intro movie (skippable, `[Intro] PlayMovie`), two logo screens (left click or timeout), main menu after about 30 s
without input. Scripted runs skip the movie and run without a frame cap.

## Audio
SDL3 plays all sound and music. No `Mss32.dll` is loaded (test `samples_no_miles`).
Scripted runs use `SDL_AUDIO_DRIVER=dummy` unless a test asks for a
real device (`# @audio device`).
