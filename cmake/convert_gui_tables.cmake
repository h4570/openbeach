# Post-build step of obl-convert: (re)writes the converted data the game reads from the game directory, so run/
# directories prepared before a loader moved to converted data keep starting. The import does the same on a fresh run/
# (docs/build/game-data.md "Converted pack"):
#   packs/base/gui/*.json from its BeachLife.exe; packs/base/gui/layouts.json and gui/*.gui from its Data/Script/GUI;
#   packs/base/rules/game_parameters.json, scripts/, maps/, sounds/, sprites/, catalog/, guests/, text/ and scenarios/ from its
#   installation (catalog/, guests/, text/), then packs/base/pack.json last (it hashes the files before it).
#   -DCONVERT=<built obl-convert> -DGAME_DIR=<run/>
# Never fails the build: without a prepared run/ there is nothing to convert.
if(NOT EXISTS "${GAME_DIR}/BeachLife.exe")
    return()
endif()
execute_process(COMMAND "${CONVERT}" gui-tables "${GAME_DIR}/BeachLife.exe" "${GAME_DIR}/packs/base"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(WARNING "obl-convert gui-tables failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CONVERT}" game-parameters "${GAME_DIR}" "${GAME_DIR}/packs/base"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(WARNING "obl-convert game-parameters failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CONVERT}" scripts "${GAME_DIR}" "${GAME_DIR}/packs/base"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(WARNING "obl-convert scripts failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CONVERT}" maps "${GAME_DIR}" "${GAME_DIR}/packs/base"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(WARNING "obl-convert maps failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CONVERT}" sounds "${GAME_DIR}" "${GAME_DIR}/packs/base"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(WARNING "obl-convert sounds failed (${result}): ${error}")
endif()
execute_process(COMMAND "${CONVERT}" gui-layouts "${GAME_DIR}" "${GAME_DIR}/packs/base"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(WARNING "obl-convert gui-layouts failed (${result}): ${error}")
endif()
# Minutes on a fresh run/, nothing when sprites/conversion.stamp matches the installation's archive.
execute_process(COMMAND "${CONVERT}" sprites "${GAME_DIR}" "${GAME_DIR}/packs/base"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(WARNING "obl-convert sprites failed (${result}): ${error}")
endif()
# The soundtrack and the intro movie are byte copies of the installation's files (media_pack.h); the engine stops without them.
execute_process(COMMAND "${CONVERT}" media "${GAME_DIR}" "${GAME_DIR}/packs/base"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(WARNING "obl-convert media failed (${result}): ${error}")
endif()
# The class icon and cursors go beside remaster/ data, not into the pack (the game loads them with LoadImageW from files).
execute_process(COMMAND "${CONVERT}" resources "${GAME_DIR}" "${GAME_DIR}/remaster"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(WARNING "obl-convert resources failed (${result}): ${error}")
endif()
foreach(step IN ITEMS fonts images catalog guests texts scenarios pack)
    execute_process(COMMAND "${CONVERT}" ${step} "${GAME_DIR}" "${GAME_DIR}/packs/base"
        RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT result STREQUAL "0")
        message(WARNING "obl-convert ${step} failed (${result}): ${error}")
    endif()
endforeach()
