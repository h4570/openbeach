# Unit tests of pure logic: no game process, run by ctest (docs/testing/unit-tests.md).
# Sources under test are compiled in directly; no game memory or platform backend is needed.
# Call-counter checks use a capturing log stub to verify failures without a game process.
add_executable(obl_unit_tests
    call_counter_test.cpp
    object_virtuals_seam.cpp
    prop_virtuals_seam.cpp
    staff_virtuals_seam.cpp
    game_core_test.cpp
    game_logic_test.cpp
    platform_main_test.cpp
    det_math_test.cpp
    renderer_jpeg_test.cpp
    gui_manager_dialogs_test.cpp
    gui_manager_buttons_test.cpp
    file_write_test.cpp
    gui_records_test.cpp
    gui_manager_messages_test.cpp
    gui_radar_test.cpp
    gui_manager_frame_test.cpp
    scripted_sound_test.cpp
    loop_sounds_test.cpp
    float_to_int_test.cpp
    text_renderer_test.cpp
    message_handler_test.cpp
    rand_tape_test.cpp
    game_rng_test.cpp
    sound_bank_test.cpp
    spatial_audio_test.cpp
    world_script_host_test.cpp
    player_lifecycle_test.cpp
    cleaning_lifecycle_test.cpp
    cleaning_manager_test.cpp
    construction_queries_test.cpp
    construction_assign_test.cpp
    construction_reallocate_test.cpp
    construction_cost_point_test.cpp
    builder_script_queries_test.cpp
    construction_start_test.cpp
    construction_finish_test.cpp
    construction_workpoints_test.cpp
    construction_footprint_test.cpp
    feedback_manager_test.cpp
    feedback_vocabulary_test.cpp
    guest_opinions_test.cpp
    staff_roster_test.cpp
    staff_thoughts_test.cpp
    staff_lookup_test.cpp
    staff_script_values_test.cpp
    staff_lifeguard_builder_script_values_test.cpp
    lifeguard_fns_test.cpp
    rep_security_test.cpp
    staff_direct_test.cpp
    staff_actions_test.cpp
    staff_profession_script_values_test.cpp
    staff_feedback_test.cpp
    staff_update_test.cpp
    staff_construct_test.cpp
    furniture_test.cpp
    shark_test.cpp
    furniture_draw_test.cpp
    vehicle_lifecycle_test.cpp
    vehicle_messages_test.cpp
    vehicle_helpers_test.cpp
    vehicle_animation_test.cpp
    vehicle_path_helpers_test.cpp
    vehicle_update_test.cpp
    vehicle_presentation_test.cpp
    staff_service_test.cpp
    staff_service_receive_test.cpp
    staff_service_home_test.cpp
    staff_snooty_test.cpp
    staff_call_function_test.cpp
    vehicle_serial_test.cpp
    performer_test.cpp
    staff_receive_test.cpp
    staff_profession_receive_test.cpp
    staff_state_test.cpp
    staff_set_variable_test.cpp
    staff_variable_text_test.cpp
    guest_visit_opinions_test.cpp
    guest_search_desire_test.cpp
    guest_selection_test.cpp
    staff_select_draw_test.cpp
    staff_duties_test.cpp
    guest_draw_test.cpp
    guest_variable_text_test.cpp
    building_variable_text_test.cpp
    guest_update_test.cpp
    guest_script_values_test.cpp
    guest_script_functions_test.cpp
    guest_beach_points_test.cpp
    guest_lifecycle_test.cpp
    guest_construction_test.cpp
    guest_actions_test.cpp
    guest_conflict_test.cpp
    guest_thoughts_test.cpp
    guest_desire_actions_test.cpp
    guest_appearance_test.cpp
    guest_placement_test.cpp
    guest_walk_targets_test.cpp
    guest_pool_test.cpp
    guest_animation_test.cpp
    guest_walk_routes_test.cpp
    guest_walk_path_test.cpp
    guest_lookups_test.cpp
    main.cpp
    unit_test.h
    blit_test.cpp
    sprite_draw_test.cpp
    render_geometry_test.cpp
    map_view_math_test.cpp
    map_view_lifecycle_test.cpp
    map_camera_test.cpp
    map_blocks_test.cpp
    map_rebuild_test.cpp
    map_restore_test.cpp
    map_culling_test.cpp
    day_night_present_test.cpp
    line_draw_test.cpp
    palette_modifiers_test.cpp
    renderer_ops_test.cpp
    map_effects_test.cpp
    water_anim_test.cpp
    sprite_scan_test.cpp
    sprite_preview_test.cpp
    sprite_bake_test.cpp
    color_mix_test.cpp
    command_test.cpp
    command_recorder_test.cpp
    bot_test.cpp
    chaos_test.cpp
    sprite_animation_test.cpp
    display_geometry_test.cpp
    file_io_test.cpp
    os_test.cpp
    ascii_case_test.cpp
    byte_reader_test.cpp
    code_page_test.cpp
    wide_format_test.cpp
    runtime_reports_test.cpp
    ini_file_test.cpp
    image_compare_test.cpp
    image_decode_test.cpp
    cursor_file_test.cpp
    image_scale_test.cpp
    input_mapping_test.cpp
    input_state_test.cpp
    touch_gestures_test.cpp
    state_helpers_test.cpp
    mp3info_test.cpp
    id3tag_test.cpp
    playlist_logic_test.cpp
    playlist_scan_test.cpp
    playlist_stream_test.cpp
    path_tile_script_test.cpp
    path_tiles_test.cpp
    path_tiles_lifecycle_test.cpp
    path_tiles_rebuild_test.cpp
    path_search_test.cpp
    path_route_test.cpp
    mover_access_test.cpp
    mover_events_test.cpp
    mover_physics_test.cpp
    mover_route_test.cpp
    mover_visibility_test.cpp
    person_methods_test.cpp
    mover_messages_test.cpp
    entity_messages_test.cpp
    entity_lifecycle_test.cpp
    entity_accessors_test.cpp
    prop_test.cpp
    map_cache_test.cpp
    mover_target_test.cpp
    mover_lifecycle_test.cpp
    grid_cell_test.cpp
    path_data_test.cpp
    grid_areas_test.cpp
    grid_lifecycle_test.cpp
    grid_math_test.cpp
    grid_class_map_test.cpp
    grid_screen_rect_test.cpp
    object_manager_queries_test.cpp
    scenery_queries_test.cpp
    scenery_placement_test.cpp
    scenery_actions_test.cpp
    scenery_lifecycle_test.cpp
    scenery_logic_test.cpp
    object_manager_lifecycle_test.cpp
    object_manager_serial_test.cpp
    world_queries_test.cpp
    world_params_queries_test.cpp
    world_params_add_test.cpp
    world_params_process_test.cpp
    pis_test.cpp
    text_table_test.cpp
    text_load_test.cpp
    pack_path_test.cpp
    hash_prime_test.cpp
    music_volume_test.cpp
    script_program_test.cpp
    script_program_exec_test.cpp
    world_script_parse_test.cpp
    refpack_test.cpp
    event_manager_test.cpp
    ferry_test.cpp
    commodity_lifecycle_test.cpp
    grid_footprint_walk_test.cpp
    repair_lifecycle_test.cpp
    repair_manager_test.cpp
    vec2_test.cpp
    script_arith_test.cpp
    script_message_test.cpp
    world_script_delegate_test.cpp
    world_script_machine_test.cpp
    gui_canvas_test.cpp
    gui_variable_test.cpp
    gui_variable_image_test.cpp
    gui_modes_tools_test.cpp
    world_snapshot_test.cpp
    world_snapshot_model_test.cpp
    world_sections_test.cpp
    world_daily_stats_test.cpp
    building_open_state_test.cpp
    building_queries_test.cpp
    building_draw_test.cpp
    building_thoughts_test.cpp
    building_staff_visibility_test.cpp
    building_spawn_test.cpp
    building_queue_clear_test.cpp
    building_queue_ops_test.cpp
    building_queue_points_test.cpp
    building_queue_lifecycle_test.cpp
    building_teardown_test.cpp
    building_construction_test.cpp
    building_set_variable_test.cpp
    building_breakdown_test.cpp
    building_update_tail_test.cpp
    building_update_test.cpp
    building_lifecycle_test.cpp
    building_events_test.cpp
    world_power_test.cpp
    world_day_cycle_test.cpp
    world_post_message_test.cpp
    world_control_test.cpp
    world_view_test.cpp
    world_save_test.cpp
    world_load_json_test.cpp
    map_load_json_test.cpp
    world_save_json_test.cpp
    save_sections_json_test.cpp
    save_sections_load_test.cpp
    save_table_reader_test.cpp
    save_objects_json_test.cpp
    save_records_test.cpp
    save_objects_load_test.cpp
    save_guests_load_test.cpp
    world_construct_test.cpp
    world_construct_pis_test.cpp
    world_construct_subsystems_test.cpp
    world_update_test.cpp
    world_objects_test.cpp
    world_receive_test.cpp
    world_create_staff_test.cpp
    world_create_actors_test.cpp
    world_create_props_test.cpp
    world_create_guest_test.cpp
    world_create_building_test.cpp
    world_create_dispatch_test.cpp
    weather_control_test.cpp
    weather_draw_test.cpp
    weather_lifecycle_test.cpp
    weather_lighting_test.cpp
    weather_strike_test.cpp
    weather_update_test.cpp
    gui_sort_test.cpp
    save_slots_test.cpp
    settings_ini_test.cpp
    gui_script_test.cpp
    gui_widgets_test.cpp
    gui_controls_test.cpp
    gui_control_constructors_test.cpp
    gui_containers_test.cpp
    gui_lists_test.cpp
    gui_combo_test.cpp
    gui_populate_test.cpp
    gui_image_list_test.cpp
    gui_tables_test.cpp
    gui_table_frame_test.cpp
    gui_table_collect_test.cpp
    gui_tabs_test.cpp
    gui_dialog_test.cpp
    gui_modes_place_test.cpp
    gui_actions_test.cpp
    game_states_test.cpp
    staff_serial_test.cpp
    building_serial_test.cpp
    guest_serial_test.cpp
    ferry_lifecycle_test.cpp
    cursor_logic_test.cpp
    event_manager_lifecycle_test.cpp
    staff_call_lifeguard_test.cpp
    staff_call_mechanic_test.cpp
    staff_call_cleaner_test.cpp
    staff_call_rep_test.cpp
    staff_call_security_test.cpp
    staff_call_builder_test.cpp
    logic_helpers_direct_test.cpp
    guest_visit_spots_test.cpp
    vehicle_states_test.cpp
    mp3info_file_test.cpp
    image_decode_reject_test.cpp
    script_program_diff_test.cpp
    renderer_dirty_exhaustive_test.cpp
    guest_variable_text_more_test.cpp
    gui_populate_select_test.cpp
    pis_edges_test.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/input_mapping.cpp
    ${CMAKE_SOURCE_DIR}/src/testing/png.cpp
    ${CMAKE_SOURCE_DIR}/src/game/objects/vehicle.cpp
    json_schema_test.cpp
    convert_cabinet_test.cpp
    convert_catalog_test.cpp
    convert_exe_tables_test.cpp
    convert_script_vocabulary_test.cpp
    gui_content_test.cpp
    gui_layouts_test.cpp
    own_credits_test.cpp
    convert_gui_tables_test.cpp
    convert_byte_source_test.cpp
    convert_source_test.cpp
    convert_save_body_test.cpp
    convert_save_checks_test.cpp
    convert_save_encode_test.cpp
    convert_save_writer_test.cpp
    convert_save_stream_test.cpp
    convert_image_test.cpp
    convert_scenario_test.cpp
    convert_map_test.cpp
    convert_map_decode_test.cpp
    convert_pack_test.cpp
    convert_sprite_test.cpp
    convert_text_test.cpp
    convert_gui_layout_test.cpp
    convert_script_test.cpp
    script_pack_test.cpp
    map_pack_test.cpp
    media_pack_test.cpp
    sprite_pack_test.cpp
    convert_sound_test.cpp
    convert_sound_script_test.cpp
    convert_game_parameters_test.cpp
    convert_media_test.cpp
    convert_resource_test.cpp
    convert_guest_set_test.cpp
    convert_web_test.cpp
    convert_import_test.cpp
    guest_set_load_test.cpp)
