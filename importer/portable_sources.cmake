# The importer's library sources and the portable game-side files it needs (obl_core, obl_formats and obl_json_schema
# subsets), for the stand-alone CMake entries that do not build the Windows targets: src/host_linux (obl-convert) and
# importer/web (the browser import). Needs REPO (the checkout root); sets OBL_IMPORTER_SOURCES.
file(GLOB_RECURSE OBL_IMPORTER_SOURCES CONFIGURE_DEPENDS ${REPO}/importer/src/*.cpp)
list(APPEND OBL_IMPORTER_SOURCES
    ${REPO}/src/core/json_schema.cpp ${REPO}/src/core/json_float.cpp ${REPO}/src/core/code_page.cpp
    ${REPO}/src/core/code_pages.cpp ${REPO}/src/core/image_decode.cpp ${REPO}/src/core/png_read.cpp
    ${REPO}/src/core/pack_path.cpp ${REPO}/src/core/os.cpp ${REPO}/src/core/sha256.cpp
    ${REPO}/src/formats/gui_content.cpp ${REPO}/src/formats/script_pack.cpp ${REPO}/src/formats/palette_modifiers_pack.cpp
    ${REPO}/src/formats/sprite_pack.cpp ${REPO}/src/formats/cell_state.cpp ${REPO}/src/formats/save_document.cpp ${REPO}/src/formats/save_records.cpp ${REPO}/src/formats/save_states.cpp ${REPO}/src/formats/schemas.cpp
    ${REPO}/src/formats/map_pack.cpp)
