# The browser import (docs/architecture/data-formats/converter/index.md, "Browser adapter"): obl-import-web.js/.wasm,
# loaded by import_worker.js inside a Web Worker. A stand-alone Emscripten entry like tests/unit/linux; the CI wasm job
# and tools/wasm_linux.sh build it.
#
#   emcmake cmake -S importer/web -B build/wasm/importer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.28)
project(OpenBeachImportWeb LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "importer/web builds with Emscripten only (emcmake cmake ...)")
endif()

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
cmake_path(NORMAL_PATH REPO)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)
include(${REPO}/cmake/zlib_inflate.cmake)
include(${REPO}/importer/portable_sources.cmake)

add_executable(obl-import-web ${OBL_IMPORTER_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/iso_source.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/picked_files_source.cpp ${CMAKE_CURRENT_SOURCE_DIR}/storage_sink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/worker_main.cpp)
target_include_directories(obl-import-web PRIVATE ${REPO}/src ${REPO} ${REPO}/importer/include)
target_link_libraries(obl-import-web PRIVATE nlohmann_json::nlohmann_json obl_zlib)
include(${REPO}/cmake/embedded_schemas.cmake)
obl_embedded_schemas(obl-import-web)
include(${REPO}/cmake/gui_content.cmake)
obl_gui_content_schemas(obl-import-web)
# The converters' float contract (as obl-convert): converted packs keep their bytes.
target_compile_options(obl-import-web PRIVATE -O2 -ffp-contract=off -fno-fast-math -fwasm-exceptions)
# A CD's sprite archive is spooled into memory (worker_main.cpp), so the heap may grow to wasm32's limit.
target_link_options(obl-import-web PRIVATE -fwasm-exceptions -sMODULARIZE=1 -sEXPORT_NAME=createOblImport
    -sENVIRONMENT=worker,node -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=1MB
    -sEXPORTED_FUNCTIONS=_obl_import,_obl_import_error,_obl_import_language,_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString
    "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$UTF8ToString,$stringToNewUTF8")
add_custom_command(TARGET obl-import-web POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/import_worker.js ${CMAKE_BINARY_DIR}/bin/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/language_ini.js ${CMAKE_BINARY_DIR}/bin/
    VERBATIM)
