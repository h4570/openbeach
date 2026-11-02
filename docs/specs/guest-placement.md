# Spec: guest placement

## Purpose
The pure rules for where a new or moving guest is put: the step to a neighbouring spot, a random spot around a point, a
clear entry point and whether the hotel has a free room.

## Rules
1. A snog step by direction 0..7 is (0,-14), (11,-12), (15,0), (11,10), (0,16), (-10,10), (-15,0), (-10,-10). A direction
   of 8 or more is refused and leaves the step untouched. The coordinate is the position plus the step, computed in double and
   stored as float (`guest_placement.snog_steps_by_direction`).
2. A surface candidate draws x first, then y (two draws), each offset spread evenly over -radius..+radius. The x offset
   is rounded to single precision before it is added to the position; the y offset is not
   (`guest_placement.surface_candidate_draws_x_then_y_and_keeps_the_y_offset_unrounded`).
3. A candidate is clamped to the map: x and y at least 10, at most 4000 and 8000. A NaN position counts as below the minimum
   (`guest_placement.surface_candidate_is_clamped_to_the_map`).
4. An entry point is clear when neither its tile nor the neighbours in directions 0, 2, 4 and 6 carry the building bit
   0x10; the other four neighbours do not matter, other flag bits do not matter, and the tile wraps like the grid
   (`guest_placement.entry_point_needs_no_building_on_the_tile_and_four_neighbours`).
5. A hotel has a room unless the number of guests equals its capacity exactly; a count above the capacity still has room
   (`guest_placement.a_room_is_free_unless_the_hotel_is_exactly_full`).

## Not covered by tests
- Where these rules are called and how often a candidate is retried.
- Whether a candidate lies on water or sand.

## Tests
`tests/unit/guest_placement_test.cpp`.
