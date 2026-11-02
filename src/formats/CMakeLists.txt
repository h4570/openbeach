# Our own pack and save contracts, shared by the game and the importer (docs/architecture/data-formats/converter/index.md):
# the schema bundle, the obl.save document checks and the pack codecs both sides read (GUI content, sprites, script and
# map paths). Portable, core/ only; never includes the importer.
add_library(obl_formats STATIC
    cell_state.cpp
    cell_state.h
    gui_content.cpp
    gui_content.h
    map_pack.cpp
    map_pack.h
    palette_modifiers_pack.cpp
    palette_modifiers_pack.h
    save_document.cpp
    save_document.h
    save_records.cpp
    save_records.h
    save_states.cpp
    save_states.h
    schemas.cpp
    schemas.h
    script_pack.cpp
    script_pack.h
    sprite_pack.cpp
    sprite_pack.h)
target_include_directories(obl_formats PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(obl_formats PUBLIC obl_core obl_json_schema nlohmann_json::nlohmann_json)
include(${CMAKE_SOURCE_DIR}/cmake/embedded_schemas.cmake)
obl_embedded_schemas(obl_formats)
include(${CMAKE_SOURCE_DIR}/cmake/gui_content.cmake)
obl_gui_content_schemas(obl_formats)
