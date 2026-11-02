# Command-line PNG comparison with the golden-shot metric (docs/testing/golden-images.md).
add_executable(obl_imgdiff main.cpp)
target_link_libraries(obl_imgdiff PRIVATE obl_common SDL3::SDL3-static)
