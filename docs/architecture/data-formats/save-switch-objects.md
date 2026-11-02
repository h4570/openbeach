# Saves: the `objects` readers

Part of [save-switch-sections.md](save-switch-sections.md), which has the section readers and their value rules; this
topic covers the per-object readers.

The object manager's load is a walk: the manager's own block, then per object its header (type and name) and its body.
Everything comes from the document:
- **The manager's block** (`ReadObjectManagerJson`, `save_objects_load.cpp`) comes from `managers.objects` and the
  `objects` list: the counts and id slots per list (buildings from slot 0, guests from `kGuestBase`, others from
  `kOtherBase`, unused slots -1), `next_id` (missing: one past the largest id) and `keyline`. The `objects` order must be
  buildings, guests, then the rest, and a full list is refused, so the n-th constructed object is `objects[n]`.
- **Object headers** (`ReadObjectHeaderJson`): the type byte from `kind` (staff by the group object it carries, `service`
  with type `waiter` is SnootyStaff 19; performers by their `type` key), the converter's `ObjectTypeOf` rule; the creation
  name is `created_as`, else `type`, narrowed to the pack's code page and cut to 127 bytes.
- **The walk** (`object_manager_serial.h`, `object_manager_lifecycle.cpp`) calls `ApplyHeader(obl_load::ReadObjectManager())`
  and `LoadObjects`, whose environment reads `objects[n]`'s header and calls `obl_load::ReadObject` (type from the header).
  Construction, `Adopt` and `Index` before the body stay in `LoadObjects`.
- **Checks before queuing:** `CheckSections` runs `ReadObjectManagerJson`; `CheckObjects` reads every entry's header and
  compares it with the converter's mapping (type byte and object name) before the scratch body read, and an object type
  without a reader is refused. Every object's reader then runs once on a scratch object of its class (scratch sprite, cursor
  freed after), so a refused value fails `load-obl-save` before anything is queued; during the load a refusal is a fatal
  error, as for the sections.
- **Readers** (`save_objects_load.cpp`) are the inverse of `WriteObjectJson` over the same `VisitFields` tables and keys
  (the writer's `Place` key forms: `a.b`, `list[]`, `list[].field`, occurrences of a repeated key), layer by layer: the
  class fields, the moving or static layer, then the base (`VisitHead`, the draw fields, the sprite). What a load does
  besides storing fields goes through `ObjectLoadCallbacks` from the game loader, in the same place of the walk: the
  position to the virtual `SetPosition` (grid occupancy and terrain height, after the three floats and before the state),
  the sprite created by name when the object has none (`SpriteBankCreateSprite`) and then filled; a route in progress
  (`moving.path`) is built as a new `Route`, a reverse cursor, then the record's `reverse` and `current_step`.
- **Links by id.** Ids are stored as numbers and resolved later (each class's post-load or first update), so nothing is
  resolved in the reader; a reader that needs another object (a building's queue, a guest's building) finds it already
  constructed and indexed, because buildings come first.
- **Trap:** an object's type field is a record of its body (`VisitHead`), so the walk decides by the header's type byte and
  `ReadObject` sets the field from it before the reader's class switch.

## Per family
- **Shark** (`kind` `animal`): class fields, the moving layer with its route, the base and sprite.
- **Vehicles:** the class's own `vehicle::Fields` chain with the reader as env: the sprites the constructor made, then
  `RestoreCircuit`, then the moving layer.
- **Performers** (six classes, `performer::Fields`): the attached sprite is made by name for the weightlifter and filled
  for the singer's constructed one; `action_point.z` reads into the y word.
- **Tables** (`table::Fields`): chairs and plates as the definition has them, each constructed sprite's records, then the
  static layer. The pre-queue check has no definition, so it checks the table's scalars and the static and base layers only.
- **Scenery:** the fields, `ScenerySetDraw` before the static layer and `SceneryJoinFootprint` after it.
- **Staff** (every profession and the waiter as service staff): the profession's keyed table, the staff table, then the
  moving layer, because the loader needs the keys. `Counter` goes to the `staffCounter` callback, which sets
  `CounterValue(key)` in the game and nothing in the pre-queue check; `MaskBit` sets bit 0 of the bool into the mask;
  `BeachArea` asks the `beachArea` callback for the index read just before; a present attached sprite is always made new by
  name; the staff extras are applied after the load by `ApplyDroppedFields`.
- **Buildings:** the field table with the reader as visitor; `building.beer` and each slot's `event` go back to ids through
  `ReadContext::beerId`/`eventId`, null -1; the definition's animation sprites and the extra one are filled in list order;
  `under_construction`/`under_demolition` set the burn field. The schedule, the queue and the thought list are parsed into
  `BuildingEventsJson`/`BuildingQueueJson`/`BuildingThoughtJson` and handed to the `building*` callbacks, which construct
  them (`building_serial.cpp` `BuildingLoad*Json`; the events reuse SetEvent and the slot restore, the queue runs
  `LoadBuildingQueue` over the entry's values); the object's `dir` becomes the base sprite's direction after the static
  layer; the queue tail and points are dropped fields for `ApplyDroppedFields`.
- **Guests** (`save_guests_load.{h,cpp}`): `GuestFields` over `VisitFields(Guest)` reads floats and bytes, the animation
  texts, the heap name (a copy of at most 127 characters), the attached sprite (made by name when present) and the three
  building types (catalog key -> `ReadContext.buildingId`, null -1, unknown refused); the scalars go to the shared reader,
  then the moving layer.

A class whose load template has an env (`Fields(self, env)`) gets the reader as that env; post-steps that need the world
go through `ObjectLoadCallbacks`. Unit tests: `save_guests_load.*`, `save_objects_load.*` (write, read, write again;
defaults; refusals).
