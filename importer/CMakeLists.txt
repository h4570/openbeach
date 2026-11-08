# The converter (docs/architecture/data-formats/converter/index.md): pure, portable code that reads the user's copy of the game;
# no Windows API and no game state, so it also builds in the Linux unit-test job.
add_library(obl_convert STATIC
    include/openbeach/importer/byte_source.h
    include/openbeach/importer/import.h
    include/openbeach/importer/pack_sink.h
    include/openbeach/importer/spool.h
    src/import.cpp
    src/source/byte_source.cpp
    src/readers/building_behavior_keys.h
    src/readers/building_list.cpp
    src/readers/building_list.h
    src/readers/exe_tables.cpp
    src/readers/exe_tables.h
    src/readers/gui_tables.cpp
    src/readers/gui_tables.h
    src/readers/pis.cpp
    src/readers/pis.h
    src/readers/save/save_body.cpp
    src/readers/save/save_body.h
    src/readers/save/save_encode.cpp
    src/readers/save/save_encode.h
    src/readers/save/save_stream.cpp
    src/readers/save/save_stream.h
    src/readers/save/save_writer.cpp
    src/readers/save/save_writer.h
    src/readers/script_vocabulary.cpp
    src/readers/script_vocabulary.h
    src/readers/sprite_bank.cpp
    src/readers/sprite_bank.h
    src/readers/world_params_add.h
    src/readers/world_params_process.h
    src/source/cabinet.cpp
    src/source/cabinet.h
    src/source/cd_source.cpp
    src/source/cd_source.h
    src/source/exe_scan.h
    src/source/md5.cpp
    src/source/md5.h
    src/source/pe_file.cpp
    src/source/pe_file.h
    src/source/source.cpp
    src/source/source.h
    src/source/spool.cpp
    src/writers/catalog_writer.cpp
    src/writers/catalog_writer.h
    src/writers/content_key.h
    src/writers/font_convert.cpp
    src/writers/font_convert.h
    src/writers/game_parameters_convert.cpp
    src/writers/game_parameters_convert.h
    src/writers/guest_set_convert.cpp
    src/writers/guest_set_convert.h
    src/writers/gui_layout_convert.cpp
    src/writers/gui_layout_convert.h
    src/writers/gui_writer.cpp
    src/writers/gui_writer.h
    src/writers/image_convert.cpp
    src/writers/image_convert.h
    src/writers/map_convert.cpp
    src/writers/map_convert.h
    src/writers/map_decode.cpp
    src/writers/media_convert.cpp
    src/writers/media_convert.h
    src/writers/pack_manifest.cpp
    src/writers/pack_manifest.h
    src/writers/pack_sink.cpp
    src/writers/png_write.cpp
    src/writers/png_write.h
    src/writers/resource_convert.cpp
    src/writers/resource_convert.h
    src/writers/scenario_convert.cpp
    src/writers/scenario_convert.h
    src/writers/script_convert.cpp
    src/writers/script_convert.h
    src/writers/sound_convert.cpp
    src/writers/sound_convert.h
    src/writers/sound_script_convert.cpp
    src/writers/sound_script_convert.h
    src/writers/sprite_convert.cpp
    src/writers/sprite_convert.h
    src/writers/text_convert.cpp
    src/writers/text_convert.h)
target_include_directories(obl_convert PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Explicit portable dependencies, never obl_common (converter-review/importer-boundary.md); obl_core carries the
# game's float contract, so converted packs keep their bytes.
target_link_libraries(obl_convert PRIVATE obl_zlib PUBLIC obl_core obl_formats obl_json_schema)

# The portable part of the browser adapter (web/): its sources and storage over callbacks, unit-tested on every build;
# the Emscripten entry (web/worker_main.cpp) builds only in web/CMakeLists.txt.
add_library(obl_import_web STATIC web/iso_source.cpp web/picked_files_source.cpp web/storage_sink.cpp web/storage_sink.h web/web_source.h)
target_link_libraries(obl_import_web PUBLIC obl_convert)

add_executable(obl-convert cli/main.cpp)
target_link_libraries(obl-convert PRIVATE obl_convert)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/version_resource.cmake)
obl_version_resource(obl-convert "OpenBeach importer for a copy of Beach Life")

# Only the Windows game build refreshes the game directory; cmake/portable_only.cmake turns this off.
option(OBL_CONVERT_ON_BUILD "Convert the game directory's packs/base after building obl-convert" ON)
if(OBL_GAME_DIR AND OBL_CONVERT_ON_BUILD)
    add_custom_command(TARGET obl-convert POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DCONVERT=$<TARGET_FILE:obl-convert> -DGAME_DIR=${OBL_GAME_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/convert_gui_tables.cmake
        VERBATIM)
endif()
