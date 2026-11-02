#pragma once

#include <nlohmann/json.hpp>

#include <string>

// The `map` section of an obl.save read into the live grid (docs/architecture/data-formats/save-switch.md): the
// inverse of WriteMapJson, with the records the grid load reads (map/grid_save.h). The `Path` block is not part of it.
namespace obl::map {
struct Grid;
} // namespace obl::map

namespace obl::world_save {

// Fills the island size (missing: 0, the bridge's default), the open state of every beach then sea area and the flags and
// height offset of every cell. The grid keeps its size and area lists (built from the map), so the section must name the same
// cell count and one open flag per area; false with `error` naming the key otherwise. The grid is unchanged on a refusal.
bool ReadMapJson(const nlohmann::json& in, map::Grid& grid, std::string& error);

// ReadMapJson's checks without a grid: a square cell count with one flag and height per cell and a list of open flags. The
// live grid's size and area count are checked when the load reads the section.
bool CheckMapJson(const nlohmann::json& in, std::string& error);

} // namespace obl::world_save
