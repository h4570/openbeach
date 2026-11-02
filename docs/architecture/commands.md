# Command layer

The simulation takes player input only as **semantic commands** (`PlaceBuilding`, `HireStaff`, `SetPrice`...), never as
mouse positions or widget clicks. The GUI turns clicks into commands; the simulation applies them at a defined tick. The
same command stream can then be recorded, replayed in the game, replayed headless by `obl-sim`, written by hand in tests
and produced by scenario bots.

Code: `src/game/game/command.h` (types, text form, queue), `src/game/game/command_recorder.h` (recording, file format, replay
cursor), `src/game/game/command_executor.cpp` (applies a command to the live world), `src/game/game/command_loop.cpp`
(the game loop's queue and tick counter); unit tests `tests/unit/command_test.cpp`, `tests/unit/command_recorder_test.cpp`,
game tests `tests/command_*.txt`, recording and replay `src/testing/replay.cpp`. GUI wiring and the direct-write audit are
complete (below); object dialog controls use typed commands (`command_controls.h` holds their keyword/word adapter).

## Data flow
```
 GUI / test script / bot        CommandQueue            simulation tick T                 Recorder
 ----------------------  Submit  ------------  TakePending  -----------------------  Record(T, c)  ------------
  click -> Command  ------------>  pending  ------------->  apply each command,  ----------------> commands
                                                             then World update                     + hashes
 replay file -> ReplayCursor.Due(T) ------------------------^
```
- **Submit** may happen at any time between ticks (the GUI runs on the game thread between `Game_Update` calls).
- At the **start of tick T** the simulation takes all pending commands in submission order, records each with tick `T`
  and applies it, then runs the world update of that tick. A replay feeds `ReplayCursor::Due(T)` into the same place
  instead of the queue, so recorded and replayed runs execute identical code.
- **Tick** = the number of `Game_Update` calls since the world became playable (after `boot-gameplay`, scenario start or
  a load). Paused frames count too: pause and speed are commands, so a replay reproduces paused stretches exactly.
- **Checkpoint `@T hash H`** = the world hash (schema in the header, [world-hashes.md](../testing/world-hashes.md)) after
  `T` ticks completed and before the commands of tick `T`. `@0` is the start state.

## What is a command
- Anything that changes simulation state on behalf of the player. Camera, zoom, panels, music, tooltips, options and
  browsing lists are **not** commands: they never touch the world, so the headless simulation does not need them.
- Save and load are not commands either: a load ends the current recording (a new start state begins).
- Commands carry **intent with stable identifiers**: object ids (`uint32_t`, the world's `m_dwObjectIDList` ids, as
  in messages), def keys (`BuildingDef::m_key`, `SceneryDef::m_key`), tile coordinates and plain values. Never pointers,
  widget names or screen coordinates.
- The **executor re-validates** (cash, tile flags, staff state) exactly as the GUI path does today; the GUI keeps its own
  checks only for cursors and previews. A command that the executor rejects is still recorded, so a replay rejects it
  the same way.
- Command application must not depend on GUI state (selected object, open dialog): everything it needs is a field.

## Command set (format version 1)
| Text name | Fields | GUI path it replaces | Executor checks before acting |
|---|---|---|---|
| `place-building` | `key x y dir` | `GuiModePlaceBuilding_Update` -> `World_PostPlaceCommand` | known key with a footprint, not locked by the tree, allowed, dir 0..7, footprint fits, cash left over |
| `place-scenery` | `key x y dir` | `GuiModePlaceScenery_OnMouse` -> `World_PostPlaceCommand` | known key, not category 1 (paths, grass), dir 0..7, footprint fits, cash covers |
| `paint-surface` | `key x y` | grass and paving modes' `PlaceTile` (one per newly painted tile of a drag) | known key of category 1, tile on the map and free for the surface (`m_PathFlag` != 0 grass, else path), cash covers |
| `dozer` | `x y` | `GuiModeDozer_OnMouse` -> `GuiModeDozer_RemoveAt` (scenery, bare path or surface, with the removal income) | tile on the map, `GuiModeDozer_CanRemove` |
| `demolish` | `object` | `ConfirmSell` -> `BuildingObject_BeginDemolish` | building in its own dialog state (below) |
| `hire-staff` | `profession` | `HireNewStaff` | Cleaner, Builder, Mechanic, Rep, Lifeguard or SecurityGuard; a home with a free place and no job |
| `dismiss-staff` | `staff` | `DismissConfirm` -> home's `BuildingObject_DismissStaff` | staff member (types 7..12) with a home |
| `set-staff-wage` | `staff wage` | wage number adjuster: the member's `SetVariable("STAFF_WAGES")` | staff member; 0..999, setter clamps below 5 |
| `copy-staff-wage` | `staff wage` | one member of "apply wage to all" (`CopyWageToClass`), then `StaffObject_RecalculateHappinessTarget` | staff member; 5..999 |
| `set-staff-shift` | `staff start end` | `StaffObject_SetShift` (+/- buttons, "apply to all" submit the hours) | staff member, hours 0..23 |
| `send-staff` | `staff x y z` | send mode click on the ground -> `the send-staff direct-to-point order` | staff member, the point's tile walkable land without building |
| `send-staff-to` | `staff target` | send mode click on a picked object -> `StaffObject_DirectToTarget` | staff member, target exists |
| `set-building-open` | `building open` | `OpenBuilding` / `CloseBuilding` | building in its own dialog state |
| `call-repair` / `call-cleaner` | `building` | `CallRepair` / `CallCleaner` | building in its own dialog state |
| `set-price` | `building slot price` | price number adjuster: `SetVariable("COMMOD_PRICE_<slot>")` | building in its own dialog state, slot < its commodity count; price 0..999.99 |
| `set-event-price` | `building slot price` | show price slider: `SetVariable("EVENT<slot>_PRICE")` | building in its own dialog state, slot 0..2, a show schedule (`m_pEvents`; the game's SetVariable does not check), the slot's show not in progress |
| `set-beer` | `building key` | beer/cocktail list row (list kinds 4/12) -> `BuildingObject_SetBeerType` | building in its own dialog state, known `BeerDef::m_key` that the scenario script offers (`m_bScriptFlag`) |
| `set-show` | `building slot key` | show list row (kinds 9..11) -> `BuildingEvents_SetSlotEvent` | building in its own dialog state with a show schedule, slot 0..2, known available `EventDef::m_key`, the slot's show not in progress |
| `resume-game` | | pause box "resume" (`ExitDialogResume`) | none; clears `World::m_forcedPaused` and `m_bPaused` at one tick (the pause key and `set-paused` leave a forced pause alone) |
| `break-object` | `object` | `BreakObject` (sets `Entity::m_bDebugBreak`) | object exists |
| `set-game-speed` | `level` | speed buttons: `GAME_GAME_SPEED` -> `World_SetGameSpeedLevel` | level 0..2 |
| `set-paused` | `paused` | pause key, `ClosePause` (`World::m_bPaused`) | none |
| `set-repair-interval` | `object value` | `BUILDING_REPAIRINTERVAL` | building in its own dialog state; integer 0..999 (0 becomes 1 in the setter) |
| `set-beer-strength` / `set-beer-price` | `object value` | `BEER_STRENGTH` / `BUILDING_BEERPRICE` | building in its own dialog state; strength 0..1, price 0..999.99 |
| `set-building-open-policy` | `object value` | `BI_BUILDING_OPEN` | building in its own dialog state; int32, 0 opens, any nonzero closes |
| `set-beach-open-policy` | `object value` | `STAFF_BEACH_OPEN` | building in its own dialog state or live lifeguard; int32, retains each class's setter semantics |
| `set-cleaner-toilets` / `set-cleaner-bins` / `set-cleaner-litter` | `object value` | `CLEANER_TOILETS` / `CLEANER_BINS` / `CLEANER_LITTER` | live cleaner; integer 0..255 |
| `set-mechanic-repair` / `set-mechanic-vandals` / `set-mechanic-water` | `object value` | `MECHANIC_REPAIR` / `MECHANIC_VANDALS` / `MECHANIC_WATER` | live mechanic; integer 0..255 |
| `set-rep-guest-time` / `set-security-harshness` | `object value` | `REP_GUEST_TIME` / `SECURITY_HARSHNESS` | live rep / security guard respectively; 0..1 |

- "Building in its own dialog state": a building with a build or demolish job opens the construction or demolition dialog
  instead (`BuildingObject_OnSelect`), which has none of these buttons, so the executor rejects such buildings.
- Ids: the all-ones value is the "no object" id and appears in the id lists, so it never resolves; the object found must carry
  the asked id.
- `x y` of the place commands is the footprint origin the mode computed from the tile under the cursor
  (`GetFootprintOrigin` / `AdjustFootprintOrigin`), `dir` the preview's direction. The GUI click and the executor share
  `BuildingFits` / `SceneryFits` / `BuyBuilding` / `BuyScenery` (`gui_modes_place.h`). Building directions must be 0-3;
  unsupported directions are rejected before payment and placement.
- Not commands: the "Home" staff button (selects and centres the home, GUI only) and `Game_SetVariable`'s keywords (options;
  its only simulation keyword, `GAME_GAME_SPEED`, is `set-game-speed`).
- "Apply shift to all" is one `set-staff-shift` per member of the class; "apply wage to all" is one `copy-staff-wage` per
  member (it bypasses the wage thought, so it is not `set-staff-wage`; the executor requires a copied wage of 5..999).
- Asset sales (`dozer`, completed building demolition) credit cash and demolition statistics as before, but leave
  operating `IncomeToday` unchanged. Scenery's cash refund is 20% of cost; its separate demolition statistic is 75%.
  This fixes an income-goal exploit; `tests/dozer_income_exploit.txt` repeats the forum's fountain trick.

Reference replays are committed (`tests/replays/`): new commands may be added, but a change of an existing command's text
form bumps the format version and re-records them. The typed object controls are additive version-1 commands. The GUI records only typed controls. Byte values use int32 fields so invalid words are rejected rather
than truncated; float words preserve their bits, including nonfinite values accepted by the existing float setters.

## Executor and tick loop
- `ApplyCommand` (`command_executor.cpp`) runs the game calls of the GUI path after that path's checks; what only the GUI
  needs (dialogs, sounds, cursors, message boxes) stays in the GUI. A rejection changes nothing and returns its reason.
- `RunCommandTick` (`command_loop.cpp`) is called by `Game_Update` after a pending state switch and the input update, before
  the state's update. Outside settled gameplay (another state, no world, a state switch this frame, a pending load) the
  queue is dropped (`dropped`) and the tick counter waits; on the first gameplay frame of a world the tick starts at 0. Each
  gameplay frame applies the pending commands with the current tick, logs `command: @T applied|rejected <text>`, then
  advances the tick, paused frames included.
- Test scripts submit with `command <text>` ([game-tests.md](../testing/game-tests.md)); the GUI calls `SubmitCommand`.

## GUI wiring
- Wired: building, scenery, grass and paving placement; dozer and sell; staff hire/dismiss, wages and shifts (both bulk
  variants), send mode; building open/close/repair/clean; commodity/show prices and other object controls; speed and pause.
  The live action Env submits stable ids; staff browser table arrows also submit explicit shift hours (`gui_tables.cpp`).
  Bulk actions enumerate members at submission. Hire submits the profession even
  when the GUI predicts no home or no room. Its message box stays immediate; hire/dismiss roster refresh waits until the
  next command tick has completed (`RefreshCommandDialogs`, before the GUI manager update).
- `gui_controls.cpp` submits object toggles/radios; `gui_controls_range.inl` submits slider and number-adjust values.
  Slider floats keep the existing hundredth quantization, number-adjust wages stay integers. Commodity/show prices and
  wages and all other object controls use typed commands. `MakeObjectControl` translates the script's keyword and value
  at the GUI boundary; the executor has no keyword allowlist. Music, sound, the weather RGB preview sliders
  (`gui_weather_slider.txt`: GUI-only) and game options retain their existing GUI paths. `GAME_GAME_SPEED` submits `set-game-speed` in `Game_SetVariable`.
- Pause and speed keys submit through `world_view_logic.h`'s Env; the pause dialog responds immediately, the world changes
  next tick. Two speed keys in one frame compute their ordered requests from a local level, without writing the world.
- **Every press on the map is submitted**, also when the mode's own check refuses it, so the refusal is recorded and a
  replay refuses it the same way. The mode's check only predicts the outcome for the sound, the cursor, the last placement
  and whether the building mode leaves; the world changes at the start of the next tick. Exception: a held drag of the
  surface modes submits only the tiles it paints (a press on a refused tile is still submitted).
- The effect is one frame later than before the wiring. Click tests step a few frames before checking; a GUI test of a new
  wired action checks `command-report` (`tests/gui_modes_place_click.txt`, `tests/gui_sell_command.txt`).
- Test drivers: `call-drive GuiActionConfirmSell|OpenBuilding|CloseBuilding|CallRepair|CallCleaner|DismissConfirm|IncStaffHours|DecStaffHours|SetStaffWages|SetStaffShift|StaffSetAllWages|StaffSetAllShift|ClosePause <id>`
  (each name has the `GuiAction` prefix; also `ExitDialogResume` and `BreakObject`, covered by `gui_resume_break_commands.txt` and the `gui_controls` replay) runs the real action with a temporary provider/browsed id; `GuiActionHireNewStaff`
  takes the hire type 0..5. `GuiControlWage|Price|EventPrice|Variable|Interval|Toggle|Weather` takes an object id and runs the actual
  control callback with a fixture value; `GuiControlSpeed` takes a level. Tests `gui_*commands.txt`,
  `gui_event_price_command.txt` and `gui_modes_mouse.txt` assert effects and command counts. `GuiListBeer|GuiListShow <building id>`
  runs the list's real selection callback with a synthetic row (the first offered beer/show that differs from the current one; show
  slot 1) on a bare list; `gui_beer_list_command.txt` and `gui_show_list_command.txt` check the effect and the refusals. The list rows carry
  definition ids, the commands the definition keys; the `gui_lists` reference replay (scenario 4) records `set-beer` through the callback,
  a stage cannot be placed by a command at a scenario start, so `set-show` is covered by the script test only. `GuiTableShiftUp|Down`
  takes a staff id. `tests/replays/gui_controls.record.txt` records the GUI-produced command stream twice and verifies its
  world hashes through the normal replay executor, with no GUI callbacks during replay.