# sdl_shim stands in for SDL's private SDL_internal.h so SDL's Windows scancode table can serve as an oracle.
target_include_directories(obl_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_shim ${sdl3_SOURCE_DIR}/src/events)
target_link_libraries(obl_unit_tests PRIVATE obl_common obl_convert obl_import_web nlohmann_json::nlohmann_json SDL3::Headers SDL3::SDL3-static)
target_compile_definitions(obl_unit_tests PRIVATE OBL_GAME_DIR="${OBL_GAME_DIR}" OBL_REPO_DIR="${CMAKE_SOURCE_DIR}")

# "data." cases compare against the user's run/ (extractor output, reference the reference build) and report SKIP (77)
# when it is not prepared; every other case is self-contained.
add_test(NAME unit COMMAND obl_unit_tests -data.)
add_test(NAME unit_optimized COMMAND obl_unit_tests construction.clamp_progress_ CONFIGURATIONS RelWithDebInfo)
# The slow cases have entries of their own so the longest entry stays short and a filter can leave them out
# (docs/testing/test-runner.md): unit_data_saves converts the local save corpus, unit_data_maps every scenario map.
set(OBL_DATA_SAVES data.convert_interprets_ data.convert_reads_local_save data.convert_save_ data.convert_writes_local_saves)
set(OBL_DATA_MAPS data.convert_maps_)
list(TRANSFORM OBL_DATA_SAVES PREPEND "-" OUTPUT_VARIABLE OBL_NOT_DATA_SAVES)
list(TRANSFORM OBL_DATA_MAPS PREPEND "-" OUTPUT_VARIABLE OBL_NOT_DATA_MAPS)
add_test(NAME unit_data COMMAND obl_unit_tests data. ${OBL_NOT_DATA_SAVES} ${OBL_NOT_DATA_MAPS})
add_test(NAME unit_data_saves COMMAND obl_unit_tests ${OBL_DATA_SAVES})
add_test(NAME unit_data_maps COMMAND obl_unit_tests ${OBL_DATA_MAPS})
set_tests_properties(unit_data unit_data_saves unit_data_maps PROPERTIES SKIP_RETURN_CODE 77)
# Portable-only configurations have no OpenBeach.exe host.
if(OBL_PORTABLE_ONLY)
    return()
