# Saves in the game

The game reads and writes only `obl.save`, through per-section serializers. The converter handles binary save input
outside the game. Format: [saves.md](saves.md); conversion: [converter/save-load-bridge.md](converter/save-load-bridge.md).

## Contracts
- The game's writer and loader are `game/game/save_load.cpp` and the world section and object JSON readers and writers.
  One field table per section or object class (name, type, accessor) drives the JSON writer, the JSON reader and the
  schema-2 snapshot emitter. `QueueOblSave` validates a document and loads it.
- The **old binary format lives only in `obl-convert`**: `save_stream`/`save_body` (reader), the mapping tables of
  `save_writer.cpp` (records -> document). The `obl.save` checks and pack identity are the shared
  `src/formats/save_document`. `EncodeOldSave` and `OldSaveBodyFromDocument` belong to the converter, where
  `data.convert_save_round_trip` proves the mapping lossless; the game links none of them.
- The **world hash has its schema-2 records** (section, field, type, bytes): the emitters of `world_snapshot_model.cpp`
  read the live state, not a save, so the save format does not affect hashes or replay references.

How the writers and readers of each section work: [save-switch-sections.md](save-switch-sections.md); the `objects`
readers: [save-switch-objects.md](save-switch-objects.md).

## Save panel
- **Write:** `save_slots.cpp` `Write` calls `WriteOblSave` for `savegames/save<N>.obl.save` and removes an old
  `save<N>.dat` of the same number, so an overwritten slot is listed once. A refused write is logged (`save slot N: ...`)
  and leaves no file.
- **List:** `Scan` lists only `save*.obl.save` and reads each summary with `save_slots::ReadOblHeader`
  (`src/game/ui/save_slot_header.cpp`, an nlohmann SAX handler that stops when the `header` object closes, so the sections
  are never parsed; it needs `format` `obl.save` and `version` 2 first). `m_scenario` comes from the header's
  `s<N>`/`sandbox<N>`, the title from `title` (the text key) or else `title_text`.
- **Load / delete:** a slot loads through `QueueOblSave`; delete removes the slot's own file
  (`save_slots::SlotPath(const Slot&)`). Free-number searches of the test commands use `SlotFileExists`.
- **Load cost** (Debug, scenario 1): reading the document and catalog 0.5 s, validation 0.6 s. Validation is fast because
  `json_schema::Registry` caches each schema node's keywords and resolved `$ref` and builds the JSON pointer only for an
  error.

## Test commands
- `serial-save [copy]` writes the scratch slot `savegames/save<N>.obl.save` (N from 9000) with `WriteOblSave` (fixed save
  time 1790841600), as the panel does; `serial-load` loads it through the slot list.
- `serial-load <file>`: an `obl.save` goes through a scratch slot; an old `.dat` is converted there in process through
  `obl-convert save`. `start save` (replay and headless) converts a `.dat` to `testlogs/old_saves/<name>.obl.save`.
- `save-confirm` is the save panel, so its tests round-trip `obl.save` too. `obl_save_write.txt` compares a `save-obl`
  file with a `serial-save` one; `replay_obl_save.txt` records from an `obl.save`.
- The building-event check captures the live events before a write (`CaptureBuildingEvents`); the next load compares them
  (`ForgetBuildingEvents` for other loads).
- Replay starts: `headless_save_s1.replay` starts from `save_s1.obl.save` written by `headless_save_fixture`;
  `s1_event_price.replay` starts from `replays/s1_stage.obl.save`.
- Test cleanup of the shared save folder: the harness removes every file a run created in `SaveGames/` whatever its
  extension (`autotest.ps1`, `obl_sim_tests.py`; runner slots start empty), and the in-game fixtures clean up after
  themselves: `FixtureDirectory` for the `.dat` scan and delete fixtures, `NewSavesRemoved` for the write fixture (the
  writer reads the pack relative to the game directory, so it cannot run in a changed working directory),
  `serial-cleanup` or a reset for the scratch slots.

## Test references
- **World-hash references** (`tests/replays/`, `tests/golden-hashes/`) do not read a save; a hash changes only where
  `obl.save` keeps state the old save dropped and a test loads a save.
- **Round-trip tests** (`serial-save` + `serial-load`) keep the state of
  [saves-dropped-state.md](saves-dropped-state.md) across a load.
- **Converter tests:** the `convert.save_*` unit tests and `data.convert_save_round_trip`.

## Old saves at first start
A first-start step, outside the game itself, runs `obl-convert save` for every `SaveGames/save<N>.dat` without an
`.obl.save`, so the game stays free of the old format and players lose no saves. The converted `.dat` files move into
`SaveGames/legacy/`, so the game's save directory holds only `obl.save` files. A failed conversion leaves its `.dat` in
place and reports it. The browser build has no such step: its importer converts saves.

`tools/convert_old_saves.py <save dir> <pack dir> --converter <obl-convert>` implements this through a `.partial` file
(renamed only on success), numbered archives when needed, and exit code 1 on failure. Ctest `converter_old_saves` checks
a corpus copy, a broken slot and an existing `obl.save`; it skips without `run/SaveGames/save1.dat` or the pack.

`launcher::ConvertOldSaves` (`src/launcher/old_saves.{h,cpp}`) runs in the standalone host before the game starts, once
per launch. It uses the resolved game directory's `SaveGames`, `gameDir / pack_path::Selected()` (`packs/base` or
`OBL_PACK`, validated with `CheckSelected` and a `pack.json` existence check), and `obl-convert.exe` beside
`OpenBeach.exe` (development: `build/x64/bin/Debug`). Installed builds must ship the converter there.
Converted and failed names are logged to stderr with an `old saves:` prefix. Missing prerequisites, failed slots and
filesystem errors never stop startup; the game's own pack checks still apply.

Hidden converter processes time out at 300 seconds per slot. Failures remove partials and keep the source files; archive
failure rolls back the output for retry. Hosts with `OBL_SCRIPT` or `OBL_HEADLESS` set skip conversion: automated tests
share the development save corpus, which conversion must not move. This covers `--script`, headless scripts and inherited
script environments, plus headless replays and bots. The `standalone_host` no-entry ctest sets this marker too; ordinary
no-entry launches still exercise conversion. Interactive launches convert.

`--keep-saves` skips the conversion as well (`tools/capture.ps1` passes it, so runs on `run/` keep the test save corpus);
a `save*.partial` left by a killed start is deleted on the next start. Ctest `launcher_old_saves` shares the corpus
assertions, plus launch failure and timeout cleanup. `launcher_startup_old_saves` starts the actual launcher and child host
with a temporary game and save directory and a junction to a selected pack: it checks script and headless skipping, corpus
conversion and archive, a broken slot, a missing pack and a missing sibling converter. It skips without the corpus or
converted pack. No game entry runs and no shared saves change.
