#include "game/world/map_load_json.h"

#include "game/map/grid_state.h"
#include "game/world/save_json_values.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace obl::world_save {
namespace {

using nlohmann::json;

// The section's values, checked before any of them reaches the grid.
struct MapValues {
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t islandSize = 0;
    std::vector<bool> areasOpen;
    std::vector<uint32_t> flags;
    std::vector<uint16_t> heights;
};

bool Parse(const json& in, MapValues& out, std::string& error)
{
    error.clear();
    if (!in.is_object()) {
        error = "map: not an object";
        return false;
    }
    const auto cells = in.find("cells");
    if (cells == in.end() || !cells->is_array() || cells->size() != 2 || !(*cells)[0].is_number_unsigned() ||
            !(*cells)[1].is_number_unsigned()) {
        error = "map.cells: not a [width, height] pair";
        return false;
    }
    out.width = (*cells)[0].get<uint32_t>();
    out.height = (*cells)[1].get<uint32_t>();
    if (out.width == 0 || out.width != out.height) {
        error = "map.cells: " + std::to_string(out.width) + " x " + std::to_string(out.height) + " is not a square grid";
        return false;
    }
    if (const auto it = in.find("island_size"); it != in.end()) {
        const auto bits = IntegerBits(*it);
        if (!bits || it->is_boolean()) {
            error = "map.island_size: not an integer";
            return false;
        }
        out.islandSize = static_cast<int32_t>(*bits);
    }
    // The grid load reads one record per area of the island's map: the list is required (formats::CheckOblSaveDocument refuses it missing).
    const auto areas = in.find("areas_open");
    if (areas == in.end() || !areas->is_array()) {
        error = "map.areas_open: not a list";
        return false;
    }
    for (const json& open : *areas) {
        if (!open.is_boolean()) {
            error = "map.areas_open: " + open.dump() + " is not true or false";
            return false;
        }
        out.areasOpen.push_back(open.get<bool>());
    }
    const size_t count = static_cast<size_t>(out.width) * out.height;
    for (const char* key : {"flags", "heights"}) {
        const auto list = in.find(key);
        if (list == in.end() || !list->is_array() || list->size() != count) {
            error = std::string("map.") + key + ": not a list of " + std::to_string(count) + " cells";
            return false;
        }
        const bool word = key[0] == 'h';
        const uint64_t limit = word ? std::numeric_limits<uint16_t>::max() : std::numeric_limits<uint32_t>::max();
        for (const json& v : *list) {
            if (!v.is_number_unsigned() || v.get<uint64_t>() > limit) {
                error = std::string("map.") + key + ": " + v.dump() + " is not a " + (word ? "word" : "dword");
                return false;
            }
            if (word)
                out.heights.push_back(static_cast<uint16_t>(v.get<uint64_t>()));
            else
                out.flags.push_back(static_cast<uint32_t>(v.get<uint64_t>()));
        }
    }
    return true;
}

} // namespace

bool CheckMapJson(const json& in, std::string& error)
{
    MapValues values;
    return Parse(in, values, error);
}

bool ReadMapJson(const json& in, map::Grid& grid, std::string& error)
{
    MapValues values;
    if (!Parse(in, values, error)) return false;
    if (values.width != grid.m_width || values.height != grid.m_height || grid.m_cells.size() != grid.NumCells()) {
        error = "map.cells: " + std::to_string(values.width) + " x " + std::to_string(values.height) + ", the island's grid is " +
                std::to_string(grid.m_width) + " x " + std::to_string(grid.m_height);
        return false;
    }
    const size_t areas = grid.m_beachAreas.size() + grid.m_seaAreas.size();
    if (values.areasOpen.size() != areas) {
        error = "map.areas_open: " + std::to_string(values.areasOpen.size()) + " flags for the island's " + std::to_string(areas) +
                " areas";
        return false;
    }
    // The grid load's order (grid_save.h VisitFields): the island size, the beach then sea areas, then the cells.
    grid.m_islandSize = values.islandSize;
    size_t a = 0;
    for (auto* list : {&grid.m_beachAreas, &grid.m_seaAreas})
        for (map::Area& area : *list) area.m_shut = !values.areasOpen[a++];
    for (size_t i = 0; i < grid.m_cells.size(); ++i) {
        grid.m_cells[i].m_flags = values.flags[i];
        grid.m_cells[i].m_zOffset = values.heights[i];
    }
    return true;
}

} // namespace obl::world_save