endif()

# OpenBeach.exe starts its host process, which starts the game in process (log, config, packs, every installer);
# --no-entry: no game runs. The ready line names the game's arguments after `--` and comes only after the start-up log
# showed "init done" without a failure. 77 = run/ not prepared.
set(OBL_STANDALONE_READY "ready, game arguments \\[ -arg \"two words\"\\] \\(--no-entry\\)")
add_test(NAME standalone_host COMMAND OpenBeach --no-entry --game-dir ${OBL_GAME_DIR} -- -arg "two words")
set_tests_properties(standalone_host PROPERTIES SKIP_RETURN_CODE 77 PASS_REGULAR_EXPRESSION "${OBL_STANDALONE_READY}")
# The tests below on a campaign scenario need its references; a checkout without them tests the L0 pack alone.
set(OBL_CAMPAIGN_REFERENCES FALSE)
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/replays/headless_s1.replay)
    set(OBL_CAMPAIGN_REFERENCES TRUE)
endif()
if(OBL_CAMPAIGN_REFERENCES)
    set_tests_properties(standalone_host PROPERTIES ENVIRONMENT "OBL_SCRIPT=${CMAKE_SOURCE_DIR}/tests/cursor.txt")
else()
    set_tests_properties(standalone_host PROPERTIES ENVIRONMENT OBL_PACK=l0)
