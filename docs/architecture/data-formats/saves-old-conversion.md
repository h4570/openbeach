# Data formats: converting old saves

Part of [saves.md](saves.md), after [saves-old-reader.md](saves-old-reader.md): the mapping from the old body to `obl.save`.

## obl-convert save
`obl-convert save <save<N>.dat> <pack directory> <out.json>` writes a validated `obl.save` from an old save; its mapping
decisions (section tables, managers, object state, keys) are in [converter/save-conversion.md](converter/save-conversion.md).

`data.convert_writes_local_saves_as_obl_save` converts every local `run/SaveGames/*.dat` against a freshly converted
catalog and validates the result. The tested corpus (29 saves, s1/s2/s3/s12, sandbox 1, chaos slots, staff and events) was
written by our own code.
- **Placement:** `pos` holds `m_WorldXYZ.x/y/z`, including the exact height bits. The load restores all three coordinates; it assumes no terrain
  recomputation. Version 1 documents are not read.
- **Uninitialized object words:** the converter writes `moving.group` as 0 (nothing reads it) and a `moving.building` or
  `scenery.owner_building` that names no object as -1 (none; 0 is a building's id). No other old-save value is changed.
- **Traversal byte flag:** `moving.building_traversal` is an integer 0..255: zero means forwards, nonzero means
  backwards. The constructor leaves it uninitialized (`mover_lifecycle.h`), an old save holds one byte, and the only simulation read tests nonzero
  (`mover_events.h::OnMoveTargetReached`). `MT_MOVE_INSIDE_BUILDING` overwrites it with 0/1 before starting
  a building walk (`mover_messages.h`). Thus normalizing to 1 is behaviorally equivalent, covered by
  `moving_events.traversal_byte_flags_have_the_same_nonzero_behavior`, but loses byte identity. The converter keeps
  the typed byte flag exactly; all other bool fields remain strict 0/1. This is a named direction flag, not opaque
  binary state. `convert.save_round_trip_keeps_height_and_byte_flags` covers 0/1/0x4c/0xa8/255 and height bit strings.

Text decoding needs the pack language; the reader intentionally preserves the bytes. `data.convert_reads_local_save_streams` and
`data.convert_interprets_local_save_bodies` check the local `run/SaveGames/*.dat` when available. The second requires a
clean check and every record typed (fields + class records + 11 header records = all records), without committing any
saves. To make local saves that cover more classes, run a replay recording script (`tests/replays/*.record.txt`)
through `tools/autotest.ps1`, without its `record` lines and with `serial-save tmp/saves/<name>.dat` before
`quit` (autotest deletes new files in `SaveGames/`, so the copy goes elsewhere), then copy the result into `SaveGames/`.
`s12_bar` gives every staff class, sharks, vehicles and tables; the chaos host deletes its scratch slot after the round trip.
`tests/replays/s1_performers.record.txt` covers the five performer classes that no other local save holds (Cabaret,
Catwalk, WeightLifter, WetTShirt, Aerobic): a stage running a CATWALK show with the other performers in it. Its copy goes
to `run/SaveGames/s1_performers.dat`; `data.convert_interprets_performer_save` checks that file when it exists. Aerobic
has no event in scenario 1, so it is a performer only.

The launcher converts old saves at first start ([save-switch.md](save-switch.md#old-saves-at-first-start)).
- Known traps of the old format, handled in the converter and documented with it: the boat list holds raw pointers
  (dropped; rebuilt from the objects on load), the ferry section's size field is copied from `World`, the event manager and
  weather are appended without class headers, the profit/income/cost history arrays may hold uninitialized values (kept
  bit for bit), and the fence read/write bug that our C++ already fixes.
- Text in old saves is cp1250 (Polish) or cp1252 widened to 16 bits; the converter decodes it to UTF-8 with the
  language's code page, as for text tables ([assets.md](assets.md#texts)).
- Catalog positions in the old save become keys through the converted catalog, so converting an old save needs the
  converted pack (L1) of the same game version.
