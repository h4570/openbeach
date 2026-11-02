# Spec: moving-object routes, targets and physics

## Purpose
How a moving object (guest, staff, vehicle) builds a route between two tiles, aims at its next target, steps toward it and
integrates its velocity. Arrival and per-frame events are in [moving-object-events.md](moving-object-events.md).

## Vocabulary
- **Route**: a list of tile steps with a path type (0 pavement, 1 land, 2 sea, 3 swim) and a valid flag.
- **Steering force / velocity / target**: 2D vectors of the object; the target is where it aims.
- **Behaviour state**: 1 seek, 3 wander, 4 follow; anything else only integrates.

## Rules: building a route
1. When both end tiles match the mask (pavement: 0x10000000 and no scenery 0x8000; land: 0x80000000, scenery ignored), the
   route is built directly after reading the two flags: pavement routes use the pavement builder, land routes the land
   builder with option 0. The new cursor is stored; a failed build clears `found` and stores none
   (`moving_route.matching_ends_get_a_pavement_route_without_cleanup`, `moving_route.matching_ends_get_a_land_route_with_option_zero`,
   `moving_route.a_failed_route_clears_found_and_stores_null`, `moving_route.a_land_end_with_scenery_still_routes_directly`).
2. When either end does not match (pavement scenery on one end, one land end missing), the end helper runs first, the
   route is built from the ends the helper returns, and the ends are cleaned up afterwards
   (`moving_route.pavement_scenery_on_either_end_goes_through_the_helper`,
   `moving_route.unmatched_ends_use_the_corrected_coordinates_and_clean_up`, `moving_route.the_helper_runs_when_only_one_end_matches`).
3. A failed helper builds nothing, still cleans up, leaves `found` and the old cursor alone
   (`moving_route.a_failed_helper_still_cleans_up_and_builds_no_route`). An unrecognised mask also cleans up after a
   successful helper (`moving_route.an_unrecognized_mask_cleans_up_after_a_successful_helper`).
4. A direct match with another mask builds nothing and keeps the cursor
   (`moving_route.direct_match_with_another_mask_builds_nothing_and_leaves_the_cursor`). Start equal to stop does nothing after
   reading the flags, for every mask (`moving_route.the_same_tile_does_nothing_after_reading_the_flags`).

## Rules: aiming and stepping
5. Without a route the target is the destination. In a building, the target is the building's current walk point (the
   height stays); if the building was demolished the destination is kept. Building wins over following. A following
   object off route copies the followed object's position (`moving_target.update_without_a_route_keeps_the_destination`,
   `moving_target.update_in_a_building_takes_the_walk_point`, `moving_target.update_in_a_demolished_building_keeps_the_destination`,
   `moving_target.update_follow_off_route_copies_the_followed_position`).
6. On a route the target is the centre of the step tile (half a tile right of the tile corner, one tile per row, half a
   tile lower on odd columns) plus the lane offset: lane 1 +15, any other non-zero lane -15, lane 0 none
   (`moving_target.update_on_a_route_aims_at_the_step_tile_with_the_lane_offset`, `moving_target.path_point_helper_matches_the_formula`).
7. If the step tile is wading and not blocked, or a scenery tile that the route type does not pass through, and the
   object's own tile is blocked (or the route is flagged invalid), the route is released and the command of the route type is
   posted again with the truncated destination: 0 pavement-to-point, 1 move-to-point, 2 move-to-sea-point, 3 swim-to-sea-point,
   other types post type 0 (`moving_target.update_resends_the_command_of_the_route_type_when_the_route_and_the_own_tile_are_bad`,
   `moving_target.update_drops_a_route_flagged_invalid_whatever_the_tiles_say`). Both a bad route and a bad own tile are
   required; a type-4 route through scenery stays (`moving_target.update_needs_both_a_bad_route_and_a_bad_own_tile`,
   `moving_target.update_leaves_a_through_scenery_route_alone`).
8. A vehicle path is not re-sent; on a wading step tile with a blocked own tile it releases the route and posts a delayed
   move-to-sea-point after 3 seconds; on other tiles it does nothing (`moving_target.update_vehicle_paths_are_not_resent_but_turn_back_at_shallow_water`).
9. A destination from a message takes both words unsigned; the y word is truncated from the exact integer, the x
   from the stored float; the terrain gives the height; the old route is released and, if inside a building, that building
   is remembered as the one being left (`moving_target.set_destination_takes_unsigned_words_and_asks_the_terrain_for_the_height`,
   `moving_target.set_destination_releases_the_route_and_remembers_the_building`,
   `moving_target.set_destination_widens_unsigned_and_truncates_x_from_the_stored_float`).
10. Seek moves toward the target and reports arrival within the tolerance (NaN tolerance counts as arrived); an
    unrecognised behaviour only integrates (`moving_target.step_seek_moves_toward_the_target_and_reports_arrival`,
    `moving_target.step_unordered_compare_counts_as_arrived`, `moving_target.step_unrecognized_behaviour_only_integrates`).
11. Wander draws twice (`draw % 200 - 100` on x, `draw - 100` on y, scaled by 1/100) and scales a force above the maximum
    back to it (`moving_target.step_wander_draws_twice_and_keeps_the_force_short`, `moving_target.step_wander_scales_a_force_above_the_limit`).
12. Follow off a route: inside the follow distance, one reached-follow-distance message is posted (once while the flag
    is set) and arrival is reported only when the followed object has stopped; outside the distance nothing happens. On a
    route, follow seeks and arrives within 1.5 (`moving_target.step_follow_off_route_posts_once_and_waits_for_the_followed_object_to_stop`,
    `moving_target.step_follow_off_route_outside_the_distance_is_not_arrival`, `moving_target.step_follow_on_a_route_seeks_and_arrives_within_one_and_a_half`).

## Rules: physics
13. Seek points at the target at maximum speed minus the current velocity; on the target, or closer than 0.001, the force is 0.
    The force is clamped to the maximum force, keeping its direction. Reciprocals are estimates, so values are close, not exact
    (`moving_physics.seek_points_at_the_target_with_max_speed`, `moving_physics.seek_on_the_target_or_closer_than_a_thousandth_gives_zero_force`,
    `moving_physics.force_is_clamped_to_max_force`).
14. Arrive slows down inside the follow distance (length / distance * maximum speed); a zero distance is replaced by 0.2 * maximum
    speed and the speed is capped; a negative distance gives a negative speed. Follow keeps a zero distance
    (infinite speed, capped) (`moving_physics.arrive_slows_down_inside_the_follow_distance`, `moving_physics.follow_does_not_replace_a_zero_distance`).
15. Integration adds force / mass to the velocity, caps the speed, and moves the position by dt times velocity; a zero speed or zero dt
    does not move (`moving_physics.integrate_adds_acceleration_and_moves_by_dt_times_velocity`,
    `moving_physics.integrate_caps_the_speed_and_skips_the_move_for_zero_speed`, `moving_physics.zero_dt_leaves_the_position`).

## Not covered by tests
- Route computation itself (see the path module) and how lanes are assigned.
- Arrival messages beyond those in the events spec.

## Tests
`tests/unit/mover_route_test.cpp`, `tests/unit/mover_target_test.cpp`, `tests/unit/mover_physics_test.cpp`.
