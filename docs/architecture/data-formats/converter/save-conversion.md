# Converter: old saves to `obl.save`

The mapping of `obl-convert save` (`importer/src/readers/save/save_writer.h/.cpp`): every record of an old save body (read by
`save_body.cpp`, [saves.md](../saves.md#converting-old-saves)) is mapped to an `obl.save` field or deliberately dropped.

`obl-convert save <save<N>.dat> <pack directory> <out.json>` (`importer/src/readers/save/save_writer.h/.cpp`) reads and checks the body,
maps it through `<pack>/catalog/index.json` (from `obl-convert catalog`) and the pack identity of `<pack>/pack.json`, validates against `obl.save` and writes the
file. It writes the header, `world`, `player`, `map`, all `managers`, `view` and every object's identity with its shared
state and its class state (every type byte has a table).

Source-format field labels in the reader tables are literal input grammar, not C++ member names. Keep their spelling
when renaming code identifiers; the separate output keys already follow the `obl.save` schema. The provenance checker
includes string literals in its member-name counts, so those counts alone do not identify code to rename.

Decisions:
- **Section tables:** each converted section has one table (`kSectionTables`: World, ObjectManager, Player, ZEngine)
  that maps every label to a key or drops it; an unknown label fails the conversion, and an indexed label
  (`burnqueue entry[3]`) matches the entry `burnqueue entry[]`, whose records form one list. A key `list[].field`
  places a list element's field (`BuildingJob[2].RepairID` at `building_jobs[2].object`); the list's size record (`Map::List`)
  makes the list, so an empty one is `[]`; `list[]` makes the record the element itself, and `a.b` nests objects
  (`m_rStrikeArea.top` at `strike_area.top`). Each `[]` level takes the label's next index (`Job[2].Hoarding[1].x` at
  `jobs[2].hoardings[1].x`); a list without a size record is made by its first element. A label the source writes
  twice per element has one table entry per occurrence, in order (`m_fConstDone`, `m_BuildingReady`); any other repeat
  fails. Object ids stay ids (`-1` none, `$defs/object_ref`).
- **World:** dropped are `sizeof`, `m_BoatList`, `m_NumBoats`; `m_MapName` becomes `map_id`, the lowercase stem of its `.biz` path (any other non-empty path fails the conversion). `m_LastGameDayTime` (hour of the previous frame)
  is `last_day_time`, `m_lastGameDayTime` (hour the day cycle last checked) `previous_day_time`, `m_averageTotty`
  `average_attractiveness`, the four `m_objectiveStatus` `objective_status`. `m_ValidBuildingIDs` up to
  `m_NumValidBuildings` become `available_buildings` keys (catalog `buildings` position = WorldParams building id); a
  duplicate fails.
- **Header:** `scenario` is `s<N+1>` (`sandbox<N+1>` for sandbox slots, checked: sandbox 1 is index 0 with title `MAINSS_SANDBOX_1_TITLE`), the scenario index being zero based. The old
  title is the scenario title's text key; it goes to `title`, and `title_text` is its value in the pack's text table
  (first value per key), so the slot list reads without the table; a title that is no key or a key the table lacks keeps
  the stored text. `packs` is the pack's identity from `<pack>/pack.json` ([pack.md](pack.md)): `id`,
  `content_version`, `pack_hash` the SHA-256 of `pack.json`. The pack's `language` picks `text/<lang>.json` and the code
  page of all narrow save text (title, feedback, thoughts): Windows-1250 for `pl`, Windows-1252 otherwise, undefined bytes
  keeping their C1 code point like the text tables ([texts.md](texts.md)). A pack without `pack.json` or without the
  table of its language is refused (`ReadSavePackOrigin`), so `obl-convert pack` runs before `obl-convert save`, and converted saves go outside the pack directory (inside it
  they would enter the next `pack.json` and make its hash stale).
- **ObjectManager** (`managers.objects`): `next_id` and `keyline`; the slot counts and the id per slot follow from the
  objects and their order, so the loader rebuilds them: CheckOldSaveBody checks the counts, the converter checks that
  the used slots of each group (buildings from 0, guests from 500, others from 2000) hold the objects' ids in file order. **Player:** `name` (UTF-16 to
  UTF-8) and `cash`; the header's cash is the slot copy. **ZEngine** (`view`): `camera` (`m_MapX/Y`, top-left corner in
  map pixels), `position` and `velocity` (floats), the scripted move (`moving`, `move_*`), `locked`, `burn_queue`,
  `last_unburn_time`, `guests_drawn`.
