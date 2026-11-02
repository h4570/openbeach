# Spec: moving objects, arrival and per-frame movement

## Purpose
What a moving object (guest, staff, vehicle) does when it reaches the next point of its route or walk, and how its per-frame
update steps the position, clamps it to the map and turns the sprite. The physics of one step and the route search are other
specs' business.

## Vocabulary
- **Route**: a list of tile steps with a cursor (current step, optionally reversed). The **last point** is the last index, or
  index 0 when reversed.
- **Messages**: *last point* (MT_LAST_POINT), *entered sea* (MT_ENTERED_SEA), *move I'm here* (MT_MOVEIMHERE), *vehicle stop
  turn* and *vehicle smooth turn*. A delayed message carries the object's current state in its extra field.
- **Direction index**: one of 8 sectors, 0 = up the screen, counted clockwise.

## Rules: direction
1. The direction index of a vector is the angle from "up" (acos of minus y) mirrored for negative x, cut into sectors: up 0, right 2,
   down 4, left 6, (0.7, -0.7) 1, (-0.7, -0.7) 7. A y that is not a number counts as -1, giving sector 4 (also with x not a
   number); a y slightly past -1 is clamped; a sector boundary rounds up to the next direction
   (`moving_events.direction_sectors_follow_acos_of_minus_y_mirrored_for_negative_x`).

## Rules: reaching a target
2. With the cursor already past the route end, the route is released and nothing else happens; the object keeps moving
   (`moving_events.reached_past_the_route_end_releases_it_and_does_nothing_else`).
3. Reaching the second-to-last point advances the cursor, clears the position offset and the entered-sea flag, and posts a delayed
   (0.01 s) last-point message with the state, even when the tile is sea. Reversed routes work from index 0; a guest sends
   it through its own slot with a delayed send, and the first step of the reversed route posts nothing
   (`moving_events.reached_second_to_last_point_reports_last_point_with_the_state`).
4. Reaching a point on an open sea tile (sea without the shallow flag) reports *entered sea* once, with the position truncated
   (100.9 / 250.2 gives 100 / 250) and the state; a shallow tile is not sea; an object already in the sea does not report again
   (`moving_events.reached_open_sea_once_reports_entered_sea_with_the_truncated_position`).
5. A sea vehicle on a vehicle path with turn type 1 compares the direction to the next route point with its last direction; on a
   change it stores the new direction, stops moving and posts a delayed *vehicle stop turn*; with the same direction nothing
   happens, and with turn type 0 it does not turn (`moving_events.reached_sea_vehicle_stops_to_turn_when_the_direction_changes`).
6. Reaching the target without a route stops the object, zeroes the remaining time, velocity and steering force, clears the
   waiting and message-received marks, and posts *move I'm here* with the state
   (`moving_events.reached_without_route_stops_and_reports_move_im_here`).
7. A follower keeps moving while the followed object still moves (the remaining time is reset anyway); when it has stopped, the
   follower stops, forgets the follow target (id -1), returns to behaviour state 1 and reports as in rule 6
   (`moving_events.reached_follower_waits_for_the_followed_object_to_stop`).
8. A walk inside a building advances the walk point; after the last point (count reached) it ends: walking flag and moving cleared
   and a report posted. With the traversal flag set (any nonzero value) the walk goes backwards and ends when it passes 0 (the
   point wraps to the maximum value) (`moving_events.reached_building_walk_ends_after_the_last_point_or_below_zero`,
   `moving_events.traversal_byte_flags_have_the_same_nonzero_behavior`).

## Rules: per-frame update
9. A stopped object only has its debug-break mark cleared; the update reports success and changes nothing else
   (`moving_events.update_stopped_object_does_nothing_but_clear_the_debug_break`).
10. A moving object is stepped in fixed 0.02 s steps; the remainder (0.05 s gives two steps and about 0.01 s) is kept for the next
    frame, and each frame the object is teleported to its new position once (z 0). A time of exactly one step is not enough
    to step: the remainder is kept (`moving_events.update_steps_in_fiftieths_and_keeps_the_remainder`).
11. Positions are clamped to the map (x to 4096, y to 8192 at most, 0 at least); a position that is not a number becomes 0, and -0
    is kept (`moving_events.update_clamps_to_the_map_and_nan_goes_to_zero`).
12. Arrival ends the step loop: the object stops, the remainder is zeroed, one *move I'm here* is posted and the sprite is no
    longer turned (`moving_events.update_arrival_stops_the_loop`).
13. While moving with direction control, the sprite is turned along the normalized velocity ((3, -4) gives (0.6, -0.8)); a zero
    velocity gives a facing that is not a number (`moving_events.update_turns_the_sprite_along_the_velocity`).
14. A vehicle on a route with the smooth turn type posts *vehicle smooth turn* once when the route end is 0.4 s away
    (distance 20 at speed 50) and zeroes its turn animation timer; 4 s away (speed 5) posts nothing. The exact threshold between
    the two is not pinned
    (`moving_events.update_smooth_turn_fires_once_when_the_route_end_is_near`).

## Not covered by tests
- The position change of one step (steering, velocity limits): other tests in `mover_physics_test`.
- The meaning of the stored state number in the delayed messages.
- What the receivers do with the messages.

## Tests
`tests/unit/mover_events_test.cpp`.
