# Native model: the objects

The object manager owns the simulation objects and provides queries over them. Grid cells refer to objects by
handle, and moving objects own their route cursors. Object implementations live in `src/game/objects/` under
`obl::object`; they name game services explicitly with `game::` qualifications.

See [map.md](map.md) for grid and route services, the tables module for object definitions and
[../specs/index.md](../specs/index.md) for behavior contracts.

<a id="steps"></a>

## Ownership and identity
The manager creates objects, assigns their ids and maintains the lists used by simulation queries. Scenery belongs
to the manager too: a grid tile list does not own its objects. Saved cross-references use ids rather than pointers.
Queries return vectors, so a caller that needs to retain results keeps its own copy.

`object::Base` provides shared name and position state and virtual dispatch. Moving objects add route cursors, speed
and queue state; guests, staff, performers, vehicles and sharks provide their specialized behavior. Static objects
include buildings, scenery and tables. Building state includes queues, events and event actors.

## Routes and shared records
Routes store tile steps in a vector and are shared through `std::shared_ptr<const Route>`. A moving object's cursor
tracks its position along the route. Object creation parameters, script values, command payloads and messages use
named C++ records shared with their callers.

## Persistence and verification
Object serializers write named fields to `obl.save` and restore object references by id. See
[data-formats/saves.md](data-formats/saves.md) for the document contract. Snapshot emitters expose deterministic
state for [world-hash checks](../testing/world-hashes.md); route progress, object identity and saved fields must survive
a save/load round trip. Object, movement and staff tests cover the behavior described in the specs.
