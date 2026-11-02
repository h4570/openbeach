# Stand-alone entry for obl_unit_tests on Linux (docs/testing/unit-tests.md, "Linux"). It is not part of the Win32 build:
# the root CMakeLists.txt and build.ps1 do not know this file. It builds the same pure-logic tests as 32-bit code with
# GCC or Clang, without the game or any Windows API.
#
#   cmake -S tests/unit/linux -B build/linux-gcc -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
#   cmake --build build/linux-gcc && ctest --test-dir build/linux-gcc --output-on-failure
cmake_minimum_required(VERSION 3.28)
project(OpenBeachUnitLinux LANGUAGES C CXX)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
cmake_path(NORMAL_PATH REPO)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# These keep the game hosts' linkage.
option(OBL_UNIT_SDL "Build SDL3 (dummy drivers only) for the sound bank and PNG tests" ON)
set(OBL_GAME_DIR "${REPO}/run" CACHE PATH "Directory with the working copy of the game (\"data.\" cases)")

include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)
include(${REPO}/cmake/zlib_inflate.cmake)

# Map, path and presentation runtime; keep game glue out of standalone units.
if(OBL_UNIT_SDL)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    # Only SDL's WAV/PNG loaders and surfaces are used: no window, audio, input or GPU backend (and no X11/Wayland headers).
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
    # A machine with X11/Wayland headers installed would otherwise build those backends (and need XTEST).
    foreach(backend X11 WAYLAND KMSDRM)
        set(SDL_${backend} OFF CACHE BOOL "" FORCE)
    endforeach()
    foreach(subsystem AUDIO CAMERA GPU HAPTIC HIDAPI JOYSTICK RENDER SENSOR VULKAN OPENGL OPENGLES)
        set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.16
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

# What the Windows build links in from src/core and src/game, minus the Windows-only SHA-256 (BCrypt), which a portable version replaces.
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS ${REPO}/src/core/*.cpp ${REPO}/src/formats/*.cpp ${REPO}/src/game/*/*.cpp)

list(REMOVE_ITEM COMMON_SOURCES
    ${REPO}/src/game/ui/text_renderer.cpp
    ${REPO}/src/game/ui/palette_modifiers_runtime.cpp
    ${REPO}/src/game/messages/message_handler.cpp
    ${REPO}/src/game/ui/image.cpp
    ${REPO}/src/game/ui/rep_security.cpp
    ${REPO}/src/game/ui/playlist_playback.cpp
)

