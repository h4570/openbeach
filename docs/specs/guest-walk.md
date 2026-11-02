# Spec: guest walk paths, routes and targets

## Purpose
The pure rules that choose where a guest walks: which tiles it may step on, which neighbour tile it wanders to, where it
boards the surf or the dance floor, where it unloads and which bin it heads for. The state machine that calls them is not
part of this spec.

## Vocabulary
- **Tile flags**: the 32-bit flag word of a map tile. Bits used here: 0x2, 0x4 (water), 0x10 (building body), 0x20,
  0x80 (marked), 0x100 (beach edge), 0x800 (built), 0x8000 (pavement/blocked), 0x10000000 (land).
- **Direction**: one of 8 neighbour steps (0..7); the grid wraps at its edges.
- **Draw**: a random number from the game generator, 0..32767.
- **Goal**: a pair of whole numbers, the target position of a walk.

## Rules: walkable tiles and wandering (`guest_walk_path`)
1. A tile is walkable if it is marked (0x80) and has no pavement bit, or if it is land (0x10000000), without the pavement
   and building bits. Marked wins over the building bit; the pavement bit stops both (`guest_path.walkable_tile_is_marked_without_pavement_or_land_without_pavement_and_building`).
2. The action state is 1 only for the desperate state together with the exact field text `D_WALK_DESPERATE`; a longer,
   shorter or different text gives 0 (`guest_path.action_state_is_one_only_for_the_desperate_walk_of_the_desperate_state`).
3. With no walkable neighbour no random number is drawn and nothing is visited (`guest_path.no_walkable_neighbour_draws_nothing`).
4. Otherwise exactly one draw is taken; the draw modulo the number of walkable neighbours picks the neighbour in direction
   order (`guest_path.the_draw_picks_the_kth_walkable_neighbour`). The draw is read as unsigned
   (`guest_path.the_draw_is_taken_as_unsigned`).
5. From the chosen neighbour the walk goes on in the same direction while tiles stay walkable and visits each one; the
   walk's goal is the second-to-last tile of the row, or the only tile for a row of length 1
   (`guest_path.the_middle_of_the_second_to_last_tile_of_a_row_is_the_goal`).

## Rules: boarding and routes (`guest_walk_routes`)
6. A boarding tile has 0x10000000 or 0x80, or has 0x2 without the building and blocked bits; nothing else qualifies
   (`guest_walk_routes.getonpath_tile_flags`).
7. The boarding search casts rays from the start tile and takes the first ray of the first round that meets a boarding
   tile; with none it finds nothing, and a ray keeps going across the grid wrap (`guest_walk_routes.getonpath_takes_the_first_ray_of_the_first_round`,
   `guest_walk_routes.getonpath_ends_in_nothing_when_no_ray_meets_a_tile`).
8. The surf search steps eight rays at once; a ray ends at the map edge (column 0 or 127, row 0) or on a tile with
   0x8030. A ray that meets a 0x100 tile reports the tile before it. After the first find the round goes on with ray 5
   (rays 1 to 4 are skipped). With no beach edge, or a start on the edge, nothing is sent
   (`guest_walk_routes.surf_to_beach_matches_the_expected_tiles_on_random_grids`,
   `guest_walk_routes.surf_to_beach_goes_on_with_ray_five_after_a_find`,
   `guest_walk_routes.surf_to_beach_without_an_edge_sends_nothing`).
9. The wind surf counts water tiles ahead (0x4, no building body) up to 5, stopping at the first tile that is dry, not
   water or built; the number of steps is the draw modulo that count plus one (`guest_walk_routes.windsurf_counts_free_water_ahead_up_to_five`,
   `guest_walk_routes.windsurf_steps_are_a_draw_modulo_the_free_tiles_plus_one`). The tile `n` steps ahead follows the
   direction step by step (`guest_walk_routes.tile_ahead_walks_the_direction`).
10. The dance offset maps a draw to an integer from -8 to 7, never decreasing with the draw
    (`guest_walk_routes.dance_offset_runs_from_minus_eight_to_seven`).
11. A dance floor has an anchor tile, an outer count (rows, direction 6) and an inner count (tiles per row, direction 0);
    for an odd direction (also negative) the two counts swap
    (`guest_walk_routes.dance_floor_swaps_its_sides_on_odd_directions`). A tile is on the floor if it lies in one of the
    rows; a floor with a zero or negative count, or a candidate off the map, never matches
    (`guest_walk_routes.on_dance_floor_covers_outer_rows_of_inner_tiles`).
12. The dance search draws two numbers per candidate (x then y offset) and discards the first candidate whatever it is;
    it takes the first later candidate that is on the floor and not the guest's own tile. Candidates need the built
    flag 0x800 and must lie on the map (`guest_walk_routes.dance_search_discards_the_first_candidate`,
    `guest_walk_routes.dance_search_needs_the_built_flag_and_a_candidate_on_the_map`).
13. The dance goal is the position plus 20 times the scaled draw minus 10 on each axis, truncated; the x sum is rounded to
    single precision first, the y sum is not (`guest_walk_routes.dance_goal_matches_the_expected_goal`,
    `guest_walk_routes.dance_goal_y_is_not_rounded_to_float`).

## Rules: goals, unloading and bins (`guest_walk_targets`)
14. A truncated goal cuts both coordinates toward zero. A value past 32 bits keeps the low 32 bits of the 64-bit
    truncation; NaN, infinity and values past 64 bits give 0 (`guest_walk_targets.truncated_goal_truncates_toward_zero`,
    `guest_walk_targets.truncated_goal_wraps_beyond_int_and_zeroes_nan`).
15. The unload goal offsets the position by rotation: rotation 7 gives x - 63, y + 14; rotation 5 gives x + 22, y + 72; every
    other rotation just truncates (`guest_walk_targets.unload_goal_offsets_by_direction`). The y sum is rounded to single
    precision, the x sum is not (`guest_walk_targets.unload_y_is_rounded_to_float_but_x_is_not`,
    `guest_walk_targets.unload_goal_matches_the_expected_goal`).
16. The wind-surf offsets are the single-precision words 0xc2be0000 (x) and 0xc27c0000 (y)
    (`guest_walk_targets.wind_surf_offset_constants_match_the_expected_bits`).
17. The distance to a bin takes dx unrounded and dy rounded to single precision before squaring
    (`guest_walk_targets.bin_distance_matches_the_expected_value`).
18. The nearest bin is the first of equal distances; only a distance strictly below 160 counts; the best distance is kept as
    a float; a NaN distance wins and every later bin then replaces it (`guest_walk_targets.nearest_bin_takes_the_first_of_a_tie_and_nothing_beyond_the_range`,
    `guest_walk_targets.nearest_bin_nan_wins_and_keeps_winning`).
19. A bin has room if its count, read unsigned, is below its capacity, read unsigned
    (`guest_walk_targets.bin_room_is_an_unsigned_compare`).
20. A guest uses a bin when the draw scaled to just under 1 is below the chance; a NaN chance counts as below, a chance of 0 or less
    never (`guest_walk_targets.uses_bin_compares_the_scaled_draw_with_the_chance`).

## Not covered by tests
- Which state calls each rule and what the guest does after reaching a goal.
- The rays' step order beyond the cases above, and how the grid's flag bits get set.
- The mapping between rotation numbers and the building models.

## Tests
`tests/unit/guest_walk_path_test.cpp`, `tests/unit/guest_walk_routes_test.cpp`, `tests/unit/guest_walk_targets_test.cpp`.
