# The 64-bit configure (docs/build/building.md): only the code that no longer depends on the 32-bit EXE layouts,
# so a pointer-size regression there shows up before the whole game moves. Each step of the plan adds targets here.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(OBL_GAME_DIR "${CMAKE_SOURCE_DIR}/run" CACHE PATH "Directory with the working copy of the game")

include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
endif()

include(cmake/zlib_inflate.cmake)

add_subdirectory(src/core)
add_subdirectory(src/formats)

# The game library obl_common and its unit tests build on x64 too; the hosts (src/dll,
# src/launcher) build only on Windows.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
if(UNIX)
    # CI and WSL have no X11/Wayland headers; the Linux job only compiles obl_common, it opens no window.
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
    # A machine with X11/Wayland headers installed would otherwise build those backends (and need XTEST).
    foreach(backend X11 WAYLAND KMSDRM)
        set(SDL_${backend} OFF CACHE BOOL "" FORCE)
    endforeach()
endif()
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3)
add_subdirectory(src/game)
# No post-build conversion: tools/linux_check.ps1 builds this tree in WSL against the Windows checkout's run/, and the
# rewrite of run/packs/base broke the parallel game tests reading the same files (hard links in run-slots/). The Linux
# checks convert into their own copy (tools/obl_sim_linux.sh).
set(OBL_CONVERT_ON_BUILD OFF CACHE BOOL "" FORCE)
add_subdirectory(importer)

enable_testing()
# The unit cases that include only core/, formats/, convert/ and portable game headers; the same files also build into the Win32
# obl_unit_tests.
add_executable(obl_portable_tests
    tests/unit/main.cpp
    tests/unit/unit_test.h
    tests/unit/code_page_test.cpp
    tests/unit/convert_cabinet_test.cpp
    tests/unit/convert_guest_set_test.cpp
    tests/unit/convert_import_test.cpp
    tests/unit/convert_gui_layout_test.cpp
    tests/unit/convert_media_test.cpp
    tests/unit/convert_pack_test.cpp
    tests/unit/convert_resource_test.cpp
    tests/unit/convert_save_body_test.cpp
    tests/unit/convert_save_checks_test.cpp
    tests/unit/convert_save_encode_test.cpp
    tests/unit/convert_save_stream_test.cpp
    tests/unit/convert_save_writer_test.cpp
    tests/unit/convert_script_test.cpp
    tests/unit/convert_byte_source_test.cpp
    tests/unit/convert_source_test.cpp
    tests/unit/convert_sprite_test.cpp
    tests/unit/det_math_test.cpp
    tests/unit/file_io_test.cpp
    tests/unit/float_to_int_test.cpp
    tests/unit/gui_content_test.cpp
    tests/unit/hash_prime_test.cpp
    tests/unit/id3tag_test.cpp
    tests/unit/image_decode_reject_test.cpp
    tests/unit/image_decode_test.cpp
    tests/unit/image_scale_test.cpp
    tests/unit/ini_file_test.cpp
    tests/unit/json_schema_test.cpp
    tests/unit/map_pack_test.cpp
    tests/unit/mp3info_file_test.cpp
    tests/unit/mp3info_test.cpp
    tests/unit/os_test.cpp
    tests/unit/pack_path_test.cpp
    tests/unit/palette_modifiers_test.cpp
    tests/unit/refpack_test.cpp
    tests/unit/script_pack_test.cpp
    tests/unit/vec2_test.cpp
    tests/unit/wide_format_test.cpp
    tests/unit/world_params_add_test.cpp
    tests/unit/world_params_process_test.cpp
    tests/unit/world_params_queries_test.cpp)
target_include_directories(obl_portable_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit)
target_link_libraries(obl_portable_tests PRIVATE obl_convert obl_import_web obl_common)
target_compile_definitions(obl_portable_tests PRIVATE OBL_GAME_DIR="${OBL_GAME_DIR}" OBL_REPO_DIR="${CMAKE_SOURCE_DIR}")
# As in tests/unit: "data." cases read the user's run/ (77 = not prepared) and stay out of the default preset.
add_test(NAME unit_portable COMMAND obl_portable_tests -data.)
add_test(NAME unit_portable_data COMMAND obl_portable_tests data.)
# The schema cases search upwards from the working directory, which a build tree outside the checkout (Linux CI) lacks.
set_tests_properties(unit_portable unit_portable_data PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(unit_portable_data PROPERTIES SKIP_RETURN_CODE 77)
if(WIN32)
    add_subdirectory(tests/unit)
endif()
