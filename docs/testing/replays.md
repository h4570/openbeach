# Command replays

A replay file ([commands.md](../architecture/commands.md#replay-file)) holds a start state, the player commands with their
ticks and world-hash checkpoints. Replaying it must reach every checkpoint with the same hash: any unintended change of
simulation behavior shows up as the first mismatching tick. Reference replays are recorded from current behavior.

Code: `src/testing/replay.cpp` (directives and the session), the tick listener `TickListener` in
`src/game/game/command_loop.h`, the file format, `Recorder` and `ReplayCursor` in `src/game/game/command_recorder.h`.

## Directives
| Directive | Effect |
|---|---|
| `record <file> [save <path>] [every N]` | arms a recording of the next world: from its tick 0, every command (applied or rejected) with its tick and a checkpoint every N ticks (default 100) |
| `record end` | a last checkpoint at the current tick, then writes the file; logs `record ticks= commands= checkpoints= applied= rejected=` |
| `replay <file> [allow-mismatch]` | boots the file's start state with its seed and fixed-dt, feeds the recorded commands at their ticks, compares every checkpoint and blocks the script until the last tick |

- `record` must follow `seed`, `fixed-dt` and `boot-gameplay scenario <n>` directly (no random draws since the seed), and
  come before `await gameplay`; the replay repeats exactly these three. `record` writes `start scenario <n>` (`<n>` is
  the `boot-gameplay` number); `replay` also reads `s<n>` and `sandbox<n>`. For a save, use `seed`, `fixed-dt`, then
  `record <file> save <path> [every N]`: it loads the save and writes `start save <path>`. Both directives resolve save paths
  relative to the game directory. An old save loads through a free temporary slot from 9000, removed when the session
  ends; a converted `obl.save` (a JSON document, detected by content) loads through `load-obl-save` in memory
  (`replay_obl_save.txt`: the same world as a recording from the old save). References generate their save fixture
  rather than commit game data.
- Between `record` and `record end` the world may change only through semantic commands (the `command` directive or wired
  GUI controls); `step`, `await` and lookups
  (`object-find`, `place-find`, `object-report`, `$key`) are fine. Other directives that write the world (`speed`,
  `set-cash`, `spawn-*`, `weather-insert`...) would be missing from the replay. `record end` refuses submitted commands
  that have not run yet (`step` once more).
- Leaving the world (load, menu, another boot) before `record end` or the replay's end fails the session.
- During a replay the recording owns the input: commands submitted otherwise are ignored, counted and fail the replay.
- Paths: `record` writes relative to the game directory (`run/` or the runner slot), `replay` reads relative to the
  script's directory (`tests/`). The directive is split on spaces, so paths cannot contain them.
- Result: `replay ticks= commands= checkpoints= applied= rejected= ignored= mismatches= first-mismatch-tick=` (-1 without
  one) `differing-sections=` (summed over the mismatches), then `PASS replay` or `FAILED replay`; `allow-mismatch` drops the
  FAILED line so a test can check the counts.
- Each mismatch logs `replay mismatch tick=T expected= actual= checkpoint=I sections=guests,staff` and one
  `replay section tick=T name=<section> expected=<digest> actual=<digest>` per differing section (digest `hhhhhhhh/fields`,
  [world-hashes.md](world-hashes.md#sections)). A checkpoint recorded without digests says `sections=unrecognized` and logs
  `replay sections tick=T actual=...`, the digests to paste after its hash. The headless host prints the same as
  `@T sections=...` / `@T section <name> expected= actual=` (ctest `headless_mismatch`).

## Ticks and checkpoints
- `RunCommandTick` calls the listener at the start of each gameplay tick, before the tick's commands; checkpoints are taken
  there, so `@T hash` is the state after T ticks. The end checkpoint (`@end`) is taken between frames after the last tick.
- Checkpoints use the `world-hash` capture (schema 3, [world-hashes.md](world-hashes.md)): the same TSV files and report
  line, with `steps=` = the tick. A recording or replay therefore writes `checkpoint-<i>.tsv` into the world-hash directory.
- A recording stores each checkpoint's section digests after its hash (`@100 hash <16 hex> world=.../56 economy=... ...`);
  the replay compares only the total and uses the digests to name the differing sections. Files without digests (older or
  hand-written ones such as `headless_s1`) still replay.
- The recording may mix normal frames (`await frames`) with `step` frames; the replay runs tick 0 as a normal frame and the
  rest as `step` frames in batches of 1000 with a render between batches. Rendering must not change the simulation, so both
  match (checked by `tools/replay.ps1 -Record`; `s12_bar` crosses a batch boundary, [determinism.md](determinism.md#rendering-must-not-feed-the-simulation)).
- Cost: about 5-6 ms per tick at full speed in Debug and under 1 ms in RelWithDebInfo, plus the boot (scenario 1: 2550 ticks
  in 13-20 s Debug, 3 s RelWithDebInfo). Literal object ids in the recording scripts (`target=12`) are scenario-specific;
  the recording stores every `$key` as its literal value anyway.

## Reference replays
`tests/replays/<name>.replay` is committed; `tests/replays/<name>.record.txt` is the script that recorded it (not a manifest
entry) and `tests/replay_<name>.txt` the manifest test that replays it.

| Replay | Content |
|---|---|
| `s1_commands` | scenario 1, 2550 ticks: every command type of format 1 applied at least once, plus rejected ones (unknown key, a cleaner without a home, demolishing scenery) |
| `s3_resort` | scenario 3 (ferry, arriving guests), 4020 ticks: a bar, a loo, a store and a lifeguard tower built, prices, a wage, open/close, a bench, a demolition |
| `s12_staff` | scenario 12, 4020 ticks: every staff class hired, wages, shifts, `send-staff`/`send-staff-to`, repair and a dismissal |
| `gui_controls` | scenario 1: live GUI actions, table arrows, number adjusts, prices, speed, pause, resume and break recorded as commands; every ten ticks checkpointed and replayed without GUI callbacks |
| `gui_lists` | scenario 4, 1220 ticks: a bar built by a command, `set-beer` submitted by the real beer list callback, refused beer key, unoffered beer and show on a building without a schedule |
| `s12_bar` | scenario 12 at speed 2, 1220 ticks: a bar built by a command, then one `step 1200` of daytime with guests and bird sounds; the replay splits it at its 1000-tick batch (render in between) |
| `s1_surfaces` | scenario 1, 1230 ticks: grass and a path painted with `paint-surface`, cleared with `dozer`, rejected presses (surface on a surface, scenery, off the map) |
| `s1_wages` | scenario 1, 920 ticks: `copy-staff-wage` on a Builder (2 refused as outside the GUI range, then a normal wage), a building rejected |
| `s1_event_price` | generated save of scenario 1 with a completed stage: two accepted show prices, an invalid slot and a building without a show schedule rejected; the manifest script generates the save before replaying |
| `l0_commands` | own L0 pack (`OBL_PACK=l0`), 1620 ticks: every command type of format 1 the L0 content can exercise, plus refused ones; targets by lookup |
| `l0_construction` | L0, 3120 ticks: a hut, toilet block and kiosk built, shifts, wage, price, open/close, demolition |
| `l0_gui` | L0, 124 ticks: the building panel's close/open buttons; windowed recording (runs on Linux windowed too), the replay is plain |
| `l0_rejected` | L0, 371 ticks: 24 refused commands of every family between accepted ones |
| `l0_mismatch` | hand-edited from the first 100 ticks of `l0_commands`: @50 a wrong total with two wrong section digests (buildings, staff), @70 a wrong total without digests (`replay_mismatch.txt`); refresh @0/@100 when `l0_commands` is re-recorded (the file's comment says how to refresh @50) |

The `l0_*` scripts carry `# @env OBL_PACK=l0`; `replay.ps1 -Record` reads it (or `-Pack l0`) for the checks, and the manifest
scripts replay with the same header.

```powershell
./tools/replay.ps1 -Record tests/replays/s1_commands.record.txt    # two recordings, identical or nothing; then one replay
./tools/replay.ps1 -Replay tests/replays/s1_commands.replay -Config RelWithDebInfo   # one replay, artifacts kept
```
`-Record` keeps the result only when both runs wrote the same file and field-identical snapshots, and the new file
replays cleanly. An intended behavior change (a bug fix) re-records the affected replays in the same commit,
with the reason in the commit body naming the sections it changed (the replay diff shows them per checkpoint); a change
of the command text form bumps the format version and re-records all. A change of the section rules re-records all too
(the totals must stay identical; check with the hash column of the diff). A new hash definition that keeps the
simulation (schema 2 -> 3) re-hashes the files in place with `python tools/replay_rehash.py --sim <OpenBeach.exe>
--game-dir run <replays>`: it replays each one headless, takes the reported hashes and digests, and replays it again
(`--keep T` leaves a deliberately wrong checkpoint of the mismatch fixtures alone). Run `l0_*` replays with
`OBL_PACK=l0`, or they take the digests of the base pack.

## Diagnosing a mismatch
1. The log names every mismatching tick, its checkpoint file and every differing section with both digests (field counts
   show added or removed objects).
2. `./tools/replay.ps1 -Replay <file> -OutDir <dirB>` with the changed build, and the same with a build that passes
   (another checkout, or `git stash` and rebuild) into `<dirA>`.
3. `./tools/world_hash_compare.ps1 -Reference <dirA> -Candidate <dirB>` names the first differing field and, per differing
   section, the number of differing records and its first one.

Hashes depend on float rounding ([determinism.md](determinism.md)); the references are recorded on Windows/MSVC and match in
Debug, RelWithDebInfo and the Linux obl-sim build.
