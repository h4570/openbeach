# Spec: world parameters (gameplay definition tables)

## Purpose
Builds the tables that define the game's content from the gameplay definition file: commodity families and commodities,
opening hours, desires, buildings, staff, guests, vehicles, tables, events, beers, fines, scenery and the name pools. Other
modules look records up by key and read their fields. The file reader and the field writers belong to the data converter
(`importer/src/readers/`); the game keeps the lookups (`src/game/world/world_params_queries.h`).

## Vocabulary
- **Line**: a keyword string and its value strings, each in double quotes; text outside quotes (comments, line breaks) is
  ignored. A keyword is `PREFIX_REST`.
- **Creating line**: `COMFAM_x`, `COM_x`, `HOURS_x`, `DESIRE_x`, `BUILD_x`, `STAFF_x`, `GUEST_x`, `VEHICLE_x`, `TABLE_x`,
  `EVENT_x`, `BEER_x`, `FINE_x`, `SCENERY_x` start a new record (or, for commodities, hours and staff, carry the whole record
  in the line). **Continuing line**: `KEY_FIELD` adds a field to the record whose key is `KEY`.
- **Record id**: the position in its table, counted from 0. Find-by-key calls return the id stored in the record.
- **Counted records**: the first `m_num*` records of a table; searches look only at those.

## Rules
### Reading the file
1. An odd number of quotes in the file is a fatal error reported before any record is made
   (`world_params_process.odd_quote_count_is_a_fatal_error_before_any_record`); an empty file or one with only a comment
   changes nothing (`world_params_process.empty_input_changes_nothing`).
