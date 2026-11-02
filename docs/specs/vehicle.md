# Spec: vehicles (boats, coach, water toys)

## Purpose
Vehicles carry guests: ferries, coaches, jet skis, speed boats, pedalos, banana boats and fishing boats. A building spawns
them; they move along routes or circuits, load and unload guests, and turn at docks. Test names below are quoted as they
appear in `tests/unit/vehicle_*_test.cpp` (the file in brackets).

## Vocabulary
- **Vehicle type**: a number from the definition key (rule 1).
- **Circuit**: a loop of points that circuit vehicles (jet ski, speed boat) follow; the lane cells around a dock are blocked
  while a vehicle uses them.
- **Unload / way point**: where a vehicle delivers guests / the next stop of a route.

## Rules
1. The key decides the type and a target tolerance: `PEDALOE` (exact case) 4 (tolerance unchanged), `JETSKIVEHICLE` (exact
   case) 0 (10), `BANANA` 3 (unchanged), `SPDBOAT` 1 (12), `BOAT` 2 (12), `BUS` 5 (8), `FISHBOAT` 6 (unchanged); the other keys
   ignore case. An unknown key (also `pedaloe` and `jetskivehicle`) leaves the type and creates no sprite
   ("Vehicle construct picks the type of the key ...", "Vehicle construct leaves the type alone ..." [lifecycle]).
2. Construction sets type specific fields and a zeroed state, takes the physics numbers from the definition, creates only the
   sprites of its type, and posts a towed vehicle between the route counters and the sprites ([lifecycle], the "Vehicle
   construct ..." cases).
3. Destruction restores the type behavior first and the base last, releases sprites in a fixed order, stops the loop sound for
   types 0, 1 and 5 only, and frees memory only when asked. A fishing boat frees the eight cells around its tile; a jet ski or
   speed boat frees its tile and the neighbours of its unload direction ([lifecycle], the "Vehicle destruct ..." cases).
4. Messages ([messages]): "created" answers 1 and unknown types go to the base; a stop turn turns every vehicle not on the
   circuit; arriving on the way out takes the next way point, the unload point or the destroy point, and a circuit vehicle
   asks for the next circuit point; arriving back turns small vehicles and unloads the others; a boat at its turn point frees
   its cells for the move order and blocks them again; a ride ends when its time is over; the fishing boat alternates between
   a new fishing point and a catch; arriving in another state does nothing.
5. Guests: serving counts guests for the ferry and the coach and seats a guest elsewhere; a guest entering a small vehicle lets
   the second board; the ferry and coach collect their guests and leave when all are in; a guest leaving a small vehicle is put
   at the owner's serve position; leaving the coach steps beside the doors, leaving the ferry stays in place; the quality
   question and leaving the unload state tell the owner its guests are served ([messages]).
6. Entering states ([messages], "Vehicle OnEnterState: ..."): a created ferry announces itself and idles; a circuit vehicle
   starts at the nearest circuit point and unloads at the one before; a fishing boat finds its turn area three tiles to the
   left; the turn state sets the velocity and turns to the unload direction; the jet ski driver falls off and remounts when the
   animation ends; the ferry unloads its last guest first, hoots, and complains when no rooms are free.
7. Per-state behavior ([states]): idle at a circuit dock blocks the dock and the cells its direction keeps, off the circuit
   touches no cells; return, unload and depart differ per type (fishing boat through its turn area, crowd vehicles straight
   to the unload point, pedalo from a free cell, coach opens its doors on unload, jet ski departs to the circuit start and
   unloads when no path is found); ride time, roaming, wait and remove are pinned.
8. Update ([update]): the base update runs first and a failure is a fatal error; a fishing boat frees its cells before and
   blocks the ones around its new tile after; a vehicle named BOAT (any case) faces direction 0; idle ferries and coaches
   send the departure when nobody is left to wait for; a boat with guests leaves more than a second after its ride time unless
   a guest is still boarding; the sprite follows the sixteenth of the velocity; every guest follows the boat.
9. Paths ([path_helpers]): next free circuit point and its speed, roaming (first long run wins, else the longest), fishing spots
   of five tiles, aiming sectors, lanes and turns for every direction pair.
10. Animation ([animation]): ride, idle, catch, boarding (per vehicle kind) and turn animations by name and order.
11. Presentation ([presentation]): ferry position and direction, wake and coach layers, closed fishing driver, clockwise turn
    restoring hull, rods and guests, first-guest boarding bias, variable keys and the fatal error for an invalid one.
12. Saving ([serial]): fixed slots and sprite order are visited, appended points keep coordinate bits, loading restores circuit
    links for jet skis and speed boats only; field keys match the save bridge ("vehicle.field_keys_match_the_save_bridge").

## Not covered by tests
- Numeric details of rules 4 to 11 are asserted only inside the named tests; this spec does not restate them. Read the test.
- How the circuit points are laid out on a map.

## Tests
`tests/unit/vehicle_helpers_test.cpp`, `vehicle_lifecycle_test.cpp`, `vehicle_messages_test.cpp`, `vehicle_states_test.cpp`,
`vehicle_update_test.cpp`, `vehicle_animation_test.cpp`, `vehicle_path_helpers_test.cpp`, `vehicle_presentation_test.cpp`,
`vehicle_serial_test.cpp`; game scripts `tests/vehicle_messages.txt`, `vehicle_paths.txt`, `vehicle_presentation.txt`.
