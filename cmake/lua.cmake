# Lua 5.4 for the presentation scripts of mods (docs/modding/reference/scripting.md, "Interpreter").
# Included by the root CMakeLists.txt, cmake/portable_only.cmake, src/host_linux and tests/unit/linux, like zlib_inflate.cmake.
# Target obl_lua: the core and only the libraries the sandbox opens. io, os, package (require, dlopen) and debug are not
# built at all, and neither is linit.c, which would reference them; the runtime opens base, coroutine, math, string, table
# and utf8 itself.
# The C sources compile as C++: a Lua error is then a C++ exception instead of a longjmp, so it unwinds the engine's C++
# frames between the interpreter and a script (their destructors run), and the WebAssembly build keeps one exception model.
include(FetchContent)
FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
    URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake) # Lua has no CMakeLists.txt; the target is defined below
FetchContent_MakeAvailable(lua)

if(NOT TARGET obl_lua)
    set(OBL_LUA_SOURCES)
    foreach(name lapi lauxlib lbaselib lcode lcorolib lctype ldebug ldo ldump lfunc lgc llex lmathlib lmem lobject lopcodes
            lparser lstate lstring lstrlib ltable ltablib ltm lundump lutf8lib lvm lzio)
        list(APPEND OBL_LUA_SOURCES ${lua_SOURCE_DIR}/src/${name}.c)
    endforeach()
    set_source_files_properties(${OBL_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
    add_library(obl_lua STATIC ${OBL_LUA_SOURCES})
    target_include_directories(obl_lua SYSTEM PUBLIC ${lua_SOURCE_DIR}/src)
    set_target_properties(obl_lua PROPERTIES LINKER_LANGUAGE CXX CXX_STANDARD 20)
    if(MSVC)
        # /TP: compile as C++ also where the generator ignores LANGUAGE for a .c file; /EHsc: the error exceptions.
        target_compile_options(obl_lua PRIVATE /TP /EHsc /W0)
    else()
        target_compile_options(obl_lua PRIVATE -x c++ -w)
        if(EMSCRIPTEN)
            target_compile_options(obl_lua PRIVATE -fwasm-exceptions)
        endif()
    endif()
endif()