endif()
# Headless mode (docs/architecture/headless/index.md): scenario 1 runs a replay without window, renderer output, audio or input devices.
# The checkpoint hashes were taken in the windowed game, so a pass also proves that both simulate the same, with no SDL subsystem.
# 77 = run/ not prepared.
set(OBL_HEADLESS_PASS "obl-sim: end ticks=600 hash=d603b028e74fd43a checkpoints=4 mismatches=0 applied=4 rejected=1 [^\r\n]* sdl-subsystems=0[\r\n]+obl-sim: PASS[\r\n]+")
if(OBL_CAMPAIGN_REFERENCES)
add_test(NAME headless_sim COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless ${CMAKE_SOURCE_DIR}/tests/replays/headless_s1.replay)
set_tests_properties(headless_sim PROPERTIES SKIP_RETURN_CODE 77
    PASS_REGULAR_EXPRESSION "${OBL_HEADLESS_PASS}"
    FAIL_REGULAR_EXPRESSION "FAILED|MISMATCH")
endif()
# A mismatching checkpoint names every differing section, or prints the actual digests when the file has none (replays.md).
add_test(NAME headless_mismatch COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless ${CMAKE_SOURCE_DIR}/tests/replays/headless_l0_mismatch.replay)
set_tests_properties(headless_mismatch PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT OBL_PACK=l0
    PASS_REGULAR_EXPRESSION "obl-sim: @0 sections=staff[\r\n]+obl-sim: @0 section staff expected=292a594b/84 actual=292a594a/84[\r\n]+.*obl-sim: @150 sections=unrecognized actual=world=[0-9a-f]+/55 economy=.*obl-sim: end ticks=150 [^\r\n]* checkpoints=2 mismatches=2 [^\r\n]*[\r\n]+obl-sim: FAILED")
