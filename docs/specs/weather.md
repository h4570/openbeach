# Spec: weather

## Purpose
Runs the weather schedule of a scenario (clear, rain, storms), tints the daylight by hour and weather, plays the rain loop,
draws rain and lightning, and strikes with lightning during storms.

## Vocabulary
- **Weather kind** (state): 0 heat wave, 1 normal, 2 a dry kind that does not rain, 3 light rain, 4 heavy rain, 5 storm.
  Kinds 3-5 are the raining kinds. A kind outside 0..5 counts as dry for the rain loop.
- **Schedule entry**: kind, day, hour and a table of desire modifiers (index 9 = soft drinks in the tests). **Next entry**:
  the index of the next entry to start; the **day offset** moves a finished schedule forward.
- **Current / previous state**: the kind now and the kind being blended out. **Weather time**: the hour the kind changed.
- **Light keys**: 24 colours, key i at hour i. **Tone tables**: the per-colour-step tables built from the final colour.
- **Strike**: a lightning strike; `strikes` = 1 when one is armed, `strikeLevel` = the thunder level 0..10, plus a strike
  area (left, top, right, bottom) and an optional target building.
- **Manual colour**: an override colour of three channels, used only while manual lighting is on.

## Rules
### Schedule
1. An entry is due when its day (read as an unsigned number) is earlier than the world day, or equal and its hour is below the
   world hour; an equal hour is not yet due, a later day never is, and a day of -2 is a far future day. A time that is not
   a number counts as due (`weather.entry_due_compares_the_day_unsigned_and_the_hour_like_fcomp`).
2. Activating an entry makes the old state the previous state, the entry's kind the current state, the entry's hour the
   weather time, copies its modifier table and advances the next entry
   (`weather.activation_moves_the_state_and_copies_the_modifiers`). The current modifiers handed to guests are that copy
   (`weather_control.current_modifiers_point_at_the_struct`).
3. On activation every guest gets the modifiers applied; from light rain up (kinds 3-5) each guest also gets a state-change
   message of type 72 from the world (sender -2); kind 2 sends none
   (`weather.every_guest_gets_the_modifiers_and_bad_weather_only_from_light_rain_up`).
4. Entries start in order and the first that is not due stops the loop; the guest list is requested once per activated
   entry and the lighting is updated once per call, after the loop, also with no entries at all
   (`weather.entries_come_due_in_order_and_the_first_that_is_not_stops_the_loop`, `weather.lighting_runs_with_no_entries_at_all`).
5. When the schedule has run out and the day offset is not zero, it starts over: next entry 0, the current day offset grows by
   the day offset, and every entry's day moves forward by it. With a day offset of 0 an exhausted schedule stays exhausted
   (`weather.an_exhausted_schedule_starts_over_shifted_by_the_day_offset`,
   `weather.without_a_day_offset_an_exhausted_schedule_stays_exhausted`).
6. Inserting an entry places it before the first element that does not sort before it (earlier day, or same day and earlier
   hour; equal times go in front of the old entry), after everything when none such exists; days are compared as unsigned
   numbers; a NaN hour on an existing element counts as earlier, and a new entry with a NaN hour goes after the entries of
   its day it cannot compare with
   (`weather.insert_goes_before_the_first_element_that_does_not_sort_before_it`,
   `weather.insert_orders_days_as_unsigned_numbers_and_nan_hours_as_earlier`). Inserting stores a copy, increments the
   entry count and leaves the next-entry index unchanged (`weather.insert_entry_allocates_first_inserts_and_counts`,
   `weather.insert_into_an_empty_schedule`).

### Rain sound
7. The rain loop follows the kind: going dry to light rain starts the light loop (no fade when no channel is held); light
   rain again only sets the volume; heavy rain fades the light loop and starts the heavy one; a storm keeps the heavy loop;
   going dry fades the loop out and sets the rain kind to 0 while the channel number is kept; staying dry does nothing; a
   kind outside 0..5 counts as dry (`weather.rain_loop_follows_the_weather_kind`).
8. A loop that never got a channel (-1) is not faded (`weather.a_loop_that_is_not_running_is_not_faded`).

### Daylight and tint
9. The pair of light keys follows the hour: key i is the upper key for hours in (i-1, i]; after 23:00 the upper key is key 0
   (midnight) with key 23 below it, and just after midnight key 0 becomes the lower key
   (`weather_lighting.key_pair_follows_the_hour`). The day colour is interpolated per channel
   between the pair and truncated, across midnight too (`weather_lighting.daylight_is_interpolated_per_channel_and_truncated`).
10. The weather tint blends the previous and current state colours for a quarter hour after the change (half way after
    0.125 h); the tint is subtracted per byte, each wrapping on its own, keeping the top byte
    (`weather_lighting.tint_blends_for_a_quarter_hour_and_wraps_when_subtracted`). A change time that lies ahead of the hour
    is taken a day back (`weather_lighting.the_change_time_is_taken_a_day_back_when_it_lies_ahead`). After the quarter
    hour the previous state becomes the current one (`weather_lighting.previous_weather_follows_the_current_one_after_the_blend`).
