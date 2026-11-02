# Spec: building drawing and selection

## Purpose
Choose building previews, effects, lighting and selection actions.

## Rules
1. Selection opens the definition's menu for a finished building at selection level 2. Construction and demolition
   open their respective dialogs at level 1; construction takes priority. With neither selection action requested,
   nothing changes (`building_draw.on_select_dialogs_and_levels`).
2. Camera selection centers the building using viewport size and zoom, then stops the camera
   (`building_draw.on_select_centers_the_camera`).
3. Starting demolition closes the building, clears the queue, hides staff, and requests demolition state 2 in that order
   (`building_draw.begin_demolish_closes_clears_hides_and_sends_state_2`).
4. Beach modes 0, 1 and 2 respectively mean both areas open, first open and second closed, and both closed. Queries
   short-circuit after the first area's decisive answer. Setting a mode changes the areas then notifies staff and
   buildings; an unsupported mode still sends notifications without changing the grid
   (`building_draw.is_beach_open_mode_truth_table`, `building_draw.is_beach_open_mode_asks_in_order`,
   `building_draw.set_beach_open_mode_grid_and_messages`).
5. Power closure draws the shortage overlay; broken wear draws the repair overlay. Demolition suppresses the overlay
   (`building_draw.overlay_shortage_spanner_and_nothing`).
6. Odd facing directions swap footprint dimensions. A requested bake is consumed by drawing. A timed absence ends only
   after its deadline, restoring a bake request; equality with the deadline is too early
   (`building_draw.draw_swaps_the_footprint_for_odd_directions`,
   `building_draw.draw_burn_limbo_returns_the_building_when_the_time_is_up`).
7. Construction previews use construction progress; demolition previews use one minus demolition progress with a small
   reduction. Custom construction combines the main and ghost previews using their height ratio and clamps the ghost
   facing to its available directions (`building_draw.draw_progress_previews`,
   `building_draw.draw_custom_construction_scales_by_the_height_ratio`,
   `building_draw.draw_clamps_the_ghost_direction`).
8. With lighting enabled, crossing the switch-on time marks lights dirty and enables the tested night light-map draw.
   Zoom level 2 uses the zoom-out draw without masking; level 3 omits the light-map draw. A closed building can draw its
   shadow animation (`building_draw.draw_light_map_and_dirty_flag`,
   `building_draw.draw_shadow_animation_when_closed`).
9. Additional animation sprites use direction-specific positions and hotspots. Open buildings advance their animation
   clock to world time; closed buildings keep the stored clock for effect sprites
   (`building_draw.draw_anim_sprites_use_the_hotspot_of_the_direction`).
10. Toilets draw flies above 20 visits and an additional smoke effect above 30. Other tested building types do not draw
    those toilet effects. Broken buildings draw smoke above the top point, except during demolition
    (`building_draw.draw_loo_smoke_and_flies`, `building_draw.draw_broken_building_smokes_above_the_top_point`).
11. The screen rectangle follows projected position, sprite dimensions, drawing offset, camera and hotspot. Screen-cell
    refresh uses animation zero for the offset then restores the selected animation; switching back resets its frame,
    while already being on animation zero preserves the frame
    (`building_draw.update_screen_rect`, `building_draw.update_screen_cells_uses_animation_zero_and_gives_it_back`).
12. Light masking copies overlaps from earlier buildings in the building list, skipping closed buildings, missing light
    maps and daytime light sources. A building absent from the list considers every entry. Copies clip to the buffer;
    a fully offscreen target visits no other building
    (`building_draw.light_mask_copies_the_overlap_of_buildings_listed_before`,
    `building_draw.light_mask_skips_buildings_that_do_not_cast_light`,
    `building_draw.light_mask_clips_to_the_buffer_and_leaves_the_sprite_when_nothing_is_copied`).

## Edge cases
- Negative toilet visits trigger smoke under unsigned comparison (rule 10 test).
- A light-source animation is restored after a copy; if clipping prevents the copy, it can remain on the light-map
  animation (rule 12 clipping test).

## Not covered by tests
The focused tests record rendering calls, coordinates and clocks. They do not judge the visual appearance of previews,
smoke, shadows or light blending. The live draw test asserts that draw, overlay and mask calls occur, without pixel checks.

## Tests
`tests/unit/building_draw_test.cpp`, `tests/building_draw.txt`.
