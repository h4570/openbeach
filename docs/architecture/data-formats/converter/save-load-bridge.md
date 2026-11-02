# Converter: `obl.save` checks, defaults and the mapping back to old records

The converter's section and class tables ([save-conversion.md](save-conversion.md)) work in both directions. The direction
from a document back to an old save body and its record bytes exists in the converter only, where it proves the tables
lossless; the game loads `obl.save` with its own readers ([../save-switch.md](../save-switch.md)).

## Pieces
1. `EncodeOldSave` (`importer/src/readers/save/save_encode.h/.cpp`): an `OldSaveBody` as record bytes, the inverse of
   `ReadOldSaveBody`. Header records, then each section with its class record (`Class name : <kind>`; World's class
   record ends the header, EventManager and Weather have none), the objects after the ObjectManager's fields. Each
   field record keeps its own tag and count.
2. The inverse mapping `obl.save` -> `OldSaveBody`, section by section, with the values the conversion drops rebuilt or
   defaulted (`sizeof`, `m_MapName`, the boat list, the ObjectManager slot ids, identity records, text lengths, the
   repeated records). The walk of `save_body.cpp` runs over a `RecordSource`: `StreamSource` reads the old records,
   `JsonSource` (`OldSaveBodyFromDocument`, `save_writer.cpp`) answers each label through the writer's tables and rebuilds
   dropped records (section `sizeof` constants, object counts and id slots, presence flags, the second `m_DayArrived`,
   thought text lengths). Rules: text records carry no terminator (a thought's `TextLength` does count one), unused
   `m_dwObjectIDList` slots are -1, and a performer's type byte follows its `type` (the content key of its class key
   `Cabaret`, `Singer`, `WeightLifter`, `Catwalk`, `WetTShirt`, `Aerobic`). `pos` retains all three coordinates. The
   traversal direction is a typed byte flag (0 forwards, nonzero backwards), preserved exactly; normalization is
   behaviorally equivalent but cannot reproduce the input bytes (evidence in
   [../saves-old-conversion.md](../saves-old-conversion.md)). Header titles have no added terminator.
   `data.convert_save_round_trip` checks every record and the complete encoded file, including header and title, with
   no tolerated differences.
3. The game side (`src/game/game/save_load.cpp` `QueueOblSave`): reads the document, the pack identity
   (`ReadSavePackOrigin`) and `catalog/index.json` of `packs/base`, validates and checks it and queues the load with the
   header's scenario and sandbox flag (`QueueSaveLoad`, `save_slots.cpp`). `ui.save-slots-load-pending-save` takes a
   queued load before `m_loadSlot`, enters the world state as a slot load (`LoadWorld`) and builds the world from the
   document (`world::LoadFromSave`), in the same main-menu frame (`LoadPendingFor`). Test commands: `convert-save <old.dat>
   <out>` (in-process `obl-convert save` against `packs/base`) and `load-obl-save <file>` (like `load-slot`).

## Writing from the live world
`WriteOblSave` (`src/game/game/save_load.cpp`) is the game's `obl.save` writer: the save panel, `serial-save` and
`save-obl <file>` (the fixed save time of `serial-save`) call it. It builds the document from the section writers
([../save-switch-sections.md](../save-switch-sections.md)), including the fields the old records drop
([../saves-dropped-state.md](../saves-dropped-state.md)), and `WriteOblSaveDocument` validates and writes it.
`obl_save_write.txt`: a `save-obl` file loaded with `load-obl-save` has the world hash of the `serial-save` save of the same
moment, and again after 50 steps.

## Checks
`ValidateOblSaveDocument` (`src/formats/save_document.h/.cpp`, shared with the game's load) runs the `obl.save`
schema first (the checks and the mapping read required keys without testing for them), then `CheckOblSaveDocument`;
the converter's `LoadOblSaveDocument` (`save_writer.h`) adds the mapping. A refusal names the field and nothing is loaded:
- **Pack identity** (`header.packs`): every pack the save names must be installed (only `packs/base` so far), and the
  installed pack must be named. A different `content_version` or `pack_hash` of the same pack id is a warning (logged
  `obl.save <file>: ...`), not a refusal: keys resolve through the installed catalog either way.
- **Map size**: `map.cells` square and equal to the game's grid (`kGameGridSide`, 128 x 128: the map loader reads the cells of
  the scenario's map; `data.convert_maps_match_engine_loaders` holds all 12 island maps, which sandboxes reuse, at 128 x 128),
  `flags` and `heights` one entry per cell. `areas_open` is optional in the schema but refused when missing: the grid
  reads one record per area of the island, a count only the map knows. Its length is checked against the map at load
  (`ReadMapJson`).
- **Ids**: unique, from 0 below `managers.objects.next_id`; buildings first, then guests, then the rest (the order the
  rebuilt id list and counts assume), at most 500 buildings and 1500 guests.
- **References** (-1 is none): a building's `queue.owner`/`events.owner` is its own id, queue guests are guests, event
  actors and guests exist, `table.owner` is a building, repair and cleaning jobs name objects (these three may also name
  a removed object: they outlive it for a while), construction jobs name buildings and `builders[].staff` names builders
  (the same families as `CheckOldSaveBody` of the old records).

`convert_save_checks_test.cpp` has a test per refusal; `data.convert_save_round_trip` runs the checks over every local
save (no false refusal, all 128 x 128).

## Defaults
A field the schema leaves optional in the sections (`world`, `player`, `view`, `map`, `managers` and each manager's
fields, or a whole optional section) may be missing; it takes its default: 0, false, empty text, no catalog entry
(-1), an empty `map_id`, no available buildings, empty lists. Two exceptions: the ObjectManager's `next_id` is one past the
largest object id, the ferry's schedule cursor without a ferry section is none (-1). Object state takes the schema's
defaults before the mapping (`FillSaveDefaults`, [../saves-records.md](../saves-records.md), "Defaults"), so the walk finds
every object field. `convert.save_bridge_defaults_missing_optional_fields` drops every optional section key from the schema
in turn; `convert.save_bridge_defaults_missing_object_state` loads objects of identity fields alone and drops each object
field in turn.

## Tests
`convert.save_encode_reproduces_synthetic_saves` (builder saves with managers and every object group) and
`data.convert_save_encode_reproduces_local_saves` (every `.dat` of the local `SaveGames/`, skipped without them): reading
and encoding gives the input bytes exactly. `data.convert_save_round_trip` (local saves): save -> document -> body gives
the same records and `EncodeOldSave(back)` equals the complete input file.

Saves, converted documents, snapshots and logs stay local; none are committed.
