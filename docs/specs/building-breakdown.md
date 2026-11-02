# Spec: building breakdown

## Purpose
Describe wear, lightning damage and preventive inspection requests.

## Vocabulary
Wear 0 is sound, 1 and 2 are damaged, and 3 is broken. The wear clock counts whole game hours from day zero.

## Rules
1. Lightning advances wear 0 to 1, 1 to 2, and 2 to 3 in building state 0. Broken buildings and other building states
   ignore it (`building_breakdown.lightning_wears_a_working_building_step_by_step`,
   `building_breakdown.lightning_leaves_a_broken_building_and_other_states_alone`).
2. Periodic wear needs both permission for breakdowns and a nonzero failure percentage. Otherwise it makes no requests
   or clock changes (`building_breakdown.wear_needs_a_fail_percent_and_breakdowns_allowed`).
3. The clock truncates fractional hours toward zero. A roll occurs once the configured failure interval has elapsed;
   a fractional interval needs the next whole hour, and a NaN interval never rolls
   (`building_breakdown.wear_clock_counts_whole_hours`, `building_breakdown.wear_rolls_every_avfailtime_hours`).
4. Failure chance is age times the age factor plus the base percentage, capped at 90 using unsigned arithmetic.
   The random value modulo 100 must be strictly below that chance
   (`building_breakdown.wear_chance_is_capped_at_90_unsigned`).
5. A successful roll advances wear through 1 and 2. On reaching 3 it closes the building, records that it broke,
   clears its queue, hides staff, stamps the fault and increments its count, announces the break, then requests repair.
   Staff hiding precedes the fault stamp; the announcement follows it
   (`building_breakdown.wear_steps_and_the_break_sequence`).
6. A pending repair prevents a duplicate repair request. Successful rolls on wear values other than 0, 1 and 2 do not
   change wear (`building_breakdown.wear_break_with_a_repair_already_called`,
   `building_breakdown.wear_other_values_never_step`).
7. Inspection requires a nonzero interval, no pending inspection, and enough elapsed hours since both the last call
   and last repair. Before requesting it, the current interval becomes the user's interval; the call is stamped after
   the request. Inspection precedes a wear roll due in the same update
   (`building_breakdown.inspection_is_due_after_the_interval_since_the_call_and_the_repair`,
   `building_breakdown.inspection_is_not_due`).
8. In a running world, three strikes break and close the tested buildings; further strikes leave them broken.
   Setting them broken directly produces broken buildings without closing them (`building_breakdown.txt`).

## Edge cases
- A future roll or inspection-call stamp counts as a large unsigned elapsed interval, whereas a future repair stamp
  prevents inspection (rule 3 and 7 tests).
- Negative wear takes lightning's breaking branch, while a large positive wear does nothing
  (`building_breakdown.a_negative_wear_takes_the_breaking_branch_like_the_signed_compare`).

## Not covered by tests
The focused wear tests record requests and notices; they do not assert the displayed wording of the breakdown notice.

## Tests
`tests/unit/building_breakdown_test.cpp`, `tests/building_breakdown.txt`.
