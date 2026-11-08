# obl_version_resource(<target> <description>): adds the version resource (cmake/version.rc.in) to a Windows executable,
# with the project version from version.txt, so that Properties > Details names the build.
function(obl_version_resource target description)
    if(NOT WIN32)
        return()
    endif()
    set(OBL_RC_NAME "${target}")
    set(OBL_RC_DESCRIPTION "${description}")
    set(rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/version.rc.in" "${rc}" @ONLY)
    target_sources(${target} PRIVATE "${rc}")
endfunction()
