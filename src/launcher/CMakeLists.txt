add_library(obl_launch_options STATIC launch_options.cpp launch_options.h)
add_library(obl_old_saves STATIC old_saves.cpp old_saves.h)
target_include_directories(obl_old_saves PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(obl_launch_options PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The game (docs/architecture/portable-host.md): the same file is the launcher and the host process; the game's objects
# (src/app) link in statically.
add_executable(OpenBeach remaster_main.cpp standalone_host.cpp standalone_host.h)
target_link_libraries(OpenBeach PRIVATE openbeach obl_launch_options obl_old_saves)
target_include_directories(OpenBeach PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(OpenBeach PRIVATE "OBL_DEFAULT_GAME_DIR=\"${OBL_GAME_DIR}\"")
