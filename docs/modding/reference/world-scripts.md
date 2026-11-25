# World scripts

A world mod (`"scope": "world"`, the default) may ship world scripts as `scripts/world/*.wsc`. They use the DSL of a scenario's
own `scenarios/<id>/world.wsc`, start with `#obl-script 1 world`, and run in every scenario of the pack beside the scenario's
script. A rule that should hold everywhere (an easier economy, a bonus each day, a storm each week) is one mod script instead of
a patched copy of every scenario. A presentation mod may not ship `scripts/world/` (the start-up check refuses it, see
[mods.md](mods.md#start-up-check)). Guide: [world-rule.md](../guides/world-rule.md).

## Load order and execution
- The scripts load from the active world mods in load order (`[Mods] Active` / `OBL_MODS`); within one mod, in ASCII order of
  the file names.
- On a new game the scenario's script starts first, then each mod script enters its own `STATE_WORLD_STARTUP` and runs its
  `OnEntry`.
- On every tick the scenario's `OnUpdate` list runs first, then each mod script's, in load order.
- World messages (`Message MT_x ... OnReceipt`) reach the scenario's handlers first, then each mod script's.
- Each mod script has its own state table (at most 10 states, like a scenario), its own `Flag0`..`Flag31` and its own
  `Var0`..`Var31`. `ChangeState`, `Flag<n>` and `Var<n>` in a mod script never touch the scenario's script or another mod's.
- On a save load every script's `STATE_WORLD_STARTUP` `OnEntry` runs again with `ChangeState` silenced (as for the scenario's
  script); then the save restores each mod script's state, flags and variables, and the world values the save holds.

## Structure
```text
#obl-script 1 world
State STATE_WORLD_STARTUP
    OnEntry
        ChangeState STATE_PAYING;
    End
    OnUpdate
    End
    OnExit
    End
End

State STATE_PAYING
    OnEntry
    End
    OnUpdate
    End
    OnEvent DayStarted
        Add PlayerCash 1000;
    End
    OnExit
    End
End
```
A state may hold `OnEntry`, `OnExit`, `OnUpdate`, `Message <MT_NAME> OnReceipt ... End` and `OnEvent <Event> ... End`
blocks. Every mod script needs `STATE_WORLD_STARTUP`.

## World events
An `OnEvent <Event> ... End` block runs when the event happens while the script is in that state. Events reach the mod scripts
in load order at a fixed point of the tick, so they are as deterministic as the rest of the simulation. Scenario scripts do not
get them.

| Event | When |
|---|---|
| `DayStarted` | The clock passes midnight, on the tick that sends the new day to buildings, staff and guests. |
| `BuildingFinished` | The builders leave a finished construction site. |
| `GuestArrived` | Each guest getting off a bus or ferry, after `GuestCount` grew. |
| `WeatherChanged` | The weather schedule switches to another weather. |

Further events are planned; ask for the one your mod needs in an issue.

## Refused calls
The scenario keeps the last word on winning and losing. Before the game starts, every active mod's `scripts/world/*.wsc` is
parsed and checked, and the game stops with a message naming the mod, the file and the reason when a script contains:
- `Function UnlockNextScenario;`
- `ShowMenu ScenarioWon`
- `ShowMenu GameWon`
- `ShowMenu ScenarioLost`

or has no `STATE_WORLD_STARTUP`, more than 10 states, or a syntax error. Every other function (`PauseGame`, `ResumeGame`,
`LockMap`, `UnlockMap`, `NoBreakDowns`, `AllowBreakDowns`, `NoLitter`, `Debug`) and every other `ShowMenu` dialog is allowed.

## Values and commands
- Read in `If ( <left> <op> <right> )` and on the right of `Set`, `Add`, `Minus`, `Divide`, `Multiply`: the script's own
  `Flag<n>` and `Var<n>`, and the world's values (`GameDay`, `GameTime`, `PlayerCash`, `IncomeToday`, `ProfitToday`,
  `GuestCount`, `ResortRating`, `BuildingCount<KEY>`, `ScenarioObj<n>`, ... as listed in the pack's
  `scripts/world_vocabulary.json`).
- Written: `PlayerCash`, `GameTime` (only in `STATE_WORLD_STARTUP`), `DemoTime`, `ScenarioObj<n>` (the objective marks the
  player sees; they do not win), `BeerActive<BEER>`, `DefaultBeer<BUILDING>`, `EventActive<SHOW>`, `WeatherDayOffset`,
  `DisasterDayOffset`.
- Commands: the scenario's DSL (`If`/`Else`/`End`, `Set`, `Add`, `Minus`, `Divide`, `Multiply`, `ChangeState <state> [Time t];`,
  `Function`, `ShowMenu`, `PlaySound`, `MoveCamera`, `Message`, `Create`, `Path`, `Grass`, `Area`, `BoatPoint`, `BoatEntry`,
  `BoatExit`, `BoatEvent`, `CircuitPoint`, `Disaster`, `Weather`).

### Built-in condition values

`GuestCount`, `BreakdownCount`, `VandalismCount`, `CleanerCount`, `MechanicCount`, `SecurityCount`, `LifeGuardCount`,
`RepCount`, `DemoTime`, `SandBox`, `LitterCount`, `UnHappyLeavers`, `ProfitToday`, `ConstructCount`,
`AllBuildingsOperating`, `GuestPartyCount`, `GuestTottyLevel`, `GuestBeautyLevel`, `DiffConstructCount`, `BuildTreeExist`,
`GuestsEaten`, `GuestsDrowned`, `FinesCollected`, `GuestShagCount`, `GuestPissedCount`, `WeatherDayOffset`, `IncomeToday`,
`GuestSafetyLevel`, `BarsBuilt`, `DisasterDayOffset`, `Me`, `GameTime`, `ResortRating`, `GameDay`, `PlayerCash`.

## Saves and replays
Mod scripts are simulation, so their mod is recorded in saves and replays ([mods.md](mods.md#saves-and-replays)). A save stores
each mod script's state name, flags and variables in `world.mod_scripts`
([saves](../../architecture/data-formats/saves.md#mods)); with no mod world scripts the key is not written, and saves, replays,
world hashes and goldens stay as they were. The world hash has a `ModWorldScripts` class with the same fields only when a mod
script is active.

## Tests
- Unit: `world_script_mod_test` (the scenario's script and two mod scripts in order on one tick, a state budget per script,
  refused calls, `OnEvent` blocks, this page against the code) and `world_receive_test` (a mod script's `ChangeState` stays in
  that mod; messages reach the scenario, then the mods).
- Game, L0: `tests/l0_mod_world_script.txt` (the mod `tests/mods/l0_world_script` pays 5000 each day: checked after two days
  and after a save written before midnight), `tests/replay_l0_mod_world_script.txt` and
  `tests/replay_l0_mod_world_script_refused.txt` (the replay recorded with the mod matches with it and is refused without it),
  `tests/l0_mod_world_events.txt` (`tests/mods/l0_world_events` counts each event), `tests/l0_mod_world_script_refused.txt`
  (`tests/mods/l0_world_refused` opens `ScenarioWon` and stops the start), `tests/l0_mods_world_scripts.txt` (two world
  mods with scripts at once, saved and loaded). `mod-world-script-report <mod>/<file>` reports a mod script's
  `state=`, `flags=` and `var0=`..`var7=`.