## Text form
One command per line: the name, then `field=value` tokens separated by spaces (output uses single spaces), e.g.
`place-building key=BeachBar x=40 y=52 dir=2`. Fields may come in any order on input, every field is required exactly
once, unknown fields and names are errors (format drift fails loudly). Output always uses the declared order.
- Integers in decimal; `bool` as `0`/`1`; `float` in the shortest form that round-trips bit for bit (`std::to_chars`),
  so a replay applies the same float the GUI produced.
- Strings are percent-encoded: bytes outside `!`..`~`, `%` itself and `=` become `%XX`; an empty string is `key=`.

The same text form serves test scripts (`command <text>`) and bot output.

## Replay file
```
OBL-replay 1
start scenario s3
seed 1
fixed-dt 0.02
hash-schema 2
@0 hash 0123456789abcdef
@0 set-game-speed level=2
@120 place-building key=BeachBar x=40 y=52 dir=2
@312 hash fedcba9876543210
end 1000
```
- Header lines in this order: format, start state (`scenario <name>` or `save <path>`; the name is `<n>` or `s<n>`
  for campaign scenario n, `sandbox<n>` for sandbox island n, `ParseStartScenario`), `seed`, `fixed-dt`, `hash-schema`.
- Body lines `@T ...`: ticks never decrease; within a tick the checkpoint comes before the commands. A checkpoint
  `@T hash <16 hex>` may carry section digests after the hash (`guests=hhhhhhhh/fields ...`, non-empty sections in
  section order, [world-hashes.md](../testing/world-hashes.md#sections)); recordings write them, readers accept files
  without them. `#` lines and blank lines are comments. `end T` is the last line: the replay runs exactly `T` ticks.
- Text, not binary: reference replays are committed and reviewed as diffs, and a hand-edited replay is a test.

## Replay and verification
- In the game: test-script directives `record <file>` / `replay <file>` ([replays.md](../testing/replays.md)). A
  `TickListener` on `RunCommandTick` (`command_loop.h`) sees each world start and leave, the start of every tick (where a
  replay swaps the queue's commands for `ReplayCursor::Due(T)` and checkpoints are hashed) and every executed command (what
  `Recorder` records).
- Headless: `OpenBeach.exe --headless <replay>` runs a replay and checks its checkpoints without window, renderer or
  audio ([headless/index.md](headless/index.md)); a standalone `obl-sim` with `--record` (commands of a bot or a script run) is the next
  PLAN item. Both hosts share the tick loop, so a replay passes or fails identically.
- Each mismatching checkpoint names the tick and every differing section (from the digests stored in the replay); the
  full TSV snapshots of a passing and a failing build then go through `tools/world_hash_compare.ps1` for the first
  differing field and the differing record count per section.
- **Platforms:** the hashes depend on float rounding (determinism.md). References are recorded on Windows/MSVC and the
  Linux obl-sim build must give the same ones (both AVX2 without FMA, `det_math` for transcendental functions).
- Reference replays are recorded from current behavior; an intended behavior change re-records them in the same commit,
  with the reason in the body (as for the golden hash tables).

## Traps for the wiring
- GUI-only controls retain their pause and display paths; simulation changes use the command layer.
  Audit both stores and object setter calls in `gui_*`, including common templates and `.inl` files, before declaring the
  layer complete. The dozer's income and placement cash writes are already executor helpers; their filenames alone do
  not make them direct GUI writes.
- `StaffSetAllWages`-style bulk actions expand into one command per object at submission, so the replay does not depend
  on which staff the GUI's list held at that moment.
- Commands submitted while a world transition is pending (load, state change) must be dropped or deferred by the queue
  owner, never applied to the next world.