- **Named states** (`Map::State`, [saves-records.md](../saves-records.md#named-states)): a table entry names its set; the converter writes
  the name and keeps a number the set does not name. `base.state` takes its names from the pack's object vocabulary, read
  with the pack identity. `kObjectClasses` maps the old staff and performer type bytes to `class`.
- **Repair/cleaning** (`managers.repair`, `managers.cleaning`): the waiting and taken job lists by object id, the
  counters. A repair job's `type` is a named state.
- **Events/weather** (`managers.events`, `managers.weather`): the disaster schedule cursor and the running events by
  schedule index; the light cycle, lightning and the active weather; `state` is the named weather kind. The tables keep
  fixed arrays (`desire_modifiers`, 26 entries); `formats::SlotsToRecords` turns them into the file's records and lists
  ([saves-records.md](../saves-records.md#records-and-lists)).
- **Ferry** (`managers.ferry`): `boats` (schedule) and `performances` share `$defs/boat`; `next_boat` is the schedule
  cursor as an index (-1 past the end); a boat's `guests_by_type` holds guest type names verbatim
  with `count` and `random` (count is a random bound); `departures` the per-guest-slot statistics.
- **Feedback/messages** (`managers.feedback`, `managers.messages`): feedback `object` is the object id it is about (negative for
  world notices), `hour` the history entry's game hour, `text` the shown text (`wide` whether it was UTF-16); counters and the 100 suppressed ids. Delayed messages keep sender, receiver,
  `type` (engine message type) and payload as numbers, since senders/receivers include fixed engine receivers.
  Source texts never go into committed samples.
- **Construction** (`managers.construction`): jobs by `building` object id with `hoardings` (second list level),
  times, progress (the second `m_fConstDone`/`m_fDemolishDone`), `started`/`finished` (the two `m_BuildingReady`), ten
  `work_points`; `builders` by staff object id. The builder count record is the list's size record: the source writes
  the count as is and at most 50 entries, so a count above 50 is not kept.
- **Object state:** an object's records are its class fields, then Prop (scenery, buildings, tables) or
  Mover, then Entity with its sprite; the reader marks where the shared layers start (`OldBlock::sharedFrom`).
  They go to `sprite`, `base`, `static` or `moving` (with `path` while walking), class state to a group named after the
  class (`scenery`); a `Map::Group` entry maps a label prefix (`sprite.`, `path.`, or `""` for the Entity rest)
  through a sub-table. Identity records are dropped (`type`, `id`, `pos`); `m_bType` must equal the type byte; `Object
  Name`, the name the load creates the object by, becomes `created_as` when it differs from `type` (map props:
  `LOWLIGHT02` for `lowlight`). The spellings of `m_Name` and the Object Name are not kept: `type` and `created_as` are their
  content keys, a load's `m_Name` is `type`, name compares fold case and the world hash folds case and spaces. Sprite names
  (`base.sprite`, an attached `sprite`) and the ferry keywords stay verbatim. `sprite.dir` is the facing, left out for buildings (their `dir`). Prop's
  repeated call labels are `called_*` then `urgent_*`. Fields the constructor leaves uninitialized do not
  survive: `moving.group`, which nothing reads, is written as 0, and a `moving.building` or `scenery.owner_building`
  that names no object in the save as -1, so these are the only words a round trip does not keep. -1, not 0: 0 is the first building's id.
  The walk point lookup keeps the stored destination for it and a post to it is dropped. Object ids that the local saves always resolve use `object_ref`.
- **Class tables** (`kClassFields`, one per type byte; a class label without a table entry fails): scenery; staff as
  the profession group (`service` for service staff and the waiter, `lifeguard`, `mechanic`, `rep`, `builder`,
  `cleaner`, `security`) plus `staff` (Staff: `home` building, thoughts as shown text); `shark`. An attached
  script sprite (lifeguard, rep) is `<group>.attached` (sprite name + sprite state, `kAttachedSpriteFields`). Class-wide
  counters every member writes (`m_sMechanicCount`) are `class_count`; the repeated security `m_FineOffsetY` is the
  offset's `z`. Ids in class state stay numbers: some hold stale or uninitialized values (a lifeguard's `saving_guest`).
  `lifeguard.influence_area` is a swimming area index, not an object id. `vehicle` (passenger slots, 64 way points,
  `sprites` present among five, `vehicle_type` an enum) and `table` (`chairs` with their sprite when the table has
  chair sprites, waiter fields, `plates`) use list Groups (`sprites[].`, `Chair[].sprite.`); passenger slots past the
  load and boat owners hold stale values. `guest`: engine-slot arrays (`desires` 26, `person`, `opinion`, `tolerance`),
  ids as numbers (`best` starts at -2, partners keep stale ids), `best_type`/`worst_type`/`current_type` are catalog
  building positions (`BuildingKey`) and become keys (`Map::Building`, -1 null, an unknown position fails; schema
  `$defs/catalog_ref`); the second `m_DayArrived` is the same field and must equal the first. `building`: the staff,
  vehicle, table and lounger slots are object ids (numbers; most slots hold stale values), the five commodity arrays
  and the queue's `commodities` are definition slots, `beer` (`Map::Beer`) and the show slots' `event` (`Map::Event`)
  catalog keys; `sprites` the animation sprites; `events` (show schedule, three slots with actor and guest lists) and
  `queue` only when present (their presence records are dropped); a thought's `TextLength` must be its text's length
  plus the terminator, then it is dropped.
  All performers share one `performer` group (`owner` the show building, `finished` from m_bFinish or m_finished,
  `contestant` for the wet T-shirt, `action_point` except the cabaret, `attached` for singer and weight lifter).
- **Map:** the old save has no grid size; the cell count must be a square (128 x 128). `flags` and `heights` are the
  per-cell `m_flags` and `m_zOffset`; `areas_open` holds the beach then sea area `Closed` values as booleans (the
  load opens an area for any nonzero value), `island_size` the counted land/water tiles. There is no per-cell path array:
  path state lives in the flags.
- **Objects:** in the old ObjectManager order (buildings, guests, then the others), which the loader may rely on. `id` is `m_dwID`;
  `pos` is `m_WorldXYZ.x/y/z`, with all float bits retained.
  `type` is the lowercased `m_Name`; a name of a key-only kind may hold other
  characters (map props `Palm 4s`, `Flower 5`), and each run of them becomes one `_` (`palm_4s`). The map/prop converter
  must key props by the same rule. Buildings, vehicles, tables and staff must
  resolve in the catalog list of their kind (the restaurant `waiter`, type 19, has no staff definition); guest types
  (scenario guest set), scenery (catalog entries, map props such as `palm`, code-made `litter`/`lowlight`), sharks
  (`animal`) and performers are keys the loader checks. Only buildings get `dir`: the sprite direction `m_iCurrDir`
  (0..3 quarter turns, `place-building dir`) as `n`, `e`, `s`, `w`; other kinds keep their facing in their own state.