if(OBL_CAMPAIGN_REFERENCES)
# A test script on the headless loop refuses input/frame/GUI/audio directives and renderer-only probes at load and runs nothing (headless/scripts.md, "Test scripts").
add_test(NAME headless_refuse COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless ${CMAKE_CURRENT_SOURCE_DIR}/headless_refuse.txt)
set_tests_properties(headless_refuse PROPERTIES SKIP_RETURN_CODE 77
    PASS_REGULAR_EXPRESSION "obl-sim: end frames=0 renders=0 passed=0 failed=[0-9]+ refused=5 completed=0 [^\r\n]*[\r\n]+obl-sim: FAILED")
# `start save`: the fixture writes a save of scenario 1 headless (game directory testlogs/headless/save_s1.obl.save), the replay loads
# it like `boot-gameplay <slot>` and checks hashes taken in the hidden game from the same save.
add_test(NAME headless_save_fixture COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless ${CMAKE_CURRENT_SOURCE_DIR}/headless_save_fixture.txt)
set_tests_properties(headless_save_fixture PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_SETUP headless_save
    PASS_REGULAR_EXPRESSION "obl-sim: end [^\r\n]* passed=2 failed=0 refused=0 completed=1 [^\r\n]*[\r\n]+obl-sim: PASS")
add_test(NAME headless_save COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless ${CMAKE_SOURCE_DIR}/tests/replays/headless_save_s1.replay)
set_tests_properties(headless_save PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED headless_save
    PASS_REGULAR_EXPRESSION "obl-sim: end ticks=600 hash=a07d872847e82c9f checkpoints=4 mismatches=0 applied=4 rejected=1 [^\r\n]*[\r\n]+obl-sim: PASS"
    FAIL_REGULAR_EXPRESSION "FAILED|MISMATCH")
# The same save loaded after play in one process gives headless_save's hashes (headless/replays.md, "A save start is a fresh-process load").
add_test(NAME headless_save_reload COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless ${CMAKE_CURRENT_SOURCE_DIR}/headless_save_reload.txt)
set_tests_properties(headless_save_reload PROPERTIES SKIP_RETURN_CODE 77
    PASS_REGULAR_EXPRESSION "obl-sim: end [^\r\n]* passed=5 failed=0 refused=0 completed=1 [^\r\n]*[\r\n]+obl-sim: PASS")
# Headless never opens or decodes the scenario's .biz map file (headless/platform.md, "Headless switches").
add_test(NAME headless_biz_map COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless ${CMAKE_CURRENT_SOURCE_DIR}/headless_biz_map.txt)
set_tests_properties(headless_biz_map PROPERTIES SKIP_RETURN_CODE 77
    PASS_REGULAR_EXPRESSION "obl-sim: end [^\r\n]* passed=1 failed=0 refused=0 completed=1 [^\r\n]*[\r\n]+obl-sim: PASS")
# A script list in one headless process: one result per script and a summary; the scripts after a played world and after a refused
# script still pass (headless/scripts.md, "Script lists").
add_test(NAME headless_batch COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless ${CMAKE_CURRENT_SOURCE_DIR}/headless_batch.list)
set_tests_properties(headless_batch PROPERTIES SKIP_RETURN_CODE 77
    PASS_REGULAR_EXPRESSION "obl-sim: end [^\r\n]* passed=5 failed=0 [^\r\n]*[\r\n]+obl-sim: PASS[\r\n]+obl-sim: scripts=4 passed=3 failed=0 aborted=1 ")
# The reference replays (tests/replays, recorded by the script `record` with `start scenario <n>`) headless: their checkpoints
# were taken in the windowed game. Label `replay` (~110 s in Debug together): runtests.ps1 runs them, `build.ps1 -Test` does not.
foreach(OBL_REPLAY s1_commands s3_resort s12_staff s1_surfaces s1_wages gui_controls s12_bar s12_provider_swap)
    add_test(NAME headless_replay_${OBL_REPLAY} COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless
        ${CMAKE_SOURCE_DIR}/tests/replays/${OBL_REPLAY}.replay)
    set_tests_properties(headless_replay_${OBL_REPLAY} PROPERTIES SKIP_RETURN_CODE 77 LABELS replay
        PASS_REGULAR_EXPRESSION "obl-sim: end ticks=[0-9]+ [^\r\n]* mismatches=0 [^\r\n]*[\r\n]+obl-sim: PASS"
        FAIL_REGULAR_EXPRESSION "FAILED|MISMATCH")
