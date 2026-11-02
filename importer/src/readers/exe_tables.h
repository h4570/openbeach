#pragma once

#include "importer/src/source/pe_file.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace obl::convert {

constexpr int kFootprintColumns = 25;
constexpr int kFootprintRows = 100;
constexpr uint32_t kFootprintBlockSize = 8 + kFootprintColumns * kFootprintRows * 4;
// Every supported build assigns this many grids (one per building key).
constexpr size_t kFootprintCount = 48;

struct ExeFootprint {
    std::string building; // the building key, as BuildingList spells it
    uint32_t address = 0; // virtual address of the grid in this build (diagnostics only)
    int32_t width = 0;
    int32_t height = 0;
    std::vector<int32_t> cells; // kFootprintRows * kFootprintColumns, row-major
};

bool FindFootprints(const PeFile& exe, const std::set<std::string>& buildingKeys, std::vector<ExeFootprint>& out, std::string& error);

} // namespace obl::convert
