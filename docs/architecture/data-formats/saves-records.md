# Data formats: saves, named states, records and defaults

Part of [saves.md](saves.md): the names that both sides use, the file form of lists, and the defaults of missing fields.

## Named states
`src/formats/save_states.h/.cpp` holds the names both the game and the converter use, and `save.schema.json` lists them:
- `managers.weather.state`, `previous_state`: `heat_wave`, `normal`, `overcast`, `light_rain`, `heavy_rain`, `thunderstorm`.
- `building.repair_state`: `sound`, `worn`, `very_worn`, `broken`; `building.open_status`: `open`, `closed`.
- `managers.repair.*_jobs[].type`: `inspection`, `scenery`, `breakdown`, `watering`.
- `managers.construction.builders[].status`: `available`, `unavailable`.
- `moving.path.type`: `pavement`, `land`, `shallow_or_land`, `sea`, `land_or_blocked`.
- `vehicle.vehicle_type`: `jetski`, `speedboat`, `ferry`, `banana`, `pedaloe`, `coach`, `fishing_boat`.
- `base.state`: the state name of the pack's `scripts/object_vocabulary.json`, so object states follow the pack.

A value without a name stays a number (a stale word of an old save, or a state the pack does not name), so a round trip
keeps every value. Staff and performer objects carry `class` (`service`, `waiter`, `lifeguard`, `mechanic`, `rep`,
`builder`, `cleaner`, `security`, `cabaret`, `singer`, `weightlifter`, `catwalk`, `wettshirt`, `aerobic`): the loader
refuses such an object without it, and only the converter knows the old type bytes. Save documents stay `version` 2
with no reader for the numeric staff identity: a document written before this change is converted again.

## Records and lists
A file never holds a positional slot array. `src/formats/save_records.h/.cpp` is the one definition of the file form:
`SlotsToRecords` is the last step before a document is validated and written (game writer, converter), and `RecordsToSlots`
the first step after validation (game load, the converter's document-to-body mapping). The serializers, `VisitFields` tables
and old-record tables keep working on fixed arrays, so the world hash does not see this step.
- **Named records** (a missing name reads 0): `guest.person` (the guest set's preference names), `guest.opinion`,
  `guest.tolerance` (`<opinion>_low`, `<opinion>_high`), `guest.desires` and `managers.weather.desire_modifiers` (the 26
  desire names), `view.position` and `view.velocity` (`x`, `y`).
- **Lists of records:** `building.staff` (`id`, `busy`), `building.vehicles` (`id`, `in_use`), `building.commodities`
  (`intensity_male`, `intensity_female`, `price`, `price_factor`, `cost`), a boat's `guests_by_type` (`keyword`, `count`, `random`).
- **Plain lists:** `building.non_serve_staff`, `tables`, `loungers`, `profit_history`, `vehicle.guests`, `guest.inventory`,
  `guest.thoughts`, `scenery.attached_guests`, `staff.thoughts`, `managers.construction.jobs[].work_point_owners`,
  `managers.ferry.departures.average_happiness`, `managers.feedback.suppressed`, `managers.commodities.complaints`.

Every list drops its trailing unused entries and loading fills them back. Unused is 0 (`0.0` for floats, `-0.0` is a value), `""`
for texts and -1 for `guest.thoughts`, `scenery.attached_guests` and `work_point_owners`. Unused slots of old saves can hold stale
words; those are values and stay, so a round trip keeps every bit. Still positional, on purpose: `object.pos` and coordinates
(a coordinate triple), `shark.swim_rect`, `guest.rides.last`, `guest.last_ids`, `guest.last_activity`, `vehicle.way_points`,
`work_points` (fixed tables without a name per entry).

## Defaults
An object needs only its identity: `id`, `kind`, `type`, and `class` for staff and performers (the loader refuses those
without it). Every other object field that a writer always emits carries its default as `default` in
`save.schema.json`, so the schema is the table. `FillSaveDefaults` (`src/formats/save_records.h`) runs after validation and
before `RecordsToSlots`, in the game load and the converter's `LoadOblSaveDocument`: it adds the missing state blocks of the
object's kind, then the missing fields, nested records and list entries included.

| Field | Default |
|---|---|
| numbers, flags, texts, lists | 0, `false`, `""`, `[]` (a list's slots then fill as in "Records and lists") |
| object references (`object_ref`, and `moving.follow_object`, `destination_object`, `staff.goto_object`, `guest.current`, `activity_person`, `fight_person`, `staying_at`, `security.offender`, `fight_with`, `shark.victim`, `table.waiter_serving`, `chairs[].occupier`, `influence_area`) | -1, none |
| catalog references (`catalog_ref`) | `null`, none |
| `pos`, `guest.walk_points`, `vehicle.way_points` and other fixed arrays | that many zeros |
| `building.repair_state`, `open_status` | `sound`, `open` |
| `moving.path.type`, `vehicle.vehicle_type` | `pavement`, `jetski` |
| `building.queue.owner`, `building.events.owner` (of a present `queue` or `events`) | the building's own id |
| `base.state`, `sprite.dir` (unless the object has `dir`) | 0 |

Blocks by kind besides `sprite` and `base`: `building`, `scenery`, `table` have `static` and their own; `guest`, `vehicle`, `performer`
have `moving` and their own; `animal` has `moving` and `shark`; `staff` has `moving`, `staff` and its class's block
(`waiter` uses `service`). `building.queue` and `events` are optional as a whole: a building without `queue` loads without one, like a building under
construction, and guests do not queue there. A table's `plates` must stay as many as its `chairs` or be left out. The fields of [the dropped state](saves-dropped-state.md) have no default: their absence means the object keeps what
the load leaves. Entries of manager lists stay complete records; missing fields of the sections are filled at load
([converter/save-load-bridge.md](converter/save-load-bridge.md), "Defaults"). A default keeps a save loadable; it does not recreate
the lost state, so such a world goes on differently.
