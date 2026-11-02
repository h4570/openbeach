#pragma once

// The converted island maps of the pack (docs/architecture/data-formats/maps-scenarios.md, converter/maps.md): `maps/<id>/terrain.png`
// is the terrain of the simulation grid (ConstructGrid). The engine paths of the grid (`data/Scenario/<n>/<n>grid.dat`) map to these
// files here, so the converter and the loader share the legend and the id rule. Pure logic: no Windows APIs.

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace obl::map_pack {

// The grid word of each terrain index of terrain.png (the legend of converter/maps.md). The converter writes these values and checks
// them when it reads the grid file, so both sides share one table.
inline constexpr uint32_t kTerrainFlags[] = {0x80000001, 0x80000002, 0x40000004, 0x20000008, 0x100, 0x20, 0x80};

// The grid file is 128 x 128 words whatever the scenario says (grid_lifecycle.h kFileSide).
inline constexpr uint32_t kGridSide = 128;

// The map id of an engine grid path: the lowercase name before `grid.dat`, so `data/Scenario/scenario1/scenario1grid.dat` is
// `scenario1` (the converter's id is the BIZ stem; the grid is named after it). "" for any other name.
std::string MapId(std::string_view gridPath);

// The grid words the grid file holds (128 x 128 u32, file order: word x * 128 + y is the cell of column x and row y),
// taken from `maps/<id>/terrain.png` under `packDir` (`<game dir>/packs/base`). False with `error`, naming the file, for a path
// that is not a grid, a missing or undecodable PNG, a size other than 128 x 128 or an index without a legend entry.
bool ReadTerrainWords(const std::filesystem::path& packDir, std::string_view gridPath, std::vector<uint32_t>& words, std::string& error);

// The height map of a map: one byte per sample, row-major, top row first (the converter writes `maps/<id>/height.png` only for a map
// that has an installed height map).
struct HeightSamples {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> samples;
};

// The island images of a map: `maps/<id>/color.png` (4096 x 4096 RGB, 8 bits a channel) and `maps/<id>/depth.png` (4096 x 4096 16-bit
// gray). The engine's MapCache blocks are cut from them (src/game/ui/map_cache.h), so both planes are whole-island arrays.
inline constexpr uint32_t kIslandSide = 4096;
inline constexpr uint32_t kIslandBlockSide = 128;

struct IslandPlanes {
    std::vector<uint16_t> colour; // kIslandSide^2 RGB565 words, row-major, top row first (the converter's expansion is exact)
    std::vector<uint16_t> depth; // kIslandSide^2 depth words in the same order
};

// The map id of an engine `.biz` path: the lowercase stem, so `data/Scenario/Scenario1/scenario1.biz` is `scenario1`. "" for any other
// name.
std::string BizMapId(std::string_view bizPath);

// Decodes the island planes of map `id` from `packDir`. False with `error`, naming the file, for a missing or undecodable PNG, or one
// that is not the expected size and format (Import the game data again).
bool ReadIslandPlanes(const std::filesystem::path& packDir, std::string_view id, IslandPlanes& planes, std::string& error);

// Reads required event placement bounds; invalid or missing bounds fail without a fallback.
bool ReadEventArea(const std::filesystem::path& packDir, std::string_view gridPath, std::array<int, 4>& area, std::string& error);

// Whether the map has a height map, from its metadata: `maps/<id>/map.json` has the `height` key exactly when the converter wrote
// `height.png` (the installed height TGA existed). A map without the key is flat (height scale 0 in ConstructGrid), and a missing
// `height.png` for a declared map is an error at load (ReadHeightSamples), not a flat map. False with `error` for a path that is not a
// grid, or a missing or unreadable `map.json`.
bool HasHeightMap(const std::filesystem::path& packDir, std::string_view gridPath, bool& has, std::string& error);

// Decodes `maps/<id>/height.png` into `samples` (the gray levels are the samples). False with `error`, naming the file, for a path that is
// not a grid, a missing or undecodable PNG.
bool ReadHeightSamples(const std::filesystem::path& packDir, std::string_view gridPath, HeightSamples& samples, std::string& error);

// The sea animation sheet of an engine water path (`data/WaterAnims/WaterAnim<k>.bin`, any case): `maps/water/<k>.png`, 1920 x 512 gray
// (water_anim.h kSheetWidth/kSheetHeight), decoded into `sheet`. False with `error`, naming the file, for a path that is not a water
// animation, a missing or undecodable PNG or one of another size.
bool ReadWaterSheet(const std::filesystem::path& packDir, std::string_view waterPath, std::vector<uint8_t>& sheet, std::string& error);

} // namespace obl::map_pack
