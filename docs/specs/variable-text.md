# Spec: variable text

## Purpose
Resolve building, guest and staff text requests for dialogs into localized labels, numbers and current status.

## Vocabulary
A key is the text request, such as `BUILDING_NAME`. Tests using a fake translator assert lookup keys or formatting
arguments; they do not establish the final translated sentence.

## Rules
1. Building names look up `BUILDING_NAME_` plus the building key. Closed status looks up `CLOSED`; unsupported open
   status leaves the supplied output unchanged (`building_text.name_and_open`).
2. Opening-hour bands resolve all-day, morning, day, evening and night labels. An unknown band returns
   `HOURS KEYWORD NOT RECOGNISED` (`building_text.opening_hours_bands`).
3. Capacity and current guests are integers, including negative current counts; satisfaction has two decimal places
   (`building_text.guests_and_satisfaction`).
4. Money includes a dollar sign and two decimals, with the minus before the dollar sign. Integer rounding uses ties to
   even: 2.5 becomes 2 and 3.5 becomes 4 (`building_text.money_rounding_signs_and_edges`).
5. Power consumption is shown directly. Negative power denotes generation and becomes zero while closed; its title
   is output rather than consumption. Output at wear 0, 1, 2, 3 and 4 is respectively 100, 75, 50, 25 and 100 percent
   for the tested generator (`building_text.power_and_title`, `building_text.output_power_scales_with_wear`).
6. Age zero, one and multiple years choose distinct labels. Fault and inspection times use a one-based day; a negative
   time means no recorded event (`building_text.age_and_event_times`).
7. Repair statuses 0 through 6 select no-mechanics, unreachable, not-needed, required, inspection-required,
   mechanic-coming and repairing labels. An urgent request changes statuses 2 and 3 to waiting and requested.
   Cleaning statuses 1 through 5 select no-cleaners, not-needed, required, on-route and cleaning. Unsupported statuses
   leave output unchanged (`building_text.repair_and_cleaning_status`).
8. Faults, inspection interval, visits and visits since cleaning print as integers. An urgent cleaning call disables the
   cleaner-call control; the tested mechanic-call control is disabled without staff and enabled with staff
   (`building_text.counters_and_call_state`).
9. Commodity prices and plain profits have two decimals; money profits include the currency sign. Beer exposes cost,
   price, profit and two-decimal strength. Tiny commodity money profits and NaN profits display zero
   (`building_text.beer_and_commodity_prices`).
10. Commodity names use localized commodity labels; beer in bars uses its brand and in cocktail bars its cocktail name.
    Beer display-name matching ignores case (`building_text.commodity_names`).
11. Building thought requests select by index and can show the associated guest name. A missing numeric suffix chooses
    entry zero; an index past the list displays an empty thought (`building_text.thoughts`).
12. Building-open controls distinguish open from nonzero closed status. The normal-hours availability control includes
    opening but excludes closing; a midnight-wrapping interval includes the closing hour. Equal endpoints are all-day.
    Broken wear and power closure disable availability (`building_text.open_status`).
13. Guest arrival/departure text passes a combined total and the two constituent counts to its format. Construction
    and demolition percent text truncates (0.375 becomes 37%); plain progress uses decimal formatting
    (`building_text.guest_counters`, `building_text.demolish_and_construction_progress`).
14. Event text supplies price, profit and slot-number/hour/minute start time. A price suffix accepts trailing characters;
    profit requires the exact suffix. Unknown event suffixes leave output unchanged without a fatal-key report
    (`building_text.event_slots`).
15. Beach-open text requires both areas open and stops querying when the first is closed. Unsupported building keys
    report a fatal key and leave output unchanged (`building_text.staff_beach_and_unrecognized_key`).
16. Guest strongest desire keeps the earlier equal maximum. All negative desires select zero in the fixture; the
    tested NaN input selects the scared desire (`guest_text.desire_ties_and_unordered_match_x87`).
17. Leaving and entering activities override other activity choices. A current commodity supplies the activity label;
    a missing commodity or the tested strolling state falls back to strolling. For the activity-choice table, commodity
    values 0, 1, 2, 3, 4 and 6 mean strolling, sunbathing, swimming, fighting, flirting and peeing; other tested values
    mean unknown (`guest_text.activity_priorities_and_missing_building`,
    `guest_text_more.activity_follows_the_desired_commodity`).
18. Guest best/worst text uses the visited building's name without a spot opinion. With an event opinion, best events
    4 and 5 choose the topless and wet-T-shirt labels; others tested choose nothing. Worst event 10 chooses drowned;
    the tested fallback events use the best-nothing label (`guest_text.events_keep_the_fallback_and_building_name`,
    `guest_text_more.best_visit_thoughts`).
