# Headless simulation

The Windows standalone host and Linux `obl-sim` run the same simulation without a window, renderer output, audio or
input devices. Replays drive commands and check world hashes; test scripts run simulation directives, and scenario
bots use the same headless lifecycle ([bots/](../bots/index.md)). Linux build and ABI details are in
[linux-host.md](../linux-host.md).

| Topic | Summary |
|---|---|
| [scripts.md](scripts.md) | Test-script execution, refused device directives, hidden-only reads, script lists and their reset contract. Includes runner selection and measured startup costs. |
| [replays.md](replays.md) | Replay boot and tick ordering, checkpoint parity with the hidden game, and save-start behavior. Explains the GUI message lifecycle needed for identical save reloads. |
| [platform.md](platform.md) | Headless switches in game code and the measured Windows import inventory. Lists portable replacements and the objects still built in memory without devices. |

## Running
```powershell
./build/x64/bin/Debug/OpenBeach.exe --game-dir run --headless tests/replays/headless_s1.replay
```
`--headless <replay>` sets `OBL_HEADLESS` (absolute path) for the DLL; it excludes `--script` (a script runs as
`--headless <script>`, [test scripts](scripts.md), or `--headless <name>.list` for several, [script lists](scripts.md)). A `.bot` file is a
scenario bot run ([bots/](../bots/index.md)). Output on stdout (and in the
game log), one `obl-sim:` line each:
```
obl-sim: start s1 seed=1 fixed-dt=0.02 end=600 commands=5 checkpoints=4
obl-sim: boot frames=7 ms=1396
obl-sim: @0 hash=ba013df8f1d1eaa4 fields=47105 expected=ba013df8f1d1eaa4 match
obl-sim: end ticks=600 hash=17c9b279084bad22 checkpoints=4 mismatches=0 applied=4 rejected=1 ms=1651 ticks-per-s=363 sdl-subsystems=0
obl-sim: PASS
```
Exit code 0 = every recorded checkpoint matched, 6 = a mismatch (`MISMATCH` on its line), 5 = the run could not proceed
(unreadable replay, unsupported start, the world did not settle or left gameplay); the host's own codes stay
([running-the-game.md](../../build/running-the-game.md)). `ctest headless_sim` (manifest `headless_sim.ctest`) runs
`tests/replays/headless_s1.replay`.

A game `FatalError` ends any headless run with exit code 5 and `obl-sim: FAILED fatal-error <formatted text>` on stdout
and in the log, including errors during startup or teardown. Bot and chaos runners use this line to name the cause
when no normal end line is produced. GUI runs retain the game's exit code 0.

