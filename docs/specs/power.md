# Spec: power grid

## Purpose
Adds up what the buildings draw and what the generators deliver, and when the grid cannot cover the demand, switches
buildings off one at a time (one random building per frame) until the draw fits.

## Vocabulary
- **Power value** of a building: positive = consumer draw, negative = generator (its capacity), 0 = no power.
- **Sums**: `total` (draw of every consumer), `active` (draw of consumers not switched off for lack of power),
  `installed` (generator capacity), `output` (what the generators deliver now). All are single-precision floats.
- **Shed**: switching one consumer off for lack of power.

## Rules
1. A consumer adds its power to `total`, and to `active` too unless it is switched off for lack of power; a generator adds
   its capacity (as a positive number) to `installed` and its present output to `output`
   (`world_power.sums_split_consumers_and_generators`: 1000 + 800 off + generator 7500 gives total 1800, active 1000,
   installed 7500, output 7500).
2. A generator with a build or demolish job adds no capacity to `installed` but its output still counts
   (`world_power.generator_with_a_job_adds_no_capacity_but_still_delivers`); the demolish check is skipped when the build
   check already said yes, and consumers are not asked about jobs (same test).
3. Every addition is rounded to a float when stored, so a large sum can lose the odd unit, and an integer above 2^24 is
   rounded through a float first (`world_power.sums_round_to_float_on_every_store`).
4. No buildings give all four sums 0 (`world_power.no_buildings_means_zero_sums`).
5. Shedding is due when the demand not yet switched off is above the delivered output while a generator is off (output
   below installed), or when there is no generator (installed below the float epsilon). Fully switched-on generators never
   shed, even when overloaded; a demand equal to the output does not shed (`world_power.shed_rule_truth_table`).
6. Sums that are not numbers give a defined answer: a demand or an output that is not a number never sheds through the
   first branch (demand above output); an installed capacity that is not a number counts as "no generator" and as
   "output below installed" (`world_power.shed_rule_with_nan_follows_the_fpu_flags`; the name is historical).
7. When shedding is due, one building index is drawn as `random % building count` and that building is switched off if it
   draws power (power above 0; neither an idle building nor a generator), is not already off, has no build or demolish job
   and is not exempt; the call returns its index, otherwise -1 and no other building is tried. Exactly one random number is used per call
   (`world_power.shed_draws_once_and_sheds_an_eligible_building`, `world_power.shed_skips_buildings_that_do_not_qualify`: the
   draw happens before the checks).
8. Exempt buildings are the mechanics' and builders' homes, hotels, the complaints hut and the accommodation block (keys
   `HANDYMAN`, `BUILDERYARD`, `HOTEL`, `COMPLAINTHUT`, `ACCOMBLK`); similar keys such as `HOTELS` and `HOTE` and other
   buildings are not exempt (`world_power.shed_exemptions_are_the_five_power_exempt_behaviors`).
9. With the rule off or no buildings, nothing is drawn (`world_power.shed_does_not_draw_when_the_rule_is_off_or_the_list_is_empty`).
10. The drawn index is an unsigned remainder (`world_power.shed_index_is_an_unsigned_remainder`: 12 over 5 buildings gives 2).

## Edge cases
- A random number of 32767 over 3 buildings gives building 1 (`world_power.shed_draws_once_and_sheds_an_eligible_building`).

## Game scripts
- `tests/power.txt` (scenario 1, two generators, demand 4150): the world's totals equal a recomputation from the building
  list (demand 4150, capacity 15000, output 15000); closing all generators lets the demand fall to 2850 within 10000 steps
  (shed buildings, and output 0); the totals stay consistent for 120 frames; reopening the generators restores the demand
  to 4150 and the output to 15000 and the totals still agree.

## Not covered by tests
- The start-up rule for a new building (`tests/power_startup.txt` issues eight start-up cases, for example a shop with
  total demand 20000 against output 15000, and prints the state, but has no expectation lines): its comment says a new
  building starts shut down only when output is below the demand of all consumers, unless its power is below 1, or it is the
  handyman while a generator is off. That rule is not asserted anywhere.
- Which buildings the shedding picks first in a real run (random by design).

## Tests
`tests/unit/world_power_test.cpp`, game scripts `tests/power.txt`, `tests/power_startup.txt`.