11. The lighting update fills the tone tables: table 0 scales the colour (a plain 64-level colour gives 7 at step 31), table
    8 is the identity, table 17 saturates every step above 0 and table 16 brightens twofold with clamping, in 5-5-5 and 5-6-5
    pixel formats (`weather_lighting.table_zero_scales_the_colour_and_saturates`).
12. A heat wave brightens by 1.3; a manual colour replaces the day colour and is multiplied by 3, and the last colour keeps its
    top byte (`weather_lighting.heat_wave_and_manual_colour_brighten`).
13. A lightning flash has phases by time since the flash: red and green double and blue quadruples (clamped) at 0.1 s; the plain
    colour between flashes at 0.2 s; brightened again at 0.4 s; the bolt is drawn at 0.5 s while the flash goes on; at 0.7 s
    the flash ends, the lightmap is released when there is one and the flash flag is cleared
    (`weather_lighting.flash_phases`).

### Thunder control and strikes
14. Setting the thunder level from off arms the first strike at three seconds plus the level schedule (`10 - level` steps of
    0.7 after the current time, e.g. level 7 at time 100 gives 3 x 0.7 + 100 + 3); level 0 from off still arms a strike
    ten steps ahead; while on, setting a level changes the level and switches the pending strike off; level 0 stops
    (`weather_control.thunder_level_from_off_schedules_the_first_strike`,
    `weather_control.thunder_level_zero_from_off_still_arms_a_strike_ten_steps_ahead`,
    `weather_strike.next_strike_matches_the_thunder_level_schedule`).
15. The lightning area stores left, top, right, bottom in that order (`weather_control.lightning_area_stores_the_four_values_in_order`).
16. Naming a target building walks the buildings backwards, collects the matches (case-sensitive; the first 100 in walking
    order), draws one random number reduced as unsigned to pick one, and arms a strike at it; with no match nothing changes
    and nothing is drawn
    (`weather_control.target_search_walks_the_buildings_backwards_and_draws_once`,
    `weather_control.target_search_reduces_the_draw_unsigned`,
    `weather_control.target_search_without_a_match_changes_nothing_and_draws_nothing`,
    `weather_control.target_search_keeps_the_first_hundred_matches`).
17. A strike is due only for a settled armed storm: strikes armed, state 5 with the previous state 5, manual lighting off,
    and the time strictly after the next strike time; while the previous state is still blending in, it is not due until
    the quarter hour is over (`weather_lighting.a_strike_is_due_only_for_a_settled_armed_storm`).
18. A due strike runs before the tint in the same lighting update and starts the flash that frame
    (`weather_strike.update_lighting_runs_a_due_strike_before_the_tint`).
19. A strike without a target picks a random point in the area (x from `rand % width + left`, y from `rand % height + top`,
    z kept), creates a bolt, starts the flash at the current time (flash colour 0), schedules the next strike by the level (level 0
    schedules none and disarms), and plays the thunderclap sound at volume 0.8 or restarts it when a channel already has it
    (`weather_strike.random_point_flash_and_next_strike`, `weather_strike.level_zero_schedules_no_next_strike`). A reversed
    area divides by a negative span (`weather_strike.reversed_area_divides_by_a_negative_span`).
20. A strike with a target building uses its position and no random draw, then clears the target; when the building is gone the
    area is used (`weather_strike.target_building_or_the_area_when_it_is_gone`).
21. A strike knocks out guests nearby that are heading for a desire: they get an action and the state-change message
    (knocked out), then a stop-moving message; guests in another state and non-guests are untouched
    (`weather_strike.knocks_out_guests_heading_for_a_desire`).
22. A strike damages the first building whose footprint holds the struck tile (the building's own tile is not part of the
    walk) (`weather_strike.damages_the_first_building_whose_footprint_holds_the_tile`) and vandalises repairable scenery within
    three tiles on each axis that is not in use and is not a building (`weather_strike.vandalises_repairable_scenery_nearby`).
    The tile distance is the absolute difference; it wraps for the extreme integer values, so such a distance counts as near
    (`weather_strike.tile_distance_is_the_absolute_difference`).
23. A bolt bitmap is `rows x 200` bytes; it consists of two segments of straight bolt with a random drift kept within -15..15
    that stays inside the bitmap, each row's run as wide as `30 x row / height` truncated, and two random draws per segment
    plus one per row; a bolt of no rows draws nothing; a new bolt replaces the old bitmap pointer without freeing it
    (`weather_strike.bolt_with_rand_zero_is_two_straight_segments`, `weather_strike.bolt_of_no_rows_draws_nothing`,
    `weather_strike.bolt_keeps_inside_the_bitmap_and_the_drift_in_range`,
    `weather_strike.bolt_replaces_the_old_bitmap_without_freeing_it`).

