# Data formats: reading old saves

Part of [saves.md](saves.md). The reader works on the input bytes of an old `save<N>.dat`; it decodes no text and
uses no catalog.

Implemented first step: `importer/src/readers/save/save_stream.h/.cpp` reads borrowed input bytes without allocating arrays from
file counts. `SaveStream::Next` checks all 14 tags, signed counts and payload bounds (bool bytes other than 0/1 occur in real saves and are kept raw); clean EOF returns
false with an empty error, malformed input returns an offset-qualified error without advancing the cursor. Records
borrow the input buffer, which must outlive them. `Bits(index)` retains little-endian integer and float payloads exactly;
`Text()` exposes raw bytes (including embedded NULs), not decoded UTF-8. No game layouts or Windows APIs are used.

`ReadOldSaveHeader` requires the stream/save signatures, exactly the float bits of version 1.6, nine typed slot fields
and the World class marker. It accepts plain class names and the old `Class name : ` prefix. It retains the unsigned
timestamp, zero-based scenario, day, day-time/cash bits, guest count, raw narrow title and sandbox flag. Unknown versions
(including NaN) are rejected even though the old slot scanner's float comparison could accept them. Header failure
clears the result but may advance the stream; construct a new cursor to retry.

`importer/src/readers/save/save_body.h/.cpp` interprets the body. `ReadOldSaveBody` follows the order of each old Save method
with typed field tables (label, tag, exact array length), one table per emitter part. The labels are the emitters' own,
repeated ones included. List elements get an index prefix (`Job[2].BuildID`), and nested records get a part prefix
(`sprite.`, `path.`, `queue.`, `events.`, `attached.`). The result is one block per section and one per object, in
file order. The EventManager and Weather tails have no class record, so they become blocks of their own. Any record
whose tag, array length or count does not fit the emitter fails with the offset, block and field, for example
`offset 437859: StaffObject.m_happiness: expected float, found int`. So does a missing section or a record after the
weather. Counts are checked against the remaining input before any loop. How the reader decides each part:
- Objects follow the ObjectManager header counts: buildings first, then guests (both loaded as their class whatever the
  type byte says), then the others by type byte. Type 19 (`SnootyStaffObject`) saves through the service staff methods.
- Parts that depend on the catalog are decided from the records themselves. That covers a building's animation sprites
  (sprites start with a float, the flags after them are booleans), a vehicle's present sprites, a table's chairs (a
  chair is bool/int/uint/uint/bool, the waiter fields bool/int/uint/int/bool) and plates, and the grid's beach/sea
  `Closed` flags (one label for both: the split needs the map).
- Optional parts follow their flags: `on path` (route and cursor), `Events`, `m_QueueValue`, attached sprites, and a
  feedback text that is UTF-16 or a text key.

`CheckOldSaveBody` checks the counts and references without the catalog:
- object count against `m_dwNumActiveObjects`, and the type bytes of the buildings and guests;
- unique ids below `m_dwNextFreeObjectID`;
- references to live objects of the right class: construction job buildings and builders, building queue guests, event
  actors and guests, table owners and repair/cleaning job ids (these may also name a removed object: a table or a job
  outlives its object for a while), and the queue/event owner being the building itself.
  `-1` means "none".

`obl-convert save-info <save<N>.dat>` prints the header and record counts (files over 64 MiB are refused before
reading). With `--objects` it adds the sections with their record counts, the objects per class and the problems.
Keep source field positions, pointer-dropping quirks and catalog-index mapping in the converter; never encode them
as opaque fields in `obl.save`.