endforeach()
endif()
# The own L0 replays (tests/replays/l0_*, recorded on data/packs/l0, docs/build/ci-l0.md): the same check on the L0 pack.
foreach(OBL_REPLAY l0_commands l0_construction l0_gui l0_rejected)
    add_test(NAME headless_replay_${OBL_REPLAY} COMMAND OpenBeach --game-dir ${OBL_GAME_DIR} --headless
        ${CMAKE_SOURCE_DIR}/tests/replays/${OBL_REPLAY}.replay)
    set_tests_properties(headless_replay_${OBL_REPLAY} PROPERTIES SKIP_RETURN_CODE 77 LABELS replay ENVIRONMENT OBL_PACK=l0
        PASS_REGULAR_EXPRESSION "obl-sim: end ticks=[0-9]+ [^
]* mismatches=0 [^
]*[
]+obl-sim: PASS"
        FAIL_REGULAR_EXPRESSION "FAILED|MISMATCH")
endforeach()
# Scenario bots (tests/bots, docs/architecture/bots/index.md): each run file must reach its expected verdict with its own seed, its metrics
# inside tests/bots/<run>.bands.json, through tools/bot_runs.py --run-seed (the other recorded seeds run in CI). Label `bot` (~15-60 s
# each in Debug): runtests.ps1 runs them, `build.ps1 -Test` does not.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME headless_missing_image COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/obl_sim_tests.py --sim $<TARGET_FILE:OpenBeach>
        --game-dir ${OBL_GAME_DIR} --filter headless_missing_image --jobs 1)
    # The shared runner returns 2 when no prepared game directory is available.
    set_tests_properties(headless_missing_image PROPERTIES SKIP_RETURN_CODE 2 TIMEOUT 120 RESOURCE_LOCK obl_game_dir
        PASS_REGULAR_EXPRESSION "1/1 passed")
    # The pack, save and launcher checks run on synthetic inputs; with a prepared run/ they also run on the licensed copy.
    add_test(NAME converter_pack_language COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/tools/test_pack_language.py --converter $<TARGET_FILE:obl-convert> --game-dir ${OBL_GAME_DIR})
    set_tests_properties(converter_pack_language PROPERTIES TIMEOUT 120)
    add_executable(synthetic_old_save ../tools/synthetic_old_save_main.cpp)
    target_include_directories(synthetic_old_save PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(synthetic_old_save PRIVATE obl_convert)
    add_test(NAME converter_old_saves COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/tools/test_convert_old_saves.py --converter $<TARGET_FILE:obl-convert>
        --save-helper $<TARGET_FILE:synthetic_old_save> --game-dir ${OBL_GAME_DIR})
    set_tests_properties(converter_old_saves PROPERTIES TIMEOUT 300)
    add_executable(launcher_old_saves_helper ../tools/launcher_old_saves_main.cpp)
    target_link_libraries(launcher_old_saves_helper PRIVATE obl_old_saves)
    add_test(NAME launcher_old_saves COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/tools/test_convert_old_saves.py --converter $<TARGET_FILE:obl-convert>
        --save-helper $<TARGET_FILE:synthetic_old_save> --game-dir ${OBL_GAME_DIR}
        --launcher-helper $<TARGET_FILE:launcher_old_saves_helper>)
    set_tests_properties(launcher_old_saves PROPERTIES TIMEOUT 300)
    add_test(NAME launcher_startup_old_saves COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/tools/test_launcher_startup_old_saves.py --launcher $<TARGET_FILE:OpenBeach>
        --save-helper $<TARGET_FILE:synthetic_old_save> --game-dir ${OBL_GAME_DIR})
    set_tests_properties(launcher_startup_old_saves PROPERTIES TIMEOUT 300)
    # A headless replay of the licensed copy's scenario 1 (engine data, not a converter input): skipped (77) without run/.
    add_test(NAME headless_sprite_metadata COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/tools/test_sprite_metadata.py --game-dir ${OBL_GAME_DIR}
        --launcher $<TARGET_FILE:OpenBeach>)
    set_tests_properties(headless_sprite_metadata PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240)
endif()
function(obl_bot_test RUN)
    if(NOT Python3_Interpreter_FOUND)
        message(WARNING "no Python 3: ctest headless_bot_${RUN} left out")
        return()
    endif()
    add_test(NAME headless_bot_${RUN} COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/bot_runs.py --host $<TARGET_FILE:OpenBeach>
        --game-dir ${OBL_GAME_DIR} --filter ${RUN} --run-seed --out ${CMAKE_CURRENT_BINARY_DIR}/bots/${RUN}.json)
    # One game per game directory (the instance lock): a second host exits at once, so `ctest -j` runs the bots one by one.
    set_tests_properties(headless_bot_${RUN} PROPERTIES SKIP_RETURN_CODE 77 LABELS bot TIMEOUT 600 RESOURCE_LOCK obl_game_dir ${ARGN})
endfunction()
obl_bot_test(build_tree_s1)
# The loss: waiting at the fastest speed runs into the scenario's deadline (17:00 on day 2), stated by `ShowMenu ScenarioLost`.
obl_bot_test(wait_s1 PASS_REGULAR_EXPRESSION "PASS wait_s1\\.bot [^\r\n]* outcome=lost day=2 hour=17\\.00 ")
# The own L0 pack's runs (tests/bots/l0, docs/build/ci.md): bot_runs.py plays them with OBL_PACK=l0 on the same game directory
# (run/packs/l0/ holds the generated pack). `--filter build_tree` matches only that file: campaign run stems end in _s<n>.
function(obl_l0_bot_test RUN)
    if(NOT Python3_Interpreter_FOUND)
        message(WARNING "no Python 3: ctest headless_bot_l0_${RUN} left out")
        return()
    endif()
    add_test(NAME headless_bot_l0_${RUN} COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/bot_runs.py --host $<TARGET_FILE:OpenBeach>
        --game-dir ${OBL_GAME_DIR} --filter ${RUN} --run-seed --out ${CMAKE_CURRENT_BINARY_DIR}/bots/l0_${RUN}.json)
    set_tests_properties(headless_bot_l0_${RUN} PROPERTIES SKIP_RETURN_CODE 77 LABELS bot TIMEOUT 600 RESOURCE_LOCK obl_game_dir)
endfunction()
obl_l0_bot_test(build_tree)
# Chaos bot (tests/chaos, docs/testing/chaos-bot.md): random valid and invalid commands under hard invariants (the run reaches max-ticks,
# rejected commands keep the world hash, the save round trip at save-at, the recorded replay matches in a fresh process), through
# tools/chaos_runs.py. Label `chaos` (~20 s each in Debug): runtests.ps1 runs them, `build.ps1 -Test` does not.
function(obl_chaos_test RUN)
    if(NOT Python3_Interpreter_FOUND)
        message(WARNING "no Python 3: ctest headless_chaos_${RUN} left out")
        return()
    endif()
    add_test(NAME headless_chaos_${RUN} COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/chaos_runs.py --host $<TARGET_FILE:OpenBeach>
        --game-dir ${OBL_GAME_DIR} --filter ${RUN})
    set_tests_properties(headless_chaos_${RUN} PROPERTIES SKIP_RETURN_CODE 77 LABELS chaos TIMEOUT 600 RESOURCE_LOCK obl_game_dir)
endfunction()
obl_chaos_test(sandbox1)
obl_chaos_test(s1)
obl_chaos_test(sandbox1_save)
obl_chaos_test(s1_save)
obl_chaos_test(s2_save)
obl_chaos_test(s8_tricky)
obl_chaos_test(s1_tricky)
obl_chaos_test(s12_tricky_asleep)
obl_chaos_test(s12_tricky_away)
obl_chaos_test(s1_work_points_save)
obl_chaos_test(s1_beer_counter_save)
obl_chaos_test(s12_queue_points_save)
# A 5 s hang limit instead of 20 s: the reload arms the watchdog and the ~1500 ticks after it take longer, so a host that stopped
# beating per update fails here every time instead of only under load (chaos-bot.md, "Findings").
set_tests_properties(headless_chaos_sandbox1_save PROPERTIES ENVIRONMENT OBL_HANG_MS=5000)