19. Guest happiness percentages and cash truncate to integers. Headache desire is capped at 1 in the tested high case,
    whereas meals desire remains 12. Thought text is copied literally, including a percent character; thought -1 is
    empty. Wrong-case keys report a fatal key (`guest_text.clamps_formats_days_and_unrecognized_key`).
20. Overall guest opinion sums seven opinions and scales by the single-precision factor 0.14285715, passed for
    two-decimal formatting (`guest_text_more.overall_opinion_is_the_single_precision_mean`).
21. Base staff activity is empty in state 0, starting work in 1, sacked in 6 and 76, attention in 77, and being directed
    in 131 and 132. Other tested states show `Not done yet`
    (`staff_variable_text.base_activity_follows_the_state_switch`).
22. Staff wages are integers; daily wages multiply by ten, happiness is an integer percent, and shift hours are integers.
    Wage requests accept trailing suffixes. Home resolves a building label, feedback requests copy one of three stored
    texts, and dismissal uses ten times recommended wage
    (`staff_variable_text.base_numbers_and_hours`, `staff_variable_text.base_home_thoughts_and_dismissal`).
23. Builder activity states 95 through 103 respectively show resting, heading home, going to build, going to demolish,
    building, demolishing, resting, resting and idle. Its other requests supply destination building, script name and
    builder title; shared wage requests delegate to base text (`staff_variable_text.builder_states_and_own_keywords`).
24. Representative activity states 7 through 11 show resting, patrolling, talking, empty text and idle. Requests refresh
    feedback first; own fields show guests talked to, truncated happiness boost percent, guest time, name and title
    (`staff_variable_text.rep_asks_for_feedback_first_and_answers_its_keywords`).
25. Cleaner states 104 through 117 show choosing a task, going to rest twice, resting twice, going to litter,
    cleaning litter, going to toilet, going to bin, cleaning bin, going to toilet, cleaning toilet twice and idle.
    Requests clear output before refreshing feedback and expose task settings, today's toilet/bin/litter totals,
    name and title (`staff_variable_text.cleaner_states_counters_and_title`).
26. Lifeguard states 78 through 87 show patrol, resting, rescue, going to tower, leaving tower, on tower, talking,
    empty text, telling off and idle. Attempts equal saves plus drowned; rescues show saves. Beach status queries
    the home beach; a missing home leaves cleared output empty. Requests clear output then refresh feedback
    (`staff_variable_text.lifeguard_states_rescues_and_beach`).
27. Mechanic states 88 through 94 show repair, resting, heading home, choosing a job, going to rest, going to repair and
    idle. Requests refresh feedback then clear output. Repair/watering controls report enabled mask bits; the vandal
    control inverts its bit. Today's inspections, repairs, vandal repairs and watering are integers
    (`staff_variable_text.mechanic_asks_feedback_before_clearing_and_reads_the_job_mask`).
28. Security states 118 through 130 show greeting, choosing a task, patrol, going to rest, choosing action, resting,
    suspicious, reprimanding, detaining, escorting, going to rest, patrol and idle. Requests clear output then refresh
    feedback. Cautions and detentions are integers; fines truncate to whole currency units; harshness is real-valued.
    Fine-amount keys accept trailing suffixes (`staff_variable_text.security_guard_states_fines_and_harshness`).
29. Specialized staff activity falls back to base behavior outside its range. Unknown base staff keys report a fatal key
    (`staff_variable_text.builder_states_and_own_keywords`, `staff_variable_text.base_unrecognized_key_is_fatal`).

## Edge cases
- NaN fault time selects no-fault text; unsupported repair wear leaves output untouched
  (`building_text.age_and_event_times`, `building_text.repair_and_cleaning_status`).
- Staff daily-wage multiplication wraps; the tested out-of-range happiness converts to zero
  (`staff_variable_text.base_wage_times_ten_wraps_and_happiness_uses_ftol`).
- Guest arrival-day increment wraps at the integer maximum (`guest_text.clamps_formats_days_and_unrecognized_key`).

## Not covered by tests
The guest resolver also exposes direct name and individual opinion text; the focused guest text tests do not
assert those requests. Translation quality and visible dialog typography are not judged by these resolver tests.

## Tests
`tests/unit/building_variable_text_test.cpp`, `tests/unit/guest_variable_text_test.cpp`,
`tests/unit/guest_variable_text_more_test.cpp`, `tests/unit/staff_variable_text_test.cpp`.
Thought storage is in [building-thoughts.md](building-thoughts.md); label-table lookup is in [text-table.md](text-table.md).