list(REMOVE_ITEM COMMON_SOURCES
    ${REPO}/src/game/ui/blit_runtime.cpp
    ${REPO}/src/game/construction/builder_script_queries.cpp
    ${REPO}/src/game/ui/cursor.cpp
    ${REPO}/src/game/ui/font_construct.cpp
    ${REPO}/src/game/game/game_states.cpp
    ${REPO}/src/game/game/game.cpp
    ${REPO}/src/game/game/game_parameters.cpp
    ${REPO}/src/game/map/grid_areas.cpp
    ${REPO}/src/game/map/grid_lifecycle.cpp
    ${REPO}/src/game/map/grid_screen_rect.cpp
    ${REPO}/src/game/map/grid_class_map.cpp
    ${REPO}/src/game/map/grid_queries.cpp
    ${REPO}/src/game/ui/line_draw_runtime.cpp
    ${REPO}/src/game/ui/map_view_blocks.cpp
    ${REPO}/src/game/ui/map_view_camera.cpp
    ${REPO}/src/game/ui/map_view_effects.cpp
    ${REPO}/src/game/ui/map_view_projection.cpp
    ${REPO}/src/game/ui/map_view_lifecycle.cpp
    ${REPO}/src/game/ui/map_view_math.cpp
    ${REPO}/src/game/ui/map_view_restore.cpp
    ${REPO}/src/game/ui/map_view_culling.cpp
    ${REPO}/src/game/ui/map_view_rebuild.cpp
    ${REPO}/src/game/ui/map_cache.cpp
    ${REPO}/src/game/ui/media_files.cpp
    ${REPO}/src/game/game/save_load.cpp
    ${REPO}/src/game/map/path_route.cpp
    ${REPO}/src/game/map/path_predicates.cpp
    ${REPO}/src/game/map/path_tiles_lifecycle.cpp
    ${REPO}/src/game/ui/playlist_state.cpp
    ${REPO}/src/game/ui/loop_sound_control.cpp
    ${REPO}/src/game/ui/music_backend.cpp
    ${REPO}/src/game/ui/music_player.cpp
    ${REPO}/src/game/ui/sample_mixer.cpp
    ${REPO}/src/game/ui/scripted_sound_runtime.cpp
    ${REPO}/src/game/ui/day_night_present_runtime.cpp
    ${REPO}/src/game/ui/save_slots.cpp
    ${REPO}/src/game/ui/text_table.cpp
    ${REPO}/src/game/map/water_anim_renderer.cpp
)
# Glue that builds into the game library on Windows (src/app/CMakeLists.txt), not into obl_common.
list(FILTER COMMON_SOURCES EXCLUDE REGEX "/src/game/ui/(sprite_anim|sprite_bank|sprite_instance.*)\\.cpp$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX "/src/game/ui/(input_ops|sprite_observer|music_stream|resolutions|text_encoding)\\.cpp$")
# The GUI runtime (widget slots, dialogs, modes, tables): built into the game hosts only.
list(FILTER COMMON_SOURCES EXCLUDE REGEX "/src/game/ui/gui_(actions|containers|controls|tooltip|lists|manager_buttons|manager_dialogs|manager_frame|modes_tools_runtime|radar|script|sort|tables|variable_draw|variable_image_draw|vtables|widgets|widgets_image_bar)\\.cpp$")
# Live-world runtime modules remain linked into the game hosts, not this standalone unit executable.
list(REMOVE_ITEM COMMON_SOURCES
    # Host text and renderer seams (game hosts).
    ${REPO}/src/game/map/path_tiles_rebuild.cpp
    ${REPO}/src/game/objects/building_draw.cpp
    ${REPO}/src/game/objects/building_methods.cpp
    ${REPO}/src/game/objects/building_variable_text.cpp
    ${REPO}/src/game/objects/guest_variable_text.cpp
    ${REPO}/src/game/objects/scenery.cpp
    ${REPO}/src/game/objects/scenery_message.cpp
    ${REPO}/src/game/objects/shark.cpp
    ${REPO}/src/game/objects/staff_variable_text.cpp
    # World, object and manager runtime (game hosts).
    ${REPO}/src/game/ui/gui_trivial_slots.cpp
    ${REPO}/src/game/cleaning/cleaning_manager.cpp
    ${REPO}/src/game/objects/commodity_providers.cpp
    ${REPO}/src/game/feedback/feedback_manager.cpp
    ${REPO}/src/game/ferry/ferry.cpp
    ${REPO}/src/game/repair/repair_manager.cpp
    ${REPO}/src/game/objects/prop.cpp
    ${REPO}/src/game/ui/furniture_runtime.cpp
    ${REPO}/src/game/weather/weather.cpp
    ${REPO}/src/game/weather/weather_draw.cpp
    ${REPO}/src/game/weather/weather_lifecycle.cpp
    ${REPO}/src/game/world/world_create.cpp
    ${REPO}/src/game/world/world_messages.cpp
    ${REPO}/src/game/world/world_objects.cpp
    ${REPO}/src/game/world/world_queries.cpp
    ${REPO}/src/game/world/world_receive.cpp
    ${REPO}/src/game/world/world_lifecycle.cpp
    ${REPO}/src/game/world/world_script_host_game.cpp
    ${REPO}/src/game/world/world_script_machine.cpp
    ${REPO}/src/game/game/settings_store.cpp
    ${REPO}/src/game/game/command_loop.cpp
    ${REPO}/src/game/game/command_executor.cpp
    ${REPO}/src/game/world/world_params_process.cpp
    # Test-harness seams.
    ${REPO}/src/game/objects/guest_thoughts.cpp
    ${REPO}/src/game/world/world_update.cpp
    ${REPO}/src/game/world/world_step.cpp
    ${REPO}/src/game/world/world_view.cpp
    ${REPO}/src/game/game/world_test_state.cpp
    ${REPO}/src/game/world/world_params_queries.cpp
    ${REPO}/src/game/world/world_script_load.cpp
    # GUI runtime built into the game library (src/app/CMakeLists.txt).
    ${REPO}/src/game/ui/gui_canvas_live.cpp
    ${REPO}/src/game/ui/gui_content_runtime.cpp
    ${REPO}/src/game/ui/gui_layouts_runtime.cpp
    ${REPO}/src/game/ui/gui_script_leaves.cpp
    ${REPO}/src/game/ui/gui_script_parse.cpp
    ${REPO}/src/game/ui/gui_script_state.cpp
    ${REPO}/src/game/ui/gui_variable_lifecycle.cpp
    ${REPO}/src/game/ui/gui_widgets_leaves.cpp)
# Guest, staff, world, event and GUI entry points: these build into the game hosts.
list(REMOVE_ITEM COMMON_SOURCES
    ${REPO}/src/game/objects/guest_dispatch.cpp
    ${REPO}/src/game/objects/guest_script.cpp
    ${REPO}/src/game/objects/staff_call.cpp
    ${REPO}/src/game/objects/staff_feedback.cpp
    ${REPO}/src/game/game/startup.cpp
    ${REPO}/src/game/game/startup_tables.cpp
    ${REPO}/src/game/ui/gui_modes_place.cpp
    ${REPO}/src/game/world/world_factory.cpp
    ${REPO}/src/game/world/world_control.cpp
    ${REPO}/src/game/world/world_script.cpp
    ${REPO}/src/game/events/event_manager.cpp)
# Object runtime; these implementations build into the game hosts.
list(REMOVE_ITEM COMMON_SOURCES
    ${REPO}/src/game/objects/guest.cpp
    ${REPO}/src/game/objects/guest_actions.cpp
    ${REPO}/src/game/objects/guest_animation.cpp
    ${REPO}/src/game/objects/guest_appearance.cpp
    ${REPO}/src/game/objects/guest_beach.cpp
    ${REPO}/src/game/objects/guest_conflict.cpp
    ${REPO}/src/game/objects/guest_desire_actions.cpp
    ${REPO}/src/game/objects/guest_draw.cpp
    ${REPO}/src/game/objects/guest_lookups.cpp
    ${REPO}/src/game/objects/guest_lifecycle_driver.cpp
    ${REPO}/src/game/objects/guest_opinion_helpers.cpp
    ${REPO}/src/game/objects/guest_placement.cpp
    ${REPO}/src/game/objects/guest_pool.cpp
    ${REPO}/src/game/objects/guest_search_desire.cpp
    ${REPO}/src/game/objects/guest_selection.cpp
    ${REPO}/src/game/objects/guest_walk_routes.cpp
    ${REPO}/src/game/objects/guest_walk_targets.cpp
    ${REPO}/src/game/objects/person_methods.cpp
    ${REPO}/src/game/objects/lifeguard_fns.cpp
    ${REPO}/src/game/objects/staff_actions.cpp
    ${REPO}/src/game/objects/staff_construct.cpp
    ${REPO}/src/game/objects/staff_direct.cpp
    ${REPO}/src/game/objects/staff_lifecycle.cpp
    ${REPO}/src/game/objects/staff_duties.cpp
    ${REPO}/src/game/objects/staff_roster.cpp
    ${REPO}/src/game/objects/staff_profession_values.cpp
    ${REPO}/src/game/objects/staff_queries.cpp
    ${REPO}/src/game/objects/staff_select_draw.cpp
    ${REPO}/src/game/objects/staff_service.cpp
    ${REPO}/src/game/objects/staff_snooty.cpp
    ${REPO}/src/game/objects/staff_text.cpp
    ${REPO}/src/game/objects/staff_values.cpp
    ${REPO}/src/game/objects/staff_variables.cpp)
# Object and construction runtime, built by the game hosts.
list(REMOVE_ITEM COMMON_SOURCES
    ${REPO}/src/game/objects/entity_accessors.cpp
    ${REPO}/src/game/objects/entity_lifecycle.cpp
    ${REPO}/src/game/objects/building_construct.cpp
    ${REPO}/src/game/objects/building_destruct.cpp
    ${REPO}/src/game/objects/building_events.cpp
    ${REPO}/src/game/objects/building_api.cpp
    ${REPO}/src/game/objects/building_queue.cpp
    ${REPO}/src/game/objects/building_queue_lifecycle.cpp
    ${REPO}/src/game/objects/building_serial.cpp
    ${REPO}/src/game/objects/building_thoughts.cpp
    ${REPO}/src/game/construction/construction_assign.cpp
    ${REPO}/src/game/construction/construction_cost_point.cpp
    ${REPO}/src/game/construction/construction_finish.cpp
    ${REPO}/src/game/construction/construction_footprint.cpp
    ${REPO}/src/game/construction/construction_manager.cpp
    ${REPO}/src/game/construction/construction_reallocate.cpp
    ${REPO}/src/game/construction/construction_start.cpp
    ${REPO}/src/game/construction/construction_workpoints.cpp
    ${REPO}/src/game/objects/mover.cpp
    ${REPO}/src/game/objects/mover_lifecycle.cpp
    ${REPO}/src/game/objects/mover_receive.cpp
    ${REPO}/src/game/objects/mover_visibility.cpp
    ${REPO}/src/game/objects/object_registry_api.cpp
    ${REPO}/src/game/objects/object_manager_lifecycle.cpp
    ${REPO}/src/game/objects/script_program_run.cpp
    ${REPO}/src/game/objects/script_program_load.cpp
    ${REPO}/src/game/objects/object_virtuals.cpp
    ${REPO}/src/game/objects/performer_lifecycle.cpp
    ${REPO}/src/game/objects/performer_methods.cpp
    ${REPO}/src/game/objects/performer_virtuals.cpp
    ${REPO}/src/game/objects/scenery_draw.cpp
    ${REPO}/src/game/objects/scenery_lifecycle.cpp
    ${REPO}/src/game/objects/scenery_serial.cpp
    ${REPO}/src/game/objects/scenery_state.cpp
    ${REPO}/src/game/objects/vehicle.cpp
    ${REPO}/src/game/objects/vehicle_lifecycle.cpp
    ${REPO}/src/game/objects/vehicle_messages.cpp
    ${REPO}/src/game/objects/vehicle_presentation.cpp)
# The portable renderer helpers live in src/platform but build into obl_common on Windows.
list(APPEND COMMON_SOURCES ${REPO}/src/game/ui/renderer_ops.cpp ${REPO}/src/platform/renderer_jpeg.cpp)
# The converter library (portable by design); its CLI entry point is not part of the tests.
file(GLOB_RECURSE CONVERT_SOURCES CONFIGURE_DEPENDS ${REPO}/importer/src/*.cpp)
# The browser adapter's portable part; its Emscripten entry (worker_main.cpp) builds in importer/web/CMakeLists.txt.
list(APPEND CONVERT_SOURCES ${REPO}/importer/web/iso_source.cpp ${REPO}/importer/web/picked_files_source.cpp
    ${REPO}/importer/web/storage_sink.cpp)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${REPO}/tests/unit/*_test.cpp)

# Windows-only by nature: DirectInput scancodes against SDL's Windows table (docs/testing/unit-tests.md lists the exclusions).
set(EXCLUDED_TESTS ${REPO}/tests/unit/input_mapping_test.cpp)
set(HOST_SOURCES
    ${REPO}/src/testing/png.cpp)
if(NOT OBL_UNIT_SDL)
    list(REMOVE_ITEM COMMON_SOURCES ${REPO}/src/game/ui/sound_bank.cpp)
    # ScriptedSound exercises the SoundBank as well; excluding only its direct tests leaves unresolved SoundBank symbols.
    list(APPEND EXCLUDED_TESTS ${REPO}/tests/unit/sound_bank_test.cpp ${REPO}/tests/unit/scripted_sound_test.cpp
        ${REPO}/tests/unit/image_compare_test.cpp ${REPO}/tests/unit/convert_sound_test.cpp)
endif()
list(REMOVE_ITEM TEST_SOURCES ${EXCLUDED_TESTS})

add_executable(obl_unit_tests
    ${REPO}/tests/unit/main.cpp
    ${REPO}/tests/unit/object_virtuals_seam.cpp
    ${REPO}/tests/unit/prop_virtuals_seam.cpp
    ${REPO}/tests/unit/staff_virtuals_seam.cpp
    ${REPO}/src/game/objects/vehicle.cpp
    ${TEST_SOURCES}
    ${COMMON_SOURCES}
    ${CONVERT_SOURCES}
    ${HOST_SOURCES})
target_include_directories(obl_unit_tests PRIVATE ${REPO}/tests/unit ${REPO}/src ${REPO} ${REPO}/importer/include)
target_link_libraries(obl_unit_tests PRIVATE nlohmann_json::nlohmann_json obl_zlib)
include(${REPO}/cmake/embedded_schemas.cmake)
obl_embedded_schemas(obl_unit_tests)
if(OBL_UNIT_SDL)
    target_link_libraries(obl_unit_tests PRIVATE SDL3::SDL3-static)
endif()
option(OBL_UNIT_RELOCATABLE "Run the unit executable from any checkout root (CI artifact reuse)" OFF)
if(OBL_UNIT_RELOCATABLE)
    target_compile_definitions(obl_unit_tests PRIVATE OBL_GAME_DIR="run" OBL_REPO_DIR=".")
else()
    target_compile_definitions(obl_unit_tests PRIVATE OBL_GAME_DIR="${OBL_GAME_DIR}" OBL_REPO_DIR="${REPO}")
endif()

# MSVC spellings of the calling conventions that appear in game-facing declarations (tests).
target_compile_options(obl_unit_tests PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/compat.h)

# The game does scalar AVX2 float math (/arch:AVX2 /fp:precise) and the tests pin values that depend on it; no fused
# multiply-add or fast-math rewrites.
target_compile_options(obl_unit_tests PRIVATE -ffp-contract=off -fno-fast-math
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-invalid-offsetof)
if(EMSCRIPTEN)
    # WebAssembly (docs/architecture/web-build.md): wasm float math is IEEE scalar already and wasm32 aligns
    # double/int64 to 8; Node runs the tests with the host file system (schemas and fixtures in the repository).
    # Emscripten turns a C++ throw into an abort unless exceptions are enabled; nlohmann::json and tests catch them.
    target_compile_options(obl_unit_tests PRIVATE -fwasm-exceptions)
    target_link_options(obl_unit_tests PRIVATE -fwasm-exceptions -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB)
else()
    target_compile_options(obl_unit_tests PRIVATE -march=haswell -mfpmath=sse)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT EMSCRIPTEN)
    # MSVC's long double is a double; no test uses one, and obl-sim builds the same way.
    target_compile_options(obl_unit_tests PRIVATE -mlong-double-64)
endif()

enable_testing()
add_test(NAME unit COMMAND obl_unit_tests -data.)
add_test(NAME unit_data COMMAND obl_unit_tests data.)
# Native WSL build trees are outside the checkout; schema fixtures still live in the repository.
set_tests_properties(unit unit_data PROPERTIES WORKING_DIRECTORY "${REPO}")
set_tests_properties(unit_data PROPERTIES SKIP_RETURN_CODE 77)

include(${REPO}/cmake/gui_content.cmake)
obl_gui_content_schemas(obl_unit_tests)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/catalog_schemas.cmake)
obl_catalog_schemas(obl_unit_tests)
