# Spec: staff shifts, builder numbers and walking to a bin

## Purpose
The small duties every staff member shares: a daily shift, the builder's number, facing a direction on screen, and the
point a cleaner walks to when it is sent to a bin.

## Vocabulary
- **Shift**: a start hour and an end hour (whole hours). **On shift**: the member is working at the current hour.

## Rules
1. Setting a shift stores both hours as given (`staff_duties.shift_is_stored_and_tested_as_unsigned_hours`).
2. A member is on shift from the start hour up to, but not including, the end hour: 8..18 is on at 8.0 and 17.99, off at
   18.0 and 7.5 (`staff_duties.shift_is_stored_and_tested_as_unsigned_hours`).
3. A shift whose end is earlier than its start runs past midnight: 22..6 is on at 23.0 and 5.9, off at 6.0 and 12.0
   (same test).
4. A shift with equal start and end is never on (5..5 is off at 5.0) (same test).
5. A negative start hour is treated as a very large number: shift -1..3 is on only before the end (on at 2.0, off at 3.0)
   (same test).
6. A builder stores its builder number as given (`staff_duties.builder_number_is_stored`).
7. Sending a staff member to a bin moves a walk point next to the bin, on the side of the bin's facing, and posts one
   message of type 8 from and to the member itself that carries the point. The offsets from the bin at (100, 200) for
   the directions 0..8 are: 0 -> (231, 100), 1 -> (222, 78), 2 -> (200, 69), 3 -> (178, 122), 4 -> (169, 100),
   5 -> (178, 122), 6 -> (200, 131), 7 -> (222, 122), 8 -> (200, 100); the message's flag holds the second value and its
   data the first; the point is written first value, second value (the point holds the values in that order, not x then y
   for every direction). A direction outside 0..8 (-1) gives the same point as direction 8
   (`staff_duties.bin_walk_moves_the_point_by_the_bins_direction_and_posts_the_target`).
8. When the bin does not exist, the point is untouched and nothing is posted (same test).
9. Facing a direction sets it as given when the sprite's animation has that direction; a direction past the animation's
   last one is clamped to the last one: with 8 directions, 3 and 7 stay, 8 and 200 become 7
   (`staff_duties.direction_is_clamped_to_the_animations_last_one`).

## Not covered by tests
- What the member does when its shift starts or ends, and who sets the shifts.
- Why the bin walk point is ordered this way; it is recorded here only as the numbers the test asserts.

## Tests
`tests/unit/staff_duties_test.cpp`.
