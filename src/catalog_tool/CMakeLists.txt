# Command-line signer and verifier for obl.mod_catalog indexes (docs/modding/catalogs.md).
add_executable(obl_catalog_sign main.cpp)
target_link_libraries(obl_catalog_sign PRIVATE obl_common)
