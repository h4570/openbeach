# Spec: hiding and showing staff and show actors

## Purpose
When a building view needs the staff out of sight (and shows back in), which objects are hidden and shown, and what happens
to a running show.

## Vocabulary
- **Staff class**: an object class counted as staff. Classes 6, 14, 15, 16, 17 and 18 are; every other class (0..31 checked)
  and -1 are not.
- **Lists**: the serving staff (with a busy flag each), the non-serving performers, and the tables.

## Rules
1. The staff classes are exactly 6, 0x0e, 0x0f, 0x10, 0x11 and 0x12 (`building_staff_visibility.staff_classes`).
2. Hiding goes through the first `serving` members of the staff list: a member of a staff class becomes invisible and its busy
   flag is cleared; a member of another class keeps both. Members beyond the count are untouched
   (`building_staff_visibility.hide_serving_staff_clears_busy_and_visibility_of_matching_classes_only`).
3. The non-serving performers follow the same class test (a cabaret performer is hidden, a mechanic is not) and have no busy
   flags. Showing makes both visible again (`building_staff_visibility.non_serving_performers_follow_the_class_test_without_busy_flags`).
4. Showing does not touch the busy flags (`building_staff_visibility.show_does_not_touch_the_busy_flags`).
5. For tables whose definition has plates the waiter of the table is hidden and shown; the table itself is not hidden, and a
   table without plates has no waiter step (`building_staff_visibility.table_waiters_only_for_tables_with_plates`).
6. A waiter that is not a living object is skipped (`building_staff_visibility.missing_waiter_object_is_skipped`).
7. Hiding with the show system present and a show active, after the visibility pass hides the show actors and then cancels
   the show (`building_staff_visibility.hide_cancels_an_active_show_after_the_visibility_pass`).
8. Hiding with an idle show, or without the show system, changes visibility only
   (`building_staff_visibility.hide_leaves_an_idle_or_missing_show_alone`).
9. Showing, when the show system exists, shows the actors without asking whether a show is active; without the show system
   only visibility changes (`building_staff_visibility.show_shows_the_actors_of_any_show_without_asking`).
10. Empty lists (and a negative table count) do nothing when hiding without a show system (`building_staff_visibility.empty_lists_do_nothing`).

## Not covered by tests
- When the building view hides or shows the staff.
- Whether empty lists also skip the show step when a show system exists.

## Tests
`tests/unit/building_staff_visibility_test.cpp`.
