cmake_minimum_required(VERSION 3.28)
file(STRINGS version.txt OBL_VERSION LIMIT_COUNT 1)
project(OpenBeach VERSION ${OBL_VERSION} LANGUAGES C CXX)
# A release bump of version.txt reconfigures, so the executables' version resources follow it.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS version.txt)

# The game builds as x64 Windows (docs/build/building.md). A 64-bit configure off Windows builds only the portable
# part (cmake/portable_only.cmake); the Linux obl-sim and unit builds have their own CMakeLists (src/host_linux, tests/unit/linux).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "OpenBeach builds for x64 Windows or, portable part only, x64 Linux. Use a preset from CMakePresets.json.")
elseif(NOT WIN32)
    set(OBL_PORTABLE_ONLY ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(OBL_GAME_DIR "${CMAKE_SOURCE_DIR}/run" CACHE PATH "Directory with the working copy of the game")

if(OBL_PORTABLE_ONLY)
    include(cmake/portable_only.cmake)
    return()
endif()

include(FetchContent)
# JSON for data files generated from the user's installation (run/remaster/*.json).
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# Platform layer (window presentation now, input/audio later). Static, so the game stays self-contained.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3)

# Intro movie decoder (MPEG-1 video + MP2 audio), a single header pinned to a commit.
FetchContent_Declare(pl_mpeg
    URL https://raw.githubusercontent.com/phoboslab/pl_mpeg/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023/pl_mpeg.h
    URL_HASH SHA256=3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(pl_mpeg)

# MP3 music decoder, pinned single header; no game assets are part of the build.
FetchContent_Declare(dr_mp3
    URL https://raw.githubusercontent.com/mackron/dr_libs/dfe8377631000664666519fdb83da193fd8037f4/dr_mp3.h
    URL_HASH SHA256=997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(dr_mp3)

include(cmake/zlib_inflate.cmake)

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
endif()

add_subdirectory(src/core)
add_subdirectory(src/formats)
add_subdirectory(src/game)
add_subdirectory(importer)
add_subdirectory(src/app)
add_subdirectory(src/imgdiff)
add_subdirectory(src/launcher)

enable_testing()
add_subdirectory(tests/unit)
