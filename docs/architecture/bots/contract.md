# Scenario bots: contract, win and loss, run files

## Contract
```
   live world --CaptureWorldView--> WorldView (copy) ---+
   live world <--Queries (read-only checks)-------------+--> Bot::Think --> commands --> SubmitCommand --> tick T
```
- `Bot::Think(view, queries, out)` runs every `ThinkInterval()` ticks (default 25 = 0.5 s of frame time at
  `fixed-dt 0.02`) from tick 0, before that tick's `Game_Update`. The commands it appends apply at the start of
  `view.tick`, in order, exactly like a replay's commands of that tick, so a bot run is a replay whose commands are
  computed. The host prints each as a replay body line (`obl-sim: @T <command>`).
- **WorldView** (plain values, no pointers into the game): tick, day and hour, cash, guests, resort rating, pause, game
  speed, map size, the script's objective flags (`ScenarioObj<n>`), the outcome so far, every building (id, key, tile,
  construction/demolition job, built by the player), the scenario's building types (cost, footprint size, `BUILDON`,
  power, predecessor in the tree, `allowed`/`lockedByTree` as the build panel computes them, finished and total
  instances), the guests' mean beauty (`beauty`), the scenic sum (`scenic`), the script's drunk count (`drunk`) and the staff count per `hire-staff` profession. New fields are added when a bot needs them.
  `BuildingType::staffCapacity` is the number of places per staff home; `WorldView::staffIds` copies each profession's
  live member ids in object-list order so a bot can adjust individual wages and shifts through commands.
- Swimming safety: `WorldView::beachAreaCount` counts the map's beach/swimming areas; `Building::beachArea` is the
  building's influence area (-1 until a new tower finishes). `Queries::BeachAreaAt(x, y)` returns the area a tower at
  that tile will control, including during construction (-1 outside an area; the default for stand-in queries).
  `Queries::SceneryFits(key, x, y, dir)` is the scenery place mode's footprint check (false for stand-in queries).
- **Queries** answer what a snapshot cannot answer cheaply, with the executor's own checks: `BuildingFits(key, x, y,
  dir)` is the place mode's footprint check. A positive answer predicts the command is accepted (cash and tree aside).
- `Queries::SceneryFits(key, x, y, dir)` is the scenery place mode's footprint check (bins, flowers; false for stand-in queries).
- Bots are deterministic: no clocks, no unordered containers keyed by pointers, no randomness of their own. With the
  run's `seed` and `fixed-dt` a run repeats bit for bit (the end line's world hash); both scenario-1 runs end with the
  same tick and hash on Windows (MSVC) and in the Linux `obl-sim`.
- The executor still validates every command; a rejected command is logged (`command: @T rejected ... (reason)`) and the
  bot sees the unchanged world on its next think.

## Win and loss
Every campaign script decides its own end in its `STATE_WORLD_RUNGAME` update: a win opens `ShowMenu ScenarioWon <text>`
(`GameWon` in scenario 12), calls `Function UnlockNextScenario` and pauses; a loss opens `ShowMenu ScenarioLost <text>`
(a deadline such as "17:00 on day 2", or a failed condition). Scripts use the zero-based simulation day
(`World::m_gameDay`, exposed as `WorldView::day`); the display adds one, making that deadline "17:00 on day 3" to a player. The world script's `ShowMenu` reports each dialog to
`NoteScriptDialog` (`scenario_outcome.cpp`); the first won/lost dialog sets the outcome (`scenario: won (ShowMenu ...)` in
the log). Observation only: nothing in the simulation reads it, hashes and replays are unchanged. Sandbox islands have no
goal and stay `running`. The objective flags in the view are the progress the script itself shows in its Objectives
window; they are not the verdict (scenario 1 sets its only flag and wins in the same update).

## Run files and the host
```
OBL-bot 1
bot build-tree-s1
start scenario s1
seed 1
fixed-dt 0.02
max-ticks 20000
expect won
```
`bot` (an id of `Bots()`), `start scenario <name>` (as a replay), `max-ticks` are required; `seed` 1, `fixed-dt` 0.02 and
`expect won` are the defaults. `--headless <file>.bot` (`OpenBeach.exe` or the Linux `obl-sim`) boots the start as a
replay does ([headless/replays.md](../headless/replays.md)), then runs ticks until the outcome is set or `max-ticks` is reached:
```
obl-sim: bot build-tree-s1 start s1 seed=1 fixed-dt=0.02 max-ticks=20000 expect=won
obl-sim: boot frames=7 ms=795
obl-sim: @0 set-game-speed level=2
obl-sim: @0 place-building key=BEACHBAR x=80 y=48 dir=0
...
obl-sim: end ticks=8170 outcome=won day=1 hour=11.15 cash=2638 guests=127 rating=52 commands=13 applied=13 rejected=0 hash=e00949a0094e8ed0 ms=14140 ticks-per-s=578
obl-sim: PASS
```
The host reports progress every 5000 ticks (day, cash, guests, rating and objective flags) to diagnose missed goals.

Exit code 0 = the expected outcome, 6 = the other outcome or none within `max-ticks`, 5 = the run could not start or left
gameplay. ctest `headless_bot_<run file>` (label `bot`, in `tests/manifest.txt`; `build.ps1 -Test` leaves them out) runs
`tools/bot_runs.py --run-seed` on one file: the run file's own seed, outcome and bands. `tools/obl_sim_tests.py`
(`bot_<run file>`) runs every file of `tests/bots` on Linux with the outcome only; CI skips those and runs `bot_runs.py`.


Building placement supports rotations 0-3. The executor and fit queries reject all other directions before payment or
placement, because the placement transform only supports quarter turns.
