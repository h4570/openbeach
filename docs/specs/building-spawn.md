# Spec: building spawning (vehicles, tables, staff)

## Purpose
What a new building places around itself: its vehicles, its tables and its staff, and how hiring and dismissing staff work.
Each placement is a command to the world; the spec says where and with what.

## Vocabulary
- **Rotation**: the building's turn, 0..3. Offsets in the definition are given for rotation 0.

## Rules
1. A vehicle starts on the tile reached from the building's tile by stepping along one axis, then the other, with step
   counts depending on rotation and the footprint size (rotation 1: footprint width minus 2 steps one way, then the offset
   the other; rotation 3: offset one way, height minus 2 the other). Step counts never go below zero; a rotation outside
   0..3 stays on the building's tile (`building_spawn.vehicle_tile_by_rotation`, `building_spawn.vehicle_step_counts_never_negative`).
2. A vehicle's own turn is its direction plus twice (4 minus rotation), modulo 8; a negative sum keeps its sign
   (`building_spawn.vehicle_rotation_is_signed_remainder`).
3. Each vehicle gives one place command with the vehicle's key, the tile centre and the building as owner
   (`building_spawn.spawn_vehicles_posts_one_command_each`). No vehicles, no tables: no commands
   (`building_spawn.no_vehicles_no_commands`).
4. Each table gives one place command with the table key, a position from the footprint, the rotation and height 0. Keys are
   cut to 63 characters (`building_spawn.spawn_tables`, `building_spawn.long_key_is_terminated`).
5. Dismissing a staff member found in the first males-plus-females entries removes it, closes the gap, lowers the male or
   female count (by the member's sex) and tells the member to leave. An id beyond the counts, or an empty list, changes
   nothing and looks nothing up (`building_spawn.dismiss_moves_the_rest_down`, `building_spawn.dismiss_female_and_last`,
   `building_spawn.dismiss_unrecognized_changes_nothing`).
6. New staff stand at the serving point plus a fixed offset by index (ten offsets; indexes outside 0..9 add nothing)
   (`building_spawn.staff_offsets`).
7. Hiring refuses when the staff count equals the maximum (a count above it still hires), and for the serving type and
   unknown types. Lifeguard, mechanic, repairman, cleaner and security guard each place their own kind, at the serving point,
   with the male key when men outnumber women and the female key otherwise, and the staff count as rotation
   (`building_spawn.hire_refuses_when_full_or_unrecognized`, `building_spawn.hire_kinds_and_keys`).
8. A hired builder gets the lowest unused builder number, or the count when all are used; numbers outside the list are
   ignored; the number is the rotation and picks the offset (`building_spawn.hire_builder_takes_the_lowest_free_number`).
9. Initial staff are placed per definition entry: serving staff at the footprint point, lifeguards and repairmen with a key
   chosen by a random number's parity, builders with their list position as number, cleaners and security guards with
   their keys; the DJ type places nothing. At most ten entries are used (`building_spawn.initial_staff_by_type`,
   `building_spawn.initial_staff_count_is_capped`).

## Not covered by tests
- What the world does with the commands (whether placing can fail on blocked tiles).

## Tests
`tests/unit/building_spawn_test.cpp`; game scripts `tests/building_guests.txt`, `building_vehicles.txt`.
