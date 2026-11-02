# Spec: building construction (creating the object)

Not the construction manager (jobs and builders): that is [construction.md](construction.md). This is what a newly made
building object starts with.

## Purpose
The values and sprites a building has right after it is created from its definition key.

## Rules
1. The order is: the base part with the key, one random number (repair interval), the main sprite, one sprite per animation
   entry of the definition, the shortage and spanner icon sprites, then two more random numbers (lights on and off times).
   The repair interval is derived from the first number, the user repair interval is twice it; lights on is near 19.5 and
   lights off near 5.5 plus a random spread of up to one hour. The last repair call and last fail time are the current day
   time, the influence area is none (`building_construction.order_and_rng`).
2. Defaults: open, burning flag on, entrance 0, influence area none, fault and repair day times -1, queue direction
   (-12, 12), no queue, events, construction sprite or walk points, no lilos, zeroed profit record, no animation sprites,
   icon sprites at 2 frames per second, no beer (`building_construction.defaults`).
3. Both screen rectangles start empty (`building_construction.screen_rects_start_empty`).
4. Equipment hire starts with 20 lilos, boards and snorkels; another key does not. An animation sprite named like the beach
   disco (compared without case) gets its own type; others keep type 0. A custom construction sprite is created from the
   definition, and the walk points are taken from the definition (`building_construction.equipment_hire_and_extras`).
5. Commodity slots copy the intensities from the definition and start with price factor 1. A slot that sells alcohol takes
   the definition's default beer: cost is the beer's base cost plus strength cost times the default strength, price is the
   beer's price; other slots keep the definition's values (`building_construction.commodity_slots_and_beer`).
6. If the default beer is not in use, the first beer that is in use is taken
   (`building_construction.beer_not_in_use_takes_the_first_one_that_is`).
7. Without an alcohol commodity there is no beer (`building_construction.no_alcohol_no_beer`).

## Not covered by tests
- Which commodity names the displayed text uses; how creation from a save differs from creation by placing.

## Tests
`tests/unit/building_construction_test.cpp`; game scripts `tests/building_construct.txt`, `building_object.txt`.
