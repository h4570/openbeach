# Data formats: saves

The game reads and writes `obl.save`; the converter imports player saves into this document format.
Saves are player data, but imported saves may contain source texts such as feedback messages. Committed test saves
therefore come only from our own packs.

## Topics
- [saves-dropped-state.md](saves-dropped-state.md): state the old save writer drops and the optional fields that keep it.
- [saves-records.md](saves-records.md): named states, records and lists, and the defaults of missing fields.
- [saves-old-reader.md](saves-old-reader.md): reading the bytes and body of an old `save<N>.dat`, and the checks.
- [saves-old-conversion.md](saves-old-conversion.md): `obl-convert save`, its decisions and the traps of the old format.

## Goals
- Named, typed fields instead of a positional stream: a save survives added, removed or reordered fields.
- Readable and diffable without tools.
- Independent of memory layout and pointer size.
- The slot list reads the header without parsing the body.
- One field model shared with the world snapshot, so saving, loading and hashing cannot drift apart.

## File
`saves/<slot>.json` in the user's data directory, UTF-8, with `format`, `version`, then `header` before the body:
```json
{
  "format": "obl.save", "version": 2,
  "header": {
    "saved_at": 1791561600, "scenario": "s3", "sandbox": false,
    "title": "<text key>", "title_text": "Resolved title at save time",
    "day": 12, "day_time": 0.4375, "cash": 10432.5, "guests": 92,
    "packs": [{ "id": "beachlife-2002", "content_version": "1.0.0", "pack_hash": "..." }],
    "converted_from": null
  },
  "world": { "map_id": "scenario3", "game_time": 1234.5, "game_day": 12, "script_state": 3, "income_today": 410.25 },
  "map": { "cells": [128, 128], "island_size": 9966, "areas_open": [true], "cell_state": { "terrain": [[16384, 3]] }, "heights": [0] },
  "objects": [{ "id": 17, "kind": "building", "type": "beachbar", "pos": [40, 52, 38.25], "dir": "e" }],
  "managers": { "repair": {}, "feedback": {}, "cleaning": {}, "construction": {}, "weather": {}, "ferry": {}, "events": {} },
  "view": { "camera": [1520, 2210] }
}
```
- **Version 2 only.** `map_id` is the pack-relative map (`maps/<id>/`), not a source path, and no section holds a size
  record. There is no reader for version 1 documents: the schema refuses them, and old `save<N>.dat`
  files convert directly to version 2.
- **Header first.** The writer always emits `format`, `version`, `header` first; the slot list reads with a streaming
  (SAX) parser and stops after `header`. `title_text` lets the list show a title when the text table is missing;
  `packs` names the content the save needs (a different catalog warns, a missing pack fails with the pack's name).
  `converted_from` is `{"format": "beachlife-1.6", "file": "save3.dat"}` for a converted old save.
- **Exact numbers.** `f32` fields use the float rule of [principles.md](principles.md#conventions): the shortest
  decimal that round-trips to the same bits, parsed from the raw token with `std::from_chars` into `float` (nlohmann's
  SAX interface passes the token text), never through `double`. `-0.0` is written as `-0.0`. Values JSON cannot hold
  (NaN, infinities, and any float whose bit pattern matters, such as uninitialized history entries of old saves) are
  written as a string `"f32:7fc00000"`. Integers above 2^53 do not occur (`u32` at most).
- **Types come from the field table**, not from JSON: the native model's serializer declares each field's name and
  type (`bool`, `i32`, `u32`, `f32`, `string`, arrays, objects) and reads exactly that type; a wrong JSON type is a load
  error naming the field.
- **Bulk data as arrays.** Per-cell grid data is one array per field (16384 entries), not one object per cell, so a
  typical save stays a few MB and loads in well under a second. No compression.
- **Map cells.** The cell words are `map.cell_state`: one run-length list of `[count, value]` per named field, in cell
  order, each covering every cell (a load refuses another total). `src/formats/cell_state.cpp` holds the table of the 32
  bits; the game keeps the words in memory, and `SlotsToRecords`/`RecordsToSlots` change the form. `terrain` stays a
  number because its bits combine (1, 2 land kinds, 4 shallow, 8 deep), `neighbours` is a byte, `grass` and `surface` are
  numbers; `margin` (a footprint value) and `unnamed` never occur in the corpus's maps. A building's `placement` and
  `work` footprint layers use the same field names, one record per cell ([game-data.md](game-data.md)).
- **Same fields as the world snapshot.** The snapshot (world hashes) emits records of section, field, type and bytes. It is the
  documented projection of the state (exclusions as in [../../testing/world-hashes.md](../../testing/world-hashes.md)),
  so the hash does not depend on how a save is stored.
- The camera, open GUI windows and other non-simulation state are their own keys (`view`, `gui`), outside the snapshot.
- References to content use keys (`"type": "beachbar"`), never catalog positions or pointers; object ids stay the object
  manager's ids. `type` is the object's content key and `created_as` the key it is created by when that differs (a
  prop variant: `flower07` for `flower`). No spelling is saved: a load's object name is `type`, name compares fold case,
  and the world hash folds case and spaces.
- Unknown keys are skipped with a log line; missing fields take documented defaults. A field whose meaning changes gets
  a new name; `version` changes only when the document structure changes.
- Determinism: the RNG state becomes part of the save once the engine owns its RNG
  ([../../testing/determinism.md](../../testing/determinism.md)); until then a loaded save continues as the source does.

## Initial schema and implementation boundary
`data/schemas/save.schema.json` defines the envelope, pack identity, scalar world state, histories, grid arrays,
object identity/placement and camera. `tests/schema_samples/save.json` is entirely hand-made; it exercises signed
zero and bit strings without carrying any source content. `schema.save_rules` tests invalid types, ranges, keys
and float encodings as well as the converted-origin header.

Object subtype state and manager fields are named; no section accepts opaque binary state. The load defines the defaults
of missing optional section fields and checks array lengths against the map size, unique object ids, object references
and pack availability ([converter/save-load-bridge.md](converter/save-load-bridge.md), "Checks" and "Defaults"). Schema validation stays strict about unknown keys for writers; a future loader
may skip them as described above. Key order and exact float token parsing are writer/reader responsibilities.

## Converting old saves
The converter reads `save<N>.dat` files in two steps: the stream and body readers ([saves-old-reader.md](saves-old-reader.md)),
then `obl-convert save`, which writes a validated `obl.save` ([saves-old-conversion.md](saves-old-conversion.md)).
