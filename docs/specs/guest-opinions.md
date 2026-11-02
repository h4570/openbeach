# Spec: guest opinions, thoughts and desires

## Purpose
How a guest's seven opinions change from the world around it and become its happiness and walk mood, which thought text it
picks and what some thoughts cost, and what satisfying or missing a desire does.

## Vocabulary
- **Opinions**: seven values in 0..1 (beauty, cleanliness, totty, variety, safety, crowding and one more), each with a lower and
  an upper **tolerance** and a per-definition **effect** pair (up step, down step).
- **Desires**: one value per desire name (accommodation, alcohol, ..., relief, stroll, leave, scared; 26 slots). The bladder
  desire is "relief".
- **Thought**: a text id remembered in a list of three, newest first.

## Rules: opinion update
1. The update does nothing, and draws no random number, in the states 0x25, 0x26, 0x28 and 0x29
   (`guest_opinions.skipped_states_change_nothing`).
2. Beauty: a scenic value above the upper tolerance raises the opinion by the effect's first value, below the lower tolerance
   lowers it by the second value, inside the band leaves it. Cleanliness is the mirror image for litter: litter above the
   upper bound lowers it by the second value, litter below the lower bound raises it by the first; a lower bound of 0 is never
   undercut (`guest_opinions.beauty_and_litter_bands`).
3. Every opinion is held in 0..1 (`guest_opinions.clamps`).
4. Totty compares the share of guests of the opposite gender (11 of 41 for gender 0, 3 of 41 for gender 1, in the test) with the
   tolerance pair chosen by the guest's attractiveness band: a share above the upper bound raises the opinion, below the lower
   bound lowers it (`guest_opinions.totty_ratio_uses_the_opposite_gender_counts`).
5. Attractiveness bands: up to 0.26 (single-precision 0.25 and 0.26) band 0, 0.5 band 1, from 0.74 (the single-precision
   0.74 lies just above the decimal) band 2; 0.7399 is band 1 (`guest_opinions.totty_ratio_uses_the_opposite_gender_counts`).
6. A guest with no variety left lets every opinion fade by a small fixed fraction of the down step (beauty by 1/12, crowding by
   0.04), whatever the bands say; safety is clamped to 0 (`guest_opinions.no_variety_fades_everything`).
7. Happiness is the mean of the seven opinions (`guest_opinions.happiness_is_the_mean_of_the_seven`).
8. Walk mood: a guest that is sad (happiness below 0.4, sober) switches to the sad walk and stand animations, draws one random
   number and sets its top speed to 30 + (draw mod 7 - 3) (draw 100 gives 29); a drunk guest (0.5 or more) walks drunk and
   overrides sad; a guest returning to normal gets the normal animations and a speed of 60 + (draw mod 15 - 7). A guest already
   walking the right way draws nothing and keeps its speed; a desperate walk stays untouched
   (`guest_opinions.walk_animation_follows_the_mood`).

## Rules: thoughts
9. A thought text key is a prefix plus a digit 1..3 chosen as draw mod 3 + 1 (draws 0, 1, 2, 3, 7, 32767 give 1, 2, 3, 1, 2, 2)
   (`guest_thoughts.key_is_the_prefix_and_a_digit_from_one_to_three`).
10. Some prefixes end in a separator before the digit and some do not, matching the keys of the text table
    (`guest_thoughts.prefixes_are_spelled_like_the_exe`).
11. A thought whose text is known is stored in front and the two older ones move back; the id is returned
    (`guest_thoughts.a_known_text_moves_the_older_thoughts_back`).
12. An unknown text changes the list not at all, but its id (-1) is still returned
    (`guest_thoughts.an_unknown_text_changes_nothing_but_is_still_returned`).
13. The drowning thought is male for gender 0 and female for every other gender
    (`guest_thoughts.drowning_prefix_follows_the_gender`).
