add_library(obl_launch_options STATIC launch_options.cpp launch_options.h)
add_library(obl_old_saves STATIC old_saves.cpp old_saves.h)
target_include_directories(obl_old_saves PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(obl_launch_options PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The game (docs/architecture/portable-host.md): the same file is the launcher and the host process; the game's objects
# (src/app) link in statically.
# The program icon of the executable file (the window's comes from the same .ico, src/platform/sdl_present.cpp).
set(OBL_ICON_PATH "${PROJECT_SOURCE_DIR}/assets/logo/openbeach.ico")
configure_file(openbeach.rc.in "${CMAKE_CURRENT_BINARY_DIR}/openbeach.rc" @ONLY)
add_executable(OpenBeach remaster_main.cpp settings_window.cpp settings_window.h standalone_host.cpp standalone_host.h
    "${CMAKE_CURRENT_BINARY_DIR}/openbeach.rc")
target_link_libraries(OpenBeach PRIVATE openbeach obl_launch_options obl_old_saves)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/version_resource.cmake)
obl_version_resource(OpenBeach "OpenBeach, the game")
target_include_directories(OpenBeach PRIVATE ${PROJECT_SOURCE_DIR}/src)
# The settings window shows the same icon as the game window.
include(${PROJECT_SOURCE_DIR}/cmake/embedded_icon.cmake)
obl_embedded_icon(OpenBeach)
target_compile_definitions(OpenBeach PRIVATE "OBL_DEFAULT_GAME_DIR=\"${OBL_GAME_DIR}\"")
