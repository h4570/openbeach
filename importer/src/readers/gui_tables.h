#pragma once

#include "importer/src/source/pe_file.h"

#include <cstdint>
#include <string>
#include <vector>

namespace obl::convert {

// One row of a list: a text key with its value, or a separator line.
struct GuiListEntry {
    std::string key; // empty for a separator
    int32_t value = -1;

    bool Separator() const { return key.empty(); }
    bool operator==(const GuiListEntry&) const = default;
};

// One line of the credits screen; `line` n shows text key CREDIT_ENTRY_<n> (999: the title line).
struct GuiCreditRow {
    int32_t font = 0;
    int32_t line = 0;
    uint32_t color = 0; // 0x00RRGGBB
    uint32_t isLong = 0; // nonzero: extra spacing after the line
    int32_t kind = 0;

    bool operator==(const GuiCreditRow&) const = default;
};

struct ExeGuiTables {
    // Object browser drop-down lists (our definitions, gui_tables.cpp).
    std::vector<GuiListEntry> guestFilters;
    std::vector<GuiListEntry> buildingFilters;
    std::vector<GuiListEntry> cellTypes;
    std::vector<GuiListEntry> staffFilters;
    std::vector<GuiListEntry> hireTypes;
    std::vector<GuiListEntry> events;
    std::vector<GuiListEntry> headings; // column heading text key per filter id
    std::vector<std::string> rowButtons; // sprite names and tooltip keys of a list row's buttons, in index order
    std::vector<GuiCreditRow> credits;
};

// False with `error` (and `out` cleared) when the credits table is missing or ambiguous; never guesses.
bool FindGuiTables(const PeFile& exe, ExeGuiTables& out, std::string& error);

} // namespace obl::convert