14. Being detained for nothing takes 0.4 off every opinion, holds each at 0 (an unordered value becomes 0), shows the next mood
    icon frame and sets the icon timer to 2 (`guest_thoughts.detained_for_nothing_takes_point_four_off_every_opinion`).
15. A closed beach lowers only variety by the definition's down step, without a lower bound (it can go negative), and shows the
    icon like rule 14 (`guest_thoughts.beach_closed_lowers_variety_without_a_bound`).
16. The "no path" ticker message is due only when more than 20 seconds have passed since the last one: exactly 20, a clock that
    went backwards and unordered values never are (`guest_thoughts.ticker_is_due_only_after_more_than_twenty_seconds`).
17. A demolished building around a sleeping guest sets the missed-purchase flag, takes 1.4 off the visit score and 0.2 off every
    opinion, holding at 0 (`guest_thoughts.demolished_asleep_spoils_the_visit_and_lowers_the_opinions`).
18. The ticker key for a blocked path follows the activity (0: the building name plus the digit; 1: sea; otherwise beach by the
    desired commodity) with a digit from the argument mod 3 + 1 (`guest_thoughts.blocked_message_key_follows_the_activity`).

## Rules: desires
19. A desire name is found by exact, case-sensitive comparison; the first match wins, repeats can be searched for after a start
    index, unknown or empty names give the count (`guest_desire_actions.the_name_table_has_one_slot_per_desire_and_the_first_match_wins`).
20. A first miss of a desire in a round only sets the missed flag; nothing else changes
    (`guest_desire_actions.the_first_miss_of_a_round_only_sets_the_flag`).
21. A repeated miss clears that desire and lowers variety by the definition's down step (0.7 - 0.25 = 0.45), holding at 0 and
    turning an unordered value into 0; the flag itself is left to the desire search
    (`guest_desire_actions.a_repeated_miss_clears_the_desire_and_costs_variety`,
    `guest_desire_actions.variety_is_held_at_zero_by_the_unrounded_difference_and_a_nan_goes_to_zero`).
22. A repeated miss of sunbathing, swimming or stroll also resets the activity and spares variety; a repeated miss of relief
    spares variety and the activity (`guest_desire_actions.a_miss_of_an_outdoor_desire_resets_the_activity_and_spares_variety`,
    `guest_desire_actions.a_miss_of_the_toilet_spares_variety_and_the_activity`).
23. An unknown desire name changes no desire and clears the blocked counter (`guest_desire_actions.an_unknown_family_changes_nothing`).
24. Satisfying alcohol, meals, snacks or soft drinks clears that desire, adds 0.1 to relief up to a maximum of 1, and clears the
    blocked counter; not while the guest is in the toilet state
    (`guest_desire_actions.a_drink_or_a_meal_relieves_the_bladder_desire_up_to_one`,
    `guest_desire_actions.the_bladder_is_not_relieved_in_the_toilet_state`).
25. Satisfying relief clears it and puts the walk and stand animations back to normal (`guest_desire_actions.satisfying_the_toilet_puts_the_animations_back_to_normal`).
26. Satisfying sunbathing, swimming or stroll resets the activity; any other desire (amusement) clears itself only
    (`guest_desire_actions.satisfying_an_outdoor_desire_resets_the_activity`).
27. Setup copies the definition's start values for bladder, fight, flirt, drunkenness, food illness, sunburn and vandalism
    into the guest and touches nothing else (`guest_desire_actions.setup_copies_the_start_values_of_the_definition`).

## Not covered by tests
- Where the scenic value, litter and the guest counts come from.
- What the seventh opinion is, and what each tolerance pair means beyond the beauty, cleanliness and totty cases tested.
- Which desires the guest picks next and when thoughts are chosen.

## Tests
`tests/unit/guest_opinions_test.cpp`, `tests/unit/guest_thoughts_test.cpp`, `tests/unit/guest_desire_actions_test.cpp`.
