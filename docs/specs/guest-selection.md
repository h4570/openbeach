# Spec: guest selection and hovering

## Purpose
What happens to a guest when the pointer hovers over it, when it is clicked, and how a screen point is tested against it.

## Vocabulary
- **Select level**: *not selected* (0), *hovered* (1) or *selected*. Any other value is *odd*.
- **Outline**: the highlight line (keyline) of the guest's sprite.

## Rules
1. Reducing a selection turns a selected guest into a hovered one and leaves every other level (0, 1, 3, -1) alone
   (`guest_select.reduce_turns_only_a_selected_guest_into_a_hovered_one`).
2. Clearing a selection sets the level to not selected. The outline stays when the caller says everything is outlined and is
   dropped otherwise (`guest_select.clear_drops_the_outline_unless_everything_is_outlined`).
3. Hovering (no click) an unselected guest makes it hovered and outlines it, and nothing else happens. A guest that is
   already hovered is not outlined again. When everything is outlined anyway, the guest becomes hovered but the outline is
   not touched (`guest_select.hovering_outlines_an_unselected_guest_and_nothing_else`).
4. A click on an unselected guest: clears the guest selection, shows the guest panel, makes the guest selected and outlined;
   when the caller asks for it, it also forgets the previous selection of the interface, centres the camera on the guest
   (world position minus half the view size divided by the zoom: view 800 x 600, zoom 0.5, guest at 1000 / 2000 gives camera
   200 / 1400) and stops the camera movement. Without the camera request only "clear, show" happens
   (`guest_select.a_click_opens_the_panel_and_centres_the_camera_only_from_unselected`).
5. A click on a hovered guest does the same without the camera move and ends selected
   (`guest_select.a_click_opens_the_panel_and_centres_the_camera_only_from_unselected`).
6. A click on a selected guest closes the panel, clears the selection and drops the outline
   (`guest_select.a_click_on_a_selected_guest_closes_the_panel_and_other_levels_do_nothing`).
7. A guest with an odd level reacts to neither clicks nor hovering (`guest_select.a_click_on_a_selected_guest_closes_the_panel_and_other_levels_do_nothing`).
8. Half the view is the view size (unsigned) divided by twice the zoom, truncated: 800 at zoom 1 gives 400, 801 gives 400,
   800 at zoom 2 gives 200 (`guest_select.half_view_divides_the_unsigned_size_by_twice_the_zoom`).
9. Testing a screen point converts the guest's world position to the screen, adds the draw offset and the first opaque pixel
   of the drawn sprite (screen 100 / 200, offset 5, hotspot -7, pixel 3 / 4 tests the point 108 / 197), and the guest is
   selected only when that hit test succeeds; on a miss nothing is selected
   (`guest_select.try_select_tests_the_first_opaque_pixel_of_the_drawn_sprite`).

## Not covered by tests
- Which numeric value the *selected* level has and what the panel shows.
- How the interface decides which guest is under the pointer when several overlap.

## Tests
`tests/unit/guest_selection_test.cpp`.
