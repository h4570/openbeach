# Spec: guest pool, slide and swimming rules

## Purpose
The pure rules a guest follows at the fun pool: which way it faces, where it is sent when it enters or leaves, which
pool tile it swims to next, which sprite it wears on the slide and in the water, and how long the slide waits. The state
machine that calls them is not part of this spec.

## Vocabulary
- **Direction**: the building's orientation, 0..3. Anything else is an *unknown direction*.
- **Facing**: one of 8 compass steps (0..7) of a sprite animation.
- **Pool tile**: a map tile carrying the pool flag.

## Rules
1. The facing at the pool is the signed remainder (by 8) of a base value minus twice the direction. For the pool base the
   directions 0, 1, 2, 3 give 1, 7, 5, 3; direction 5 and 9 give -1, direction 6 gives -3 and direction -2 gives 5. The swim
   base gives 7, 5, 3, 1 for directions 0..3 (`guest_pool.facing_is_the_signed_remainder_of_the_base_minus_twice_the_direction`,
   `guest_pool.swim_swaps_the_sprite_in_the_states_one_to_four_only`).
2. The slide facing for a start mode and a direction is `(mode + (4 - direction) * 2) mod 8`, also for directions past 3
   (`guest_pool.facing_is_the_signed_remainder_of_the_base_minus_twice_the_direction`). The swim start modes feed this rule
   (`guest_pool.swim_swaps_the_sprite_in_the_states_one_to_four_only`).
3. A facing is clamped against the number of directions of an animation: a facing below the count stays (also when
   negative), otherwise it becomes the last direction (count - 1); an animation without directions gives -1
   (`guest_pool.clamp_facing_compares_as_signed_and_uses_the_last_direction`).
4. Only directions 0..3 are known; 4 and -1 are not (`guest_pool.known_direction_is_unsigned_below_four`).
5. A posted goal is the building position plus an entry or exit offset for the direction, truncated to whole numbers
   (building 100.7 / 200.9, direction 0, entry offsets: 67 / -239; direction 3, exit offsets: 247 / -131). An unknown direction
   keeps the building's own position truncated (100 / 200). The y sum is rounded to single precision before it is truncated,
   the x sum is not (`guest_pool.posted_goal_truncates_x_unrounded_and_y_as_float`).
6. A teleport point is the building position plus an offset, each sum rounded to single precision
   (`guest_pool.teleport_point_stores_both_sums_as_float`).
7. The next pool tile: a tile with no pool neighbour among its 8 neighbours yields no goal and draws no random number. Otherwise
   exactly one random number is drawn; the number modulo the count of pool neighbours picks the neighbour in direction
   order. From there the walk goes on in the chosen direction while the next tile is a pool tile too, and the goal is the
   centre of the last pool tile. Three tiles of pool straight below the start give the centre of the last of them
   (`guest_pool.next_tile_without_pool_neighbours_draws_nothing`,
   `guest_pool.next_tile_picks_the_kth_flagged_neighbour_and_walks_on_while_pool`,
   `guest_pool.next_tile_goes_to_the_last_tile_of_a_pool_row`).
8. The slide sprite follows the guest's name, ignoring case: Gail 0, Shaz 1, Fat_Bloke 2; every other name (Fat_Bloke2, Chip,
   empty) 3. Index 1 names the sprite `Shaz_funpool` (`guest_pool.slide_sprite_follows_the_name_without_case`).
9. The swim sprite follows the name of the slide sprite, ignoring case: Gail_funpool 0, Shaz_funpool 1, Bob_funpool 2,
   everything else 3 (Chip_funpool, Gail, empty). Index 2 is `Fat_bloke`, index 3 is `Fit_Bloke`
   (`guest_pool.swim_sprite_follows_the_funpool_name_without_case`).
10. The swim swaps the sprite only in the states 1 to 4 (`guest_pool.swim_swaps_the_sprite_in_the_states_one_to_four_only`).
11. The slide palette is the guest's palette clamped to the palette count (8 gives 7; a negative value stays; no palettes
    gives -1). The slide delay is 2 for indexes 0 and 1 and 4 for 2 and 3 (`guest_pool.slide_palette_and_delay`).

## Edge cases
- The swim has four lanes with start offsets, one landing offset and one footprint offset per direction; the test pins
  every one of these floating-point constants bit for bit (`guest_pool.swim_offsets_match_the_expected_constants`).

## Not covered by tests
- The state machine that decides when each rule is used, and what the guest does on arrival.
- How the pool flag gets onto tiles.
- The meaning of the lane numbers (which guest uses which lane).

## Tests
`tests/unit/guest_pool_test.cpp`.
