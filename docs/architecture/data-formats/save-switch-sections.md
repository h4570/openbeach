# Saves: section writers and readers

How the per-section `obl.save` writers and readers of the game work. Overview: [save-switch.md](save-switch.md).

## The `world` section
`world_save::WriteWorldJson` (`src/game/world/world_save_json.h/.cpp`) visits `world_save::VisitFields` and writes each
record under its `obl.save` key (the key table there mirrors `kWorldFields` of `save_writer.cpp`): `sizeof`, `m_BoatList`
and `m_NumBoats` are dropped, `m_NumValidBuildings` + `m_ValidBuildingIDs` become `available_buildings` (catalog keys),
the objective flags one `objective_status` array, floats by the float rule (`core/json_float.h`, shared with the
converter). Tests: `world_save_json.*` unit tests and `obl_save_write.txt`.

## The other sections
`src/game/world/save_sections_json.h/.cpp` holds one writer per section after `world`, with the keys and value rules of
the converter's tables in `save_writer.cpp`; `WriteDocument` (`save_load.cpp`) assembles them with the header
(`WriteHeaderJson`), in the converter's key order, and the fields of
[saves-dropped-state.md](saves-dropped-state.md) come from the section writers after each section's saved records.
The converter places a list without records (an empty `burn_queue`) after the section's other keys, and the writers keep
that order: `player` (name to its first NUL, cash), `view` (pairs, burn queue, bools refused above 1), every manager
(`managers.*`: feedback, messages, repair, cleaning, construction, ferry, events, weather, objects), `map` (`WriteMapJson`:
beach then sea areas' open flags, cell flags and heights; a grid that is not square is refused like the converter does).

The managers share `TableWriter`, a JSON visitor over the module's `VisitFields` with a label -> key table and the
converter's key forms (`a.b` nested, `list[]` one value per occurrence, `list[].field` one element per occurrence); a label
that repeats in one scope (each list's "List size", the message's two `fTime`, construction's `m_fConstDone`) has one entry
per occurrence in record order.

`objects`: `save_objects_json.cpp` (`WriteObjectJson`, `HasObjectWriter`) writes an entry's identity (id, kind, content key
by `core/content_key.h`, engine name, pos, creation name), then the class's `VisitFields` (whose entries carry their
`obl.save` keys), the moving or static layer and the base with its sprite (kBaseFields, kSpriteFields). `WriteDocument`
writes the live objects in the object manager's save order (buildings, guests, others). Per family:
- shark;
- vehicle (`vehicle.sprites[]` one sprite per present sprite);
- performer (`performer.attached` the attached sprite's name and records);
- table (chairs and plates with their sprites, then the static layer);
- scenery (rectangles as `left`/`top`/`right`/`bottom`, `created_as` from the object name);
- staff (class fields, the staff layer, the moving layer; `Counter` the class-wide count from `game::Context` through
  `staff_serial.h`'s kCounters, `MaskBit` the `(mask >> bit) & 0xffffff01` test as a bool, the waiter as service staff);
- guests (`best_type`, `worst_type` and `current_type` catalog building keys through `buildingKey`, null for -1; the
  repeated `m_DayArrived` writes the same key again);
- buildings (`beer` and event slots' `event` catalog keys through `beerKey`/`eventKey`,
  `under_construction`/`under_demolition` from the ConstructionManager through the context, `sprites[]` the definition's
  animation sprites then the extra one, `events`/`queue` only when present; the sprite direction 0..3 leaves `sprite` and
  becomes the object's last key `dir` (`n`/`e`/`s`/`w`), as the converter writes it).

`moving.building_traversal` is a byte (the converter's ByteFlag). The feedback texts go through the pack's code page.
Tests: `save_sections_json.*` unit tests, `obl_save_write.txt` and `obl_save_objects.txt` (scenario 9 with its scenery,
sharks and three vehicle hires with their tables, an event's six performers and one spawned member of each staff class;
`save-obl` logs `objects=N`).

## Readers
`QueueOblSave` keeps the validated document for its load (with the catalog index and a `world_save::ReadContext`: UTF-8 to
the pack's code page as the converter maps it, building keys to catalog positions). `world::LoadFromSave` reads every
section from it (`obl_load`, `save_load.h`). Before queuing, every reader runs once on scratch state (`CheckSections`), so
a refused value fails `load-obl-save` instead of the load; the load itself treats a refusal as a fatal error.

Readers are the inverse of the writers and use the defaults of
[converter/save-load-bridge.md](converter/save-load-bridge.md) for missing optional keys (a missing optional section reads
as `{}`):
- `world` (`world_load_json.cpp`, over `world_save::VisitFields`, whose record names are the `obl.save` keys, and
  `WorldJsonKey`; the dropped boat records read as zeros, `map_id` gets the bytes and a terminator);
- `player` and `view` (`save_sections_load.cpp`; the view's burn queue is appended and the camera brought in line);
- `map` (`map_load_json.cpp`: island size, missing 0; the open flag of every beach then sea area; cell flags and height
  offsets; `CheckMapJson` checks the shape before queuing, `ReadMapJson` also the island's cell count and area count, so a
  section for another map is a fatal load error);
- the object manager and every object ([save-switch-objects.md](save-switch-objects.md));
- the managers (below).

Value rules: `save_json_values.h` (an integer record from a JSON integer or bool, a float by `json_float::ParseBits`).
Tests: `world_load_json.*`, `save_sections_load.*`, `map_load_json.*` unit tests (write, read, write again), the round-trip
game tests and the `headless_save` ctest.

**Managers.** Each manager's load is `LoadFields(m, visitor)`: the clears, its `VisitFields`, the post-steps (construction
drops the work points of a job without them; events close a running event's beer and shift the schedule days; weather
shifts its schedule days). The reader is a `world_save::TableReader<Derived>` (`save_table_reader.h`), the inverse of
`TableWriter`; each `VisitFields` record is named by its `obl.save` key: a list record is the array at its key, `a.b` nests,
`list[]` is one value per occurrence, `list[].field` one element per occurrence, an Int whose key holds an array reads its
size (`builders`). The CRTP lets a derived reader on the game side add its steps: construction's `Define` (the job from its
building's definition) and weather's `CreateLightningBolt` (`construction::runtime::LoadJson`, `weather::runtime::LoadJson`;
events go through `event_facade::LoadJson` for the schedule). Defaults: missing values read 0 or empty, the ferry's
`next_boat` kNoCursor; feedback texts are refused at 512 wide units or 128 bytes, keywords at 64 bytes. Repair, cleaning,
messages, feedback and ferry are checked on scratch managers by the readers themselves; events, weather and construction
only by `Check*Json` (their post-steps need the live schedules and objects). The ObjectManager section
(`managers.objects`, `next_id` missing -> one past the largest id) loads with the objects. Tests: `save_table_reader.*` unit
tests.