2. A family line (`COMFAM_key` plus a name), a commodity line (`COM_key`, a name and the family's key) and an hours line
   (`HOURS_key`, open, close) each create a record; the commodity stores the id of its family found by key
   (`world_params_process.families_commodities_and_hours_have_native_values`).
3. A commodity whose family key is unknown is fatal and stops the parse; the commodity is not added
   (`world_params_process.commodity_with_an_unknown_family_is_fatal`). An unknown keyword prefix is also fatal and stops the
   parse; records read before it stay (`world_params_process.unknown_prefix_is_fatal_and_stops_the_parse`).
4. A `BUILD_x` line creates a building (id = position, no footprint yet); lines `x_FIELD` continue the building whose key is
   `x`, also after another building was created in between (`world_params_process.building_lines_continue_the_record_whose_key_is_the_prefix`).
5. The prefix ends at the first underscore, except when that first part is `JETTY` or `SECURITY`: then it runs to the next
   underscore, so keys such as `JETTY_SMALL` and `SECURITY_POST` keep theirs
   (`world_params_process.jetty_and_security_keep_their_underscore_in_the_prefix`).
6. When a building and a guest have the same key, the line belongs to the building: the building lookup runs first
   (`world_params_process.the_first_matching_table_wins_when_keys_clash`).
7. A desire line creates a desire; its `x_COMFAM` lines append family ids in file order; any other desire field is fatal
   (`world_params_process.desire_families_are_listed_in_file_order`, `world_params_process.desire_field_other_than_comfam_is_fatal`).
8. A staff line carries the key and twelve values (name, sex, wage, sprite, object type, menu, recommended wage, scale,
   expectation growth, initial happiness, start hour, end hour) (`world_params_process.staff_takes_the_key_and_twelve_values`).
9. Guest keywords are routed to a scalar field or to a pair of range values (`x_ALCOHOLPREF`, `x_CLEANLINESS_START` take two
   values); an unknown guest keyword is fatal (`world_params_process.guest_keywords_route_to_the_scalar_or_the_range_writer`,
   `world_params_process.unknown_guest_keyword_is_fatal`).
10. Vehicle, table, event, beer, fine and scenery lines create a record and continue by `KEY_FIELD`; a fine accepts only
    `COST`, any other fine field is fatal (`world_params_process.vehicles_tables_events_beers_fines_and_scenery_create_then_continue`,
    `world_params_process.fine_field_other_than_cost_is_fatal`).
11. `MALENAME`, `FEMALENAME` and `SURNAME` lines add the text key that follows to three name pools, counted separately
    (`world_params_process.name_pools_take_the_text_key_after_the_keyword`).
12. The tables grow as needed and keep long keys and names whole: 25 events, 7 beers, 8 hours, 31 families, 50 commodities and
    50 desires with 30 families each are read without truncation (`world_params_process.native_leaves_grow_and_preserve_long_keys`,
    `world_params_process.native_commodity_and_desire_tables_grow_without_truncation`).
13. Parsing blanks the text outside the quotes and the closing quotes in the caller's buffer
    (`world_params_process.the_buffer_is_zeroed_outside_the_quotes_and_the_closing_quotes`).
14. Reset empties the family, commodity, desire, event, fine and hours tables and the event count; beers are kept or cleared
    as the caller asks (`world_params_process.reset_clears_native_tables_and_can_keep_beers`).

### Adding fields (`world_params_add`)
15. Staff records are appended in file order with ids 0, 1, ...; a sex other than M or F is fatal (female flag 1 for F)
    (`world_params_add.staff_appends_records_in_file_order`, `world_params_add.staff_rejects_a_sex_other_than_m_or_f`).
16. The name pools are three runs of one array: male names from index 0, female names from 140, surnames from 280; an
    unknown pool keyword is fatal (`world_params_add.names_fill_three_pools_of_one_array`).
17. Event fields: `COST`, `DEF_PRICE`, `MALE_INTENSITY`, `FEMALE_INTENSITY`; availability is left to the scenario script; an
    unknown field is fatal (`world_params_add.event_fields`).
18. Beer fields: `BRAND`, `COCKTAIL`, `BASECOST`, `STRCOST`, `ALCSTR`, `PRICE`, and `EFFECT` with FIGHT, FLIRT, VANDAL, HAPPY,
    SAD as 0 to 4; every call sets the script flag; an invalid effect or field is fatal (`world_params_add.beer_fields_and_effects`).
19. Vehicle fields cover name, sprite, grid size, capacity, speed, force, mass, towed vehicle, animation set, creation point,
    `MOVETYPE` (POINTS 1, BOUNDARY 2) and `STAFF` (appended as staff ids, found by key). An invalid move type, an unknown staff
    key or an unknown field is fatal, and a failed `STAFF` is not counted (`world_params_add.vehicle_fields`,
    `world_params_add.vehicle_errors`).
20. Scenery fields cover name, sprite, animation, grid size, build cost, collision, capacity, menu, icon, rating and the flags
    for cleaning, repair, burning, user removal, vegetation, path and water. `BUILDON` values LAND, BEACH, SHALLOW_WATER,
    DEEP_SEA and PATH combine into a mask; WATER is ignored for scenery, as is an unknown field; the eight `CATEGORY` words
    map to 0 to 7 and another word is fatal (`world_params_add.scenery_fields`, `world_params_add.scenery_categories`).

### Finding records (`world_params_queries`)
21. Each table can be searched by key (families, commodities, buildings, events, beers, fines, staff, guests, vehicles,
    tables, scenery) and returns the stored id, not the position (`world_params_queries.find_each_table_by_key`,
    `world_params_queries.find_returns_the_stored_id_not_the_index`, `world_params_queries.scenery_id_and_the_four_io_leftover_accessors`).
22. A key must match exactly: case, whole key and the right table; a miss is `kNotFound`. Only counted records are searched,
    the first of duplicate keys wins, and an empty key matches a record with an empty key
    (`world_params_queries.find_is_case_sensitive_and_exact`, `world_params_queries.find_searches_only_the_counted_records`,
    `world_params_queries.find_returns_the_first_of_duplicate_keys`, `world_params_queries.find_empty_key_matches_an_empty_record_key`).
23. Accessors return the record at an id (building, event, beer, staff, vehicle, table, scenery, fine, guest), a building's
    key, a commodity's family, its key (the commodity name accessor) and its display name; they do not check the id, so an id
    past the counted records or a negative one is plain address arithmetic
    (`world_params_queries.accessors_match_the_field_offsets`, `world_params_queries.accessors_read_the_record_fields`,
    `world_params_queries.accessors_do_not_bounds_check`, `world_params_queries.scenery_id_and_the_four_io_leftover_accessors`).

## Not covered by tests
- The remaining field-by-field rules of the add tests, still to be written as rules (the tests exist, this spec does not
  state their rules yet): guest fields, limits and ranges (`world_params_add.guest_scalar_fields_land_in_the_record`,
  `world_params_add.guest_limits_gender_and_drunk_type`, `world_params_add.guest_float_ranges_land_in_the_pair_lists`); table
  fields and chair, walk and animation lists (`world_params_add.table_scalars_strings_and_heap_arrays`,
  `world_params_add.table_point_lists_use_running_counts`, `world_params_add.table_chair_walk_lists_and_anims`); building
  fields, the default beer, `BUILDON` masks, lists, events, commodities, animation sprites, walk points and base plates
  (`world_params_add.building_scalars_and_strings_land_in_the_fields`,
  `world_params_add.building_resets_the_default_beer_on_every_line`, `world_params_add.building_buildon_masks_and_errors`,
  `world_params_add.building_lists_with_running_counts`, `world_params_add.building_event_id_lands_at_the_global_event_count`,
  `world_params_add.building_event_past_the_slots_only_counts`,
  `world_params_add.building_commodity_splits_the_value_string_in_place`,
  `world_params_add.building_animsprite_interleaves_the_coordinates`,
  `world_params_add.building_walkpoints_and_baseplates_allocate_by_count`).
- Which scenario or building uses which record; the tests use small invented tables.

## Tests
`tests/unit/world_params_process_test.cpp`, `tests/unit/world_params_add_test.cpp`, `tests/unit/world_params_queries_test.cpp`.
