# zlib's inflater for the converter's InstallShield cabinet reader.
# Included by the root CMakeLists.txt and the Linux unit-test entry.
# Only the inflate sources are built, as target obl_zlib: zlib's own CMakeLists.txt renames zconf.h in the source tree
# and assumes it is the top-level project.
include(FetchContent)
FetchContent_Declare(zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake) # no add_subdirectory: the target is defined below
FetchContent_MakeAvailable(zlib)

add_library(obl_zlib STATIC
    ${zlib_SOURCE_DIR}/adler32.c
    ${zlib_SOURCE_DIR}/crc32.c
    ${zlib_SOURCE_DIR}/inffast.c
    ${zlib_SOURCE_DIR}/inflate.c
    ${zlib_SOURCE_DIR}/inftrees.c
    ${zlib_SOURCE_DIR}/zutil.c)
target_include_directories(obl_zlib SYSTEM PUBLIC ${zlib_SOURCE_DIR})
if(MSVC)
    target_compile_options(obl_zlib PRIVATE /W0)
else()
    target_compile_options(obl_zlib PRIVATE -w)
endif()
