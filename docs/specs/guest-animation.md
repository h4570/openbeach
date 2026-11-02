# Spec: guest animation names and end messages

## Purpose
How a guest resolves the animation names that scripts use, and which messages follow when an animation ends.

## Vocabulary
- **Names**: the guest's four current animation names: message, walk, stand and return.
- **Special name**: a script name that stands for one of the guest's own animations, such as `WalkAnimation`.

## Rules
1. `WalkAnimation`, `StandAnimation` and `ReturnAnimation` resolve to the guest's walk, stand and return names. Matching is
   case sensitive; any other name stays as it is (`guest_anim.special_names_stand_for_the_guests_animations_and_are_case_sensitive`).
2. `MessData` resolves to the guest's message name, and that result is resolved once more, so a message name that is
   itself special (`WalkAnimation`, a fidget) is resolved too (`guest_anim.the_message_name_is_resolved_again`).
3. `FidgetAnimation1` and `FidgetAnimation2` (any case) resolve to the stand name with `_FIDGET1` / `_FIDGET2` appended.
   The stand name is upper-cased, and a `_NORMAL` ending is removed first (`D_STAND_NORMAL` gives `D_STAND_FIDGET1`)
   (`guest_anim.fidgets_hang_on_the_stand_animation_and_ignore_case`).
4. A stand name of 39 characters plus the fidget suffix still fits the scratch buffer
   (`guest_anim.the_longest_fidget_fits_the_scratch_buffer`).
5. The return target of an animation is its own name when there is no finish name; `WalkAnimation` and `StandAnimation`
   map to the guest's names; `ReturnAnimation` stays unresolved text; any other finish name is used as is
   (`guest_anim.return_to_is_the_name_or_the_finish_with_two_aliases`).
6. The animation type is reverse if reverse is requested (also with looping), loop if only looping, otherwise once
   (`guest_anim.reverse_wins_over_looping`).
7. A duration of 0, -0 or NaN becomes 1; every other value, also a negative one, is kept
   (`guest_anim.a_zero_or_unordered_duration_becomes_one`).
8. When an animation ends: the leave state is the current state unless one is set; the end-state message is sent unless
   the return animation is set or the end state is the default; the delayed leave message is not sent when the leave
   state equals the current state and the return animation is set (`guest_anim.the_end_messages_follow_the_state_and_the_return_animation`).

## Not covered by tests
- Playing, advancing and drawing the animations; sprite lookup.
- Fidget names whose stand name does not end in `_NORMAL` beyond the upper-casing shown above.

## Tests
`tests/unit/guest_animation_test.cpp`.
