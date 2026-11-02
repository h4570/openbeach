# Embed the checked-in schemas so conversion and parsing need no installed schema directory.
function(obl_gui_content_schemas target)
    set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    file(READ "${root}/data/schemas/object_browser.schema.json" OBL_BROWSER_SCHEMA)
    file(READ "${root}/data/schemas/credits.schema.json" OBL_CREDITS_SCHEMA)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${root}/data/schemas/object_browser.schema.json" "${root}/data/schemas/credits.schema.json")
    configure_file("${root}/src/formats/gui_content_schemas.h.in" "${CMAKE_CURRENT_BINARY_DIR}/gui_content_schemas.h" @ONLY)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
