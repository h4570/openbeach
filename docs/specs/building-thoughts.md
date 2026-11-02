# Spec: building thoughts

## Purpose
Keep the recent guest comments associated with a building.

## Rules
1. A new thought owns a copy of its text and remembers its object's identity. New thoughts appear first; changing the
   supplied text afterward does not alter the stored thought (`building_thoughts.add_copies_text_newest_first`).
2. Only the newest eight thoughts remain (`building_thoughts.list_keeps_the_newest_eight`).
3. Removing an object's thoughts removes every matching entry and preserves the other entries' order
   (`building_thoughts.remove_takes_every_thought_of_the_object`).
4. Removing an unknown object or removing from an empty list does nothing. A null entry is skipped and retained
   (`building_thoughts.remove_of_a_stranger_changes_nothing`, `building_thoughts.remove_skips_a_null_entry`).
5. Indexed access counts from the newest thought; negative indices and indices past the end return no thought
   (`building_thoughts.at_answers_null_past_either_end`).

## Edge cases
An interspersed null entry does not prevent the removal of a matching thought (rule 4 test).

## Not covered by tests
Adding to a list already larger than eight removes only one oldest entry before inserting the new thought; normal
insertion never creates that oversized list. No test supplies an oversized list to insertion.

## Tests
`tests/unit/building_thoughts_test.cpp`. Text presentation is in [variable-text.md](variable-text.md).