### Manual colour
24. Setting one override channel (value 0..1 times 255, truncated) changes only that channel and clears the top byte; values out
    of range wrap through the low byte and a value that is not a number gives 0
    (`weather_control.set_override_changes_one_channel`, `weather_control.set_override_rebuilds_the_colour_without_the_top_byte`,
    `weather_control.set_override_wraps_out_of_range_values_through_the_low_byte`, `float_to_int.truncates_to_64_bits_and_keeps_the_low_dword`).
    Reading a channel gives the byte times a float step slightly above 1/255, so a full channel reads a hair above 1
    (`weather_control.get_override_is_the_exact_product_of_the_byte_and_the_float_step`).
25. The weather sliders in the GUI only preview the manual colour: the colour changes but manual lighting stays off, and no
    command is queued (`tests/gui_weather_slider.txt`).

### Drawing
26. Rain drop count at the base size is 2000 for light rain and 4000 otherwise, doubled above 600 rows; the scaled count keeps
    the 800x600 density, never goes below the base count (also for a 0x0 view) and is capped at 64000
    (`weather_draw.drop_count_doubles_above_600_rows`, `weather_draw.scaled_drop_count_keeps_the_800x600_density`,
    `weather_draw.scaled_drop_count_never_drops_below_the_base_count`).
27. Drops are drawn only in kinds 3-5 and views with enough width and height; a light-rain drop moves 1 across and 8 down and
    marks two rows, a heavy drop 2 by 14 and three rows, wrapping at the view; while paused drops stand still but are drawn
    (`weather_draw.light_rain_moves_one_by_eight_and_dims_two_rows`, `weather_draw.heavy_rain_moves_two_by_fourteen_and_dims_three_rows`,
    `weather_draw.paused_drops_stand_still_but_are_drawn`, `weather_draw.other_weather_and_degenerate_views_draw_nothing`).
    A drop pixel is the colour halved plus half of white (a half blend toward white in the 5-5-5 or 5-6-5 format); the
    upper word of the pixel (its depth) stays (`weather_draw.dim_word_halves_the_colour_and_keeps_the_depth`).
28. Drops beyond the first 8000 reuse the buffer, shifted by a fixed stride per lap, and do not move themselves
    (`weather_draw.extra_drops_of_a_scaled_count_ride_on_the_first_8000`).
29. Randomizing the drops draws y first, then x, for each of the 8000 drops, reduced unsigned by the view height and width; a
    view of 1x1 gives all zeros; an empty view draws nothing (`weather.rain_drops_draw_y_first_then_x_and_reduce_unsigned`,
    `weather.rain_drops_in_a_one_pixel_view_are_all_zero`, `weather.rain_drops_leave_an_empty_view_alone`).
30. Drawing the lightning does nothing without a bitmap; otherwise it converts the strike point to the screen, clips, blits the
    bitmap when it is visible and always marks the lights dirty (`weather_draw.lightning_without_a_bitmap_does_nothing`,
    `weather_draw.lightning_clips_blits_and_marks_the_lights_dirty`, `weather_draw.lightning_off_screen_still_marks_the_lights_dirty`).

### Lifecycle
31. Constructing sets the world's weather pointer, randomizes the rain drops once, runs one lighting update, sets the rain
    channel and strike building to -1, leaves the schedule empty, fills the 24 light
    keys (key i at hour i) and the 14 state colours and zeroes the modifiers (`weather_lifecycle.construct_native_state`).
32. Destroying empties the schedule, deletes the lightning bitmap once and clears the world's pointer
    (`weather_lifecycle.destroy_owned_schedule_and_bitmap`).

## Edge cases
- A current state outside the colour table, or a strike with an empty area (no width or no height) when no target building
  is struck, is reported as unsupported by the lighting check (`weather_lighting.a_strike_is_due_only_for_a_settled_armed_storm`,
  `weather_strike.an_empty_area_is_unsupported_lighting`). A strike that is not due is never unsupported (same test).
- Time that is not a number counts as due for entries (rule 1).

## Not covered by tests
- The meaning of kind 2 and of the other state colours beyond 0, 1, 3, 4, 5.
- How the saved weather is written and read (`weather_save.h`; the game script `tests/weather_lifecycle.txt` only checks that
  save and load run and that construct and destruct counters move).
- The rain-loop sound names and the volumes in the game, beyond the loop names the test sees.
- The weather state a constructed world starts with and its first light keys (the construct test only counts the lighting
  update).
- `tests/weather_draw.txt` and `tests/weather_runtime.txt` only check that drawing, schedule, lighting and control calls are
  made in a running game; they pin no rule.

## Tests
`tests/unit/weather_update_test.cpp`, `weather_control_test.cpp`, `weather_lighting_test.cpp`, `weather_strike_test.cpp`,
`weather_draw_test.cpp`, `weather_lifecycle_test.cpp`; game scripts `tests/weather_draw.txt`, `tests/weather_lifecycle.txt`,
`tests/weather_runtime.txt`, `tests/gui_weather_slider.txt`.
