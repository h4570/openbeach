# obl-sim for Linux (docs/architecture/linux-host.md). A stand-alone CMake entry like tests/unit/linux: the root
# CMakeLists.txt is the Win32 build and does not know this file. tools/obl_sim_linux.sh configures and builds it.
#
#   cmake -S src/host_linux -B build/linux-sim -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake_minimum_required(VERSION 3.28)
project(OpenBeachSimLinux LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "obl-sim needs Clang: the Windows test tools use __try (docs/architecture/linux-host.md)")
endif()

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
cmake_path(NORMAL_PATH REPO)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF) # no C++ modules; the scan would only fail early on the same errors as the compile
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# CI recompiles most of the host when shared layout headers change. -O1 keeps the
# simulation optimized without the compile cost of -O2; ordinary builds retain -O2.
option(OBL_SIM_FAST_BUILD "Use cheaper optimization, line-table debug info and PCH for CI" OFF)
if(OBL_SIM_FAST_BUILD)
    set(CMAKE_C_FLAGS_DEBUG "-gline-tables-only")
    set(CMAKE_CXX_FLAGS_DEBUG "-gline-tables-only")
    set(OBL_SIM_OPTIMIZATION -O1)
    # SDL also uses PCH. Stable PCH bytes let ccache reuse objects after a fresh configure.
    add_compile_options(-Xclang -fno-pch-timestamp)
else()
    set(OBL_SIM_OPTIMIZATION -O2)
endif()

