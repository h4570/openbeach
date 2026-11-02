# World hashes

`world-hash` writes a defined projection of the simulation state on the game thread into a memory snapshot (no save slot,
no change to game data or RNG) and reports a 64-bit hash. Two runs that
reach identical checkpoints followed the same simulation. Replay checkpoints use the same capture ([replays.md](replays.md)).

## Commands
| Command | Effect |
|---|---|
| `world-hash` | immediate checkpoint (needs a settled world with a ready GUI) |
| `world-hash every N` | checkpoint 0 now, then after every N `Game_Update` frames inside `step` |
| `world-hash off` | stops periodic checkpoints |
| `world-hash expect-same` / `expect-different` | checkpoint and compare with the previous one |
| `world-hash expect <16 hex digits>` | checkpoint and compare with a hash taken elsewhere (another process, a replay file) |

A periodic checkpoint that falls inside a transition (world loading, state change, pending save) is deferred to the next
settled frame and logged as `world-hash deferred` / `resumed`; the delay depends only on the update count, so it is
repeatable. Report line: `world-hash schema=3 checkpoint=N steps=S fields=F hash=H file=checkpoint-N.tsv`. Files go to
`run/world-hashes/` or the absolute `OBL_WORLD_HASH_DIR`. For hash assertions use `expect-same/different`, not
`expect-last hash` (assertion values are doubles).

## Snapshot format (schema 3)
The TSV starts with `OBL-world-v3`; each record holds section, field name, type tag and the exact little-endian bytes in
hex. Record order matters; floats are compared bit for bit. The hash is 64-bit FNV-1a over the header line and, of every
record, its type tag and bytes (`type<TAB>bytes<LF>`), printed as 16 hex digits: the section and field names stay in the
TSV for diagnosis but are not hashed (`WorldSnapshot::ForEachHashedLine`), so renaming a record or a class changes no
reference. Changing scope, order, types or values needs a new schema version and new references.

Covered: world clock, speed, flags, counters, scenario script, power, economy, resort opinions and goals; the map grid and
paths; every object's identity, state, owner, position, movement and route; guest needs, cash, happiness and visit state;
buildings and staff (assignments, power, breakdowns, money, queues, wages); the serialized managers (ferry/events,
repair, feedback, cleaning, construction, weather); the delayed message list; and `rand-calls` (RNG draws since `seed`).
Excluded: camera, sprites and animation, screen rectangles, sound, fields outside the list in
`src/game/world/world_snapshot.cpp` (`kObjectFields`), and fields the game never initializes (uninitialized heap would
make two identical runs differ). Each exclusion and normalization is documented next to its code. Add fields together
with the tests of their subsystem.

