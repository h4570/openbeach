# Pure, portable helpers without game concepts. Included as
# "core/<file>.h"; tools/check_layers.py keeps them free of executable-image layouts, image addresses, Windows headers and upward includes.
add_library(obl_core STATIC
    ascii_case.h
    byte_reader.h
    config.h
    cursor_file.cpp
    cursor_file.h
    wide_path.h
    fatal.cpp
    fatal.h
    watchdog.h
    call_counter.cpp
    call_counter.h
    code_page.cpp
    code_page.h
    code_pages.cpp
    code_pages.h
    det_math.cpp
    det_math.h
    file_io.cpp
    file_io.h
    float_to_int.h
    hash_prime.h
    id3tag.cpp
    id3tag.h
    image_compare.cpp
    image_compare.h
    image_decode.cpp
    image_decode.h
    image_scale.cpp
    image_scale.h
    log.cpp
    log.h
    ini_file.cpp
    ini_file.h
    mp3info.cpp
    mp3info.h
    os.cpp
    os.h
    pack_path.cpp
    sha256.cpp
    sha256.h
    pack_path.h
    png_read.cpp
    png_read.h
    refpack.cpp
    refpack.h
    text16.cpp
    text16.h
    vec2.h
    wide_format.cpp
    wide_format.h)
target_include_directories(obl_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
# The float contract of every project target (src/game/CMakeLists.txt): det_math depends on it.
if(MSVC)
    target_compile_options(obl_core PUBLIC /arch:AVX2 /fp:precise)
endif()

# The schema validator on its own: the converter validates its output with it and links nothing else of obl_common.
add_library(obl_json_schema STATIC json_float.cpp json_float.h json_schema.cpp json_schema.h)
target_include_directories(obl_json_schema PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(obl_json_schema PUBLIC nlohmann_json::nlohmann_json)