include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)
include(${REPO}/cmake/zlib_inflate.cmake)
# The runtime loads converted GUI tables even in headless scripts that exercise GUI containers.
# Build the portable converter without the simulation's MSVC ABI compatibility flags.
file(GLOB_RECURSE CONVERT_SOURCES CONFIGURE_DEPENDS ${REPO}/importer/src/*.cpp)
include(${REPO}/importer/portable_sources.cmake)
add_executable(obl-convert ${OBL_IMPORTER_SOURCES} ${REPO}/importer/cli/main.cpp)
target_include_directories(obl-convert PRIVATE ${REPO}/src ${REPO} ${REPO}/importer/include)
# The Debug build type is -O0: converting the sprites (584k frames) took 13 min of the 16 min "Build obl-sim" CI step.
target_compile_options(obl-convert PRIVATE -O2)
target_link_libraries(obl-convert PRIVATE nlohmann_json::nlohmann_json obl_zlib)
include(${REPO}/cmake/embedded_schemas.cmake)
obl_embedded_schemas(obl-convert)
# Helpers of the old-save tests (tests/tools/test_convert_old_saves.py, test_launcher_startup_old_saves.py): a synthetic
# old save and the launcher's conversion step on its own.
add_executable(synthetic_old_save ${REPO}/tests/tools/synthetic_old_save_main.cpp)
target_include_directories(synthetic_old_save PRIVATE ${REPO} ${REPO}/tests/unit ${REPO}/importer/include)
add_executable(launcher_old_saves_helper ${REPO}/tests/tools/launcher_old_saves_main.cpp ${REPO}/src/launcher/old_saves.cpp)
target_include_directories(launcher_old_saves_helper PRIVATE ${REPO}/src/launcher)
FetchContent_Declare(pl_mpeg
    URL https://raw.githubusercontent.com/phoboslab/pl_mpeg/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023/pl_mpeg.h
    URL_HASH SHA256=3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(pl_mpeg)
FetchContent_Declare(dr_mp3
    URL https://raw.githubusercontent.com/mackron/dr_libs/dfe8377631000664666519fdb83da193fd8037f4/dr_mp3.h
    URL_HASH SHA256=997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(dr_mp3)

# OBL_LINUX_WINDOWED adds the windowed game OpenBeach (docs/architecture/portable-host.md): SDL then builds its video,
# renderer and audio drivers (X11/Wayland, ALSA/PulseAudio/PipeWire as found, all loaded at run time). Without it SDL has
# dummy drivers only, as in the Linux unit tests: headless never initializes a subsystem.
option(OBL_LINUX_WINDOWED "Also build the windowed OpenBeach (SDL video, renderer and audio)" OFF)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
# FORCE sets every switch in both branches, so a tree reconfigured with the other mode does not keep stale values.
if(OBL_LINUX_WINDOWED AND EMSCRIPTEN)
    # The browser build (docs/architecture/browser-build.md): SDL's Emscripten backend (canvas video, Web Audio) needs no
    # system driver switches; only the unused subsystems are left out.
    set(SDL_UNIX_CONSOLE_BUILD OFF CACHE BOOL "" FORCE)
    foreach(enabled AUDIO RENDER)
        set(SDL_${enabled} ON CACHE BOOL "" FORCE)
    endforeach()
    foreach(disabled CAMERA GPU HAPTIC HIDAPI JOYSTICK SENSOR VULKAN)
        set(SDL_${disabled} OFF CACHE BOOL "" FORCE)
    endforeach()
elseif(OBL_LINUX_WINDOWED)
    set(SDL_UNIX_CONSOLE_BUILD OFF CACHE BOOL "" FORCE)
    foreach(enabled X11 WAYLAND AUDIO RENDER OPENGL OPENGLES)
        set(SDL_${enabled} ON CACHE BOOL "" FORCE)
    endforeach()
    # XTest (synthetic X input) is unused and would add libxtst-dev to the build dependencies.
    foreach(disabled X11_XTEST KMSDRM CAMERA GPU HAPTIC HIDAPI JOYSTICK SENSOR VULKAN)
        set(SDL_${disabled} OFF CACHE BOOL "" FORCE)
    endforeach()
else()
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
    # A machine with X11/Wayland headers installed would otherwise build those backends (and need XTEST).
    foreach(disabled X11 WAYLAND KMSDRM AUDIO CAMERA GPU HAPTIC HIDAPI JOYSTICK RENDER SENSOR VULKAN OPENGL OPENGLES)
        set(SDL_${disabled} OFF CACHE BOOL "" FORCE)
    endforeach()
endif()
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3)

file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS ${REPO}/src/core/*.cpp ${REPO}/src/formats/*.cpp ${REPO}/src/game/*/*.cpp)
file(GLOB_RECURSE HOST_SOURCES CONFIGURE_DEPENDS ${REPO}/src/app/*.cpp ${REPO}/src/testing/*.cpp ${REPO}/src/app/*.c ${REPO}/src/platform/*.cpp ${REPO}/src/platform/*.c)

# Sources outside obl-sim, one per line. The windowed modules (window, presentation, input, intro movie, frame limiter,
# start-up, the presenter test tool): a headless run never reaches them, so InitGame skips their init (OBL_WINDOWS_ONLY), their other
# entries are stubs (headless_modules.cpp) and their game functions abort. The windowed OpenBeach builds them.
set(WINDOWED_MODULES
    ${REPO}/src/platform/cursor.cpp
    ${REPO}/src/platform/frame_limiter.cpp
    ${REPO}/src/platform/hidden_window_sdl.cpp
    ${REPO}/src/platform/input.cpp
    ${REPO}/src/platform/input_mapping.cpp
    ${REPO}/src/platform/intro_movie.cpp
    ${REPO}/src/platform/platform_main.cpp
    ${REPO}/src/platform/sdl_present.cpp
    ${REPO}/src/platform/test_tools/presenter.cpp
)
# Windows only: the Win32 hidden mode and the test tools built on Windows debugging and window APIs (their commands are
# test_tool_stubs.cpp here).
set(WINDOWS_ONLY
    ${REPO}/src/platform/hidden_window.cpp
    ${REPO}/src/platform/test_tools/crash.cpp
    ${REPO}/src/platform/test_tools/cursor.cpp
    ${REPO}/src/platform/test_tools/expected_exit.cpp
    ${REPO}/src/platform/test_tools/profile.cpp
    ${REPO}/src/platform/test_tools/windowed_display.cpp
)
list(REMOVE_ITEM COMMON_SOURCES ${WINDOWS_ONLY})
list(REMOVE_ITEM HOST_SOURCES ${WINDOWS_ONLY})
set(GAME_SOURCES ${COMMON_SOURCES} ${HOST_SOURCES})
list(REMOVE_ITEM COMMON_SOURCES ${WINDOWED_MODULES})
list(REMOVE_ITEM HOST_SOURCES ${WINDOWED_MODULES})

# The game without its host and windowed modules, compiled once for obl-sim and the windowed OpenBeach.
# InitGame (game_start.cpp) compiles per executable: its init chain depends on the host (OBL_WINDOWS_ONLY).
set(INIT_SOURCE ${REPO}/src/app/game_start.cpp)
list(REMOVE_ITEM HOST_SOURCES ${INIT_SOURCE})
add_library(obl-game OBJECT ${COMMON_SOURCES} ${HOST_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/test_tool_stubs.cpp
    ${CONVERT_SOURCES})
add_executable(obl-sim ${INIT_SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/headless_input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/headless_modules.cpp)
set(GAME_TARGETS obl-game obl-sim)
if(OBL_LINUX_WINDOWED)
    if(EMSCRIPTEN)
        # The browser tab is the only process: no host process and no old-save conversion (browser_main.cpp).
        set(LAUNCHER_SOURCES ${REPO}/src/launcher/browser_main.cpp)
    else()
        set(LAUNCHER_SOURCES ${REPO}/src/launcher/remaster_main.cpp ${REPO}/src/launcher/standalone_host.cpp
            ${REPO}/src/launcher/old_saves.cpp)
    endif()
    add_executable(OpenBeach ${INIT_SOURCE} ${WINDOWED_MODULES} ${LAUNCHER_SOURCES} ${REPO}/src/launcher/launch_options.cpp)
    target_include_directories(OpenBeach PRIVATE ${REPO}/src/launcher)
    # The development default of --game-dir, as in the Windows build.
    target_compile_definitions(OpenBeach PRIVATE "OBL_DEFAULT_GAME_DIR=\"${REPO}/run\"")
    list(APPEND GAME_TARGETS OpenBeach)
endif()

include(${REPO}/cmake/gui_content.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/catalog_schemas.cmake)
include(${REPO}/cmake/embedded_icon.cmake)
foreach(target ${GAME_TARGETS})
    target_include_directories(${target} PRIVATE ${REPO}/src ${REPO} ${REPO}/importer/include ${REPO}/tests/unit/linux
        ${pl_mpeg_SOURCE_DIR} ${dr_mp3_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/win_types)
    target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json SDL3::SDL3-static obl_zlib)
    # The convert-save test command calls the converter in process (native saves do not, obl_formats serves them): its
    # portable sources compile into the game with the simulation's flags, since they share json_schema and nlohmann types.
    obl_embedded_schemas(${target})
    obl_embedded_icon(${target})
    obl_gui_content_schemas(${target})
    obl_catalog_schemas(${target})
    target_compile_definitions(${target} PRIVATE NOMINMAX OBL_LINUX_HOST=1)
    target_compile_options(${target} PRIVATE
        -include ${CMAKE_CURRENT_SOURCE_DIR}/msvc_compat.h
        -fms-extensions -fasm-blocks
        # AVX2 float math like the Windows /arch:AVX2 build (docs/build/ci.md). No
        # contraction, so no FMA changes a result.
        -ffp-contract=off -fno-fast-math ${OBL_SIM_OPTIMIZATION}
        -Wno-unknown-pragmas -Wno-microsoft -Wno-invalid-offsetof -Wno-pragma-pack)
    if(EMSCRIPTEN)
        # WebAssembly (docs/architecture/web-build.md): no x86 flags; C++ exceptions on (a throw aborts otherwise);
        # Node reads the pack and the scripts through the host file system.
        target_compile_options(${target} PRIVATE -fwasm-exceptions)
        target_link_options(${target} PRIVATE -fwasm-exceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB)
        if(target STREQUAL "OpenBeach")
            # The browser build (docs/architecture/browser-build.md): a page in a browser, files in an in-memory file
            # system filled from browser storage before the game starts.
            target_link_options(${target} PRIVATE -sENVIRONMENT=web -sFORCE_FILESYSTEM=1
                "SHELL:--pre-js ${REPO}/src/launcher/browser_files.js")
            set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${REPO}/src/launcher/browser_files.js)
            # The site's page beside OpenBeach.js (click to start, canvas scaling, "game ended"); its importer
            # files come with tools/web_site.sh, which assembles the whole site.
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${REPO}/web/index.html ${REPO}/web/site.js $<TARGET_FILE_DIR:${target}>)
        elseif(NOT target STREQUAL "obl-game")
            target_link_options(${target} PRIVATE -sNODERAWFS=1)
        endif()
    else()
        target_compile_options(${target} PRIVATE -march=haswell -mfpmath=sse -mlong-double-64)
        # Non-PIE keeps the executable at a fixed address; -rdynamic lets the crash report name our
        # functions through dladdr (fatal.cpp).
        target_link_options(${target} PRIVATE -no-pie -rdynamic)
    endif()
    if(NOT target STREQUAL "obl-game")
        target_link_libraries(${target} PRIVATE obl-game)
    endif()
endforeach()
# The windowed modules of the windowed game: InitGame runs their init (OBL_WINDOWS_ONLY, platform/windows_only.h).
# Only this executable's own sources see the definition; the shared objects keep the headless chain.
if(OBL_LINUX_WINDOWED)
    target_compile_definitions(OpenBeach PRIVATE OBL_WINDOWED_HOST=1)
endif()
set_source_files_properties(${REPO}/src/platform/pl_mpeg_impl.c ${REPO}/src/platform/dr_mp3_impl.c PROPERTIES
    COMPILE_OPTIONS "-w")
obl_gui_content_schemas(obl-convert)

if(OBL_SIM_FAST_BUILD)
    # Only stable library headers: changing a game layout must not invalidate the PCH.
    # The two targets need separate PCHs because the host has MSVC compatibility flags.
    foreach(target obl-game obl-convert)
        target_precompile_headers(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:<nlohmann/json.hpp$<ANGLE-R>>")
    endforeach()
endif()