## Model emitter
The snapshot comes from `src/game/world/world_snapshot_model.{h,cpp}` and `world_snapshot_objects.cpp`, not from the save
path, so the save format and the memory layout can change under it. Each subsystem has an emitter that writes its
records straight from the state; `EmitWorld` runs them in a fixed order. Records `Normalize` erases are not emitted; `Normalize` still runs over the result
(a record-level pass, layout-free) for the conditional cases (staff prefixes, a guest's stale vehicle id, a daily
ferry's day, weather and work-point garbage).
- Objects: buildings, guests, then the others, each as `object.type` (byte), `object.name` (the virtual name: a scenery's
  construction key, otherwise `base.name`) and its class chain derived-first (profession, `Staff`, `Mover` with
  the route after `moving.path`, `Prop`, `Entity`), only the `kObjectFields` names. Every object record is named
  by its obl.save key (`guest.cash`, `base.id`; the field tables of `src/game/objects/object_*.h`), the manager header by
  `active_*`, `object_ids` and `next_id`; schema 3 hashes no names. The class follows `base.type`:
  `ServiceStaff`, `Mechanic` and `SnootyStaff` have no own records; performers go straight to
  `Mover` (their Save base is `Person`, which writes nothing of its own).
- A new field in the snapshot is a schema change, not an emitter change: an emitter that writes more or other records
  fails the replays. The replays, the golden hashes and the scripts that use `world-hash` guard the emitters.
- A new field or section means a new version and new references.

## Sections
The total hash says whether two states match; per-section digests say where they differ, so a replay mismatch names every
differing subsystem and an intended fix re-records a scope the commit body can name. Code: `src/game/world/world_sections.h`
(pure, unit-tested), a pass over the TSV text; the total hash and the TSV are unchanged (schema 3).

| Section | Records |
|---|---|
| `world` | `World` outside economy: clock, speed, flags, power, counters, opinions, goals, script |
| `economy` | `World` fields named `*income*`, `*cost*`, `*profit*`, `*fine*`; `Player` (cash) |
| `map` | `WorldMap`, `Path` |
| `objects` | `ObjectManager` header (counts, id list, next id) and object types of no other section (sharks) |
| `scenery` / `buildings` / `guests` / `staff` / `vehicles` | objects by their `object.type` record: 1 / 2 and 5 (tables) / 3 / 6-12 and 14-20 / 4 |
| `ferry`, `repair`, `feedback`, `messages`, `cleaning` | `FerryManager` (ferry and events), `RepairManager`, `FeedbackManager`, `MessageHandler`, `CleaningManager` |
| `construction` / `weather` | `ConstructionManager` before / from `light_index` on (Weather is saved without its own class header) |
| `rng` | `rand-calls` |
| `other` | any other class |

A digest is `hhhhhhhh/fields`: 32-bit FNV-1a over the section's TSV lines without their names (type and bytes with
newlines, as the total) in snapshot order, and the record count. Only the total is authoritative; a section digest is a diagnostic. Records that move between sections without
changing (an object reordered against objects of another section) change the total but no digest (`sections=none`).
`tools/world_hash_compare.ps1` mirrors the classification; a shared fixture in `tests/unit/world_sections_test.cpp` and
`tests/tools/test_world_hash_compare.ps1` keeps both in step, so change them together.

## Comparing runs
```powershell
./tools/world_hash_compare.ps1 -Reference <dir> -Candidate <dir>         # compare two existing checkpoint directories
./tests/tools/test_world_hash_compare.ps1                                # synthetic tests of the tool, no game
```
The comparison needs equal checkpoint count, indices, schema, steps, hashes, field counts and identical TSV content, and
names the first difference (checkpoint, step, record, class, field, bytes). At that checkpoint it then lists every differing
section: `section guests differing=N fields reference=A candidate=B first: record R <class> <field>`. Records are compared
position by position inside a section, so an inserted object counts its section's later records too; the count is a scope,
not an exact edit distance. Real snapshots take about 10 s. Fixtures must use `seed`, `fixed-dt`,
`boot-gameplay` and frame-counted waits.

## Golden hash tables
`tests/golden-hashes/<script>.tsv` holds one line per checkpoint (`index steps fields hash`) under the `OBL-world-v3`
header; manifest entries `<script>.golden` run the script once and compare (`tools/world_hash_golden.ps1`). Only the short
`golden_hash_s1/s3/s12` tables remain, for the fixture-only `set-cash` and `weather-insert` writes on guest, ferry and staff
worlds. Those are not player actions and do not belong in the semantic command stream. Everything else is covered by the
command references ([replays.md](replays.md)). A name's spelling reaches no hash. Their shared script is `tests/golden_hash_sN.txt`.
```powershell
./tools/world_hash_golden.ps1 tests/golden_hash_s3.txt -Config RelWithDebInfo -Record   # re-record from this build
```
`-Record` is the only way to change a table: it runs the fixture twice with the current build and records only when both
runs are identical field by field (`world_hash_compare.ps1`). An intended behavior change (for example a bug fix) re-records the affected tables in the same commit, with the reason in the commit body. Artifacts go to a fresh
`run/world-hash-check/golden-<name>-<id>/`; a passing check deletes them unless `-KeepArtifacts`, `-OutDir` or
`OBL_KEEP_WORLD_HASH_ARTIFACTS=1`.
