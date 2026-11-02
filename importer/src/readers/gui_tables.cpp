#include "importer/src/readers/gui_tables.h"

#include "importer/src/source/exe_scan.h"
#include "formats/gui_content.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace obl::convert {

namespace {

using scan::LoadU32;

// The object browser's lists, column headings and row buttons are our own definitions; only the text keys name entries of
// the user's text tables. A filter is named as in gui_content (its numeric id is the engine adapter's business).
struct FilterRow {
    std::string_view key; // empty: a separator
    std::string_view filter;
};
struct ValueRow {
    std::string_view key;
    int32_t value;
};

constexpr FilterRow kGuestFilters[] = {{"OB_GF_HAPPY", "guest_happiness"}, {"OB_GF_SUNBURN", "guest_sunburn"},
        {"OB_GF_HEADACHE", "guest_headache"}, {"OB_GF_DRUNK", "guest_drunk"}, {"OB_GF_BLADDER", "guest_bladder"},
        {"OB_GF_ILL", "guest_illness"}, {"", ""}, {"OB_GF_PRICE", "guest_price"}, {"OB_GF_SAFETY", "guest_safety"},
        {"OB_GF_QUEUING", "guest_crowding"}, {"OB_GF_BEAUTY", "guest_beauty"}, {"OB_GF_CLEANLINESS", "guest_cleanliness"},
        {"OB_GF_TOTTY", "guest_attractiveness"}, {"OB_GF_VARIETY", "guest_variety"}, {"", ""}, {"OB_GF_CASH", "guest_cash"}, {"", ""},
        {"OB_GF_MALE", "guest_male"}, {"OB_GF_FEMALE", "guest_female"}};
constexpr FilterRow kBuildingFilters[] = {{"OB_BF_ALL", "building_all"}, {"", ""}, {"OB_BF_REPAIR", "building_repair"},
        {"OB_BF_FAULTS", "building_faults"}, {"OB_BF_AGE", "building_age"}, {"OB_BF_POWER", "building_power"}, {"", ""},
        {"OB_BF_INCOME", "building_income"}, {"OB_BF_COSTS", "building_cost"}, {"OB_BF_PROFIT", "building_profit"},
        {"OB_BF_AVPROFIT", "building_average_profit"}, {"", ""}, {"OB_BF_CUSTOMERS_TODAY", "building_visits"}, {"", ""},
        {"OB_BF_OPEN", "building_open"}, {"OB_BF_CLOSED", "building_closed"}};
// Cell masks: the engine's cell-class bits (the signed values keep the top bit).
constexpr ValueRow kCellTypes[] = {{"CELL_LAND_TEXT", INT32_MIN + 1}, {"CELL_SHALLOW_SEA_TEXT", 0x40000004},
        {"CELL_DEEP_SEA_TEXT", 0x20000008}, {"CELL_STRUCTURE_TEXT", 16}, {"CELL_ROCK_TEXT", 32}, {"CELL_PLANT_TEXT", 32832},
        {"CELL_PATH_TEXT", 128}, {"CELL_BEACH_TEXT", INT32_MIN + 2}, {"SEA_EDGE_TEXT", 256}};
constexpr ValueRow kStaffFilters[] = {{"STAFFB_FILTER_ALL", 6}, {"STAFFB_FILTER_BUILDERS", 1}, {"STAFFB_FILTER_CLEANERS", 0},
        {"STAFFB_FILTER_LIFEGUARD", 4}, {"STAFFB_FILTER_MECHANICS", 2}, {"STAFFB_FILTER_REP", 3}, {"STAFFB_FILTER_SECURITY", 5}};
constexpr ValueRow kHireTypes[] = {{"STAFFB_TITLE_BUILDER", 1}, {"STAFFB_TITLE_CLEANER", 0}, {"STAFFB_TITLE_LIFEGUARD", 4},
        {"STAFFB_TITLE_MECHANIC", 2}, {"STAFFB_TITLE_REP", 3}, {"STAFFB_TITLE_SECURITY", 5}};
constexpr ValueRow kEvents[] = {{"BI_EVENT_SINGING", 0}, {"BI_EVENT_CABARET", 1}, {"BI_EVENT_WETTSHIRT", 2}, {"BI_EVENT_LIFTING", 3},
        {"BI_EVENT_CATWALK", 4}, {"BI_EVENT_AEROBICS", 5}};

// Column heading text key per filter. The guest sex columns share the happiness heading, the opening hours one heading.
constexpr FilterRow kHeadings[] = {{"OB_GF_HAPPY", "guest_happiness"}, {"OB_GF_SUNBURN", "guest_sunburn"},
        {"OB_GF_HEADACHE", "guest_headache"}, {"OB_GF_DRUNK", "guest_drunk"}, {"OB_GF_BLADDER", "guest_bladder"},
        {"OB_GF_ILL", "guest_illness"}, {"OB_GF_CASH", "guest_cash"}, {"OB_GF_HAPPY", "guest_male"}, {"OB_GF_HAPPY", "guest_female"},
        {"OB_GF_PRICE", "guest_price"}, {"OB_GF_SAFETY", "guest_safety"}, {"OB_GF_QUEUING", "guest_crowding"},
        {"OB_GF_BEAUTY", "guest_beauty"}, {"OB_GF_CLEANLINESS", "guest_cleanliness"}, {"OB_GF_TOTTY", "guest_attractiveness"},
        {"OB_GF_VARIETY", "guest_variety"}, {"OB_BF_REPAIR", "building_repair"}, {"OB_BF_INCOME", "building_income"},
        {"OB_BF_COSTS", "building_cost"}, {"OB_BF_PROFIT", "building_profit"}, {"OB_BF_AVPROFIT", "building_average_profit"},
        {"OB_BF_POWER", "building_power"}, {"OB_BH_HOURS", "building_open"}, {"OB_BH_HOURS", "building_closed"},
        {"OB_BF_AGE", "building_age"}, {"OB_BF_CUSTOMERS_TODAY", "building_visits"}, {"OB_BF_FAULTS", "building_faults"}};

// Sprite names and tooltip keys of a list row's buttons, in index order.
constexpr std::string_view kRowButtons[] = {"UI3_ob_goto", "MENU_TIPTEXT_MOREINFO", "STAFFB_TIPTEXT_HOURS_DOWN", "STAFFB_TIPTEXT_HOURS_UP",
        "STAFFB_TIPTEXT_GOTO_HOME", "MENU_TIPTEXT_THOUGHTINFO", "UI3_inc_flat", "UI3_dec_flat"};

std::vector<GuiListEntry> FromFilters(std::span<const FilterRow> rows)
{
    std::vector<GuiListEntry> out;
    for (const FilterRow& row : rows) out.push_back({std::string(row.key), row.key.empty() ? -1 : gui_content::FilterId(row.filter)});
    return out;
}

std::vector<GuiListEntry> FromValues(std::span<const ValueRow> rows)
{
    std::vector<GuiListEntry> out;
    for (const ValueRow& row : rows) out.push_back({std::string(row.key), row.value});
    return out;
}

void FillBrowserDefinitions(ExeGuiTables& out)
{
    out.guestFilters = FromFilters(kGuestFilters);
    out.buildingFilters = FromFilters(kBuildingFilters);
    out.cellTypes = FromValues(kCellTypes);
    out.staffFilters = FromValues(kStaffFilters);
    out.hireTypes = FromValues(kHireTypes);
    out.events = FromValues(kEvents);
    out.headings = FromFilters(kHeadings);
    for (std::string_view name : kRowButtons) out.rowButtons.emplace_back(name);
}

// The credits table by its structure: 20-byte rows {font, line, color, isLong, kind}, a title row (line 999) followed by
// the lines in strictly rising order from 0; it ends at the first row that breaks that shape and must be unique.
constexpr uint32_t kCreditRowSize = 20;
constexpr int32_t kCreditTitleLine = 999;
constexpr int32_t kMaxCreditFontOrKind = 255;
constexpr uint32_t kMaxCreditColor = 0xFFFFFF; // 0x00RRGGBB

std::optional<GuiCreditRow> CreditRowAt(std::span<const uint8_t> bytes, size_t pos)
{
    if (pos + kCreditRowSize > bytes.size()) return std::nullopt;
    const uint8_t* p = bytes.data() + pos;
    GuiCreditRow row{static_cast<int32_t>(LoadU32(p)), static_cast<int32_t>(LoadU32(p + 4)), LoadU32(p + 8), LoadU32(p + 12),
            static_cast<int32_t>(LoadU32(p + 16))};
    if (row.font < 0 || row.font > kMaxCreditFontOrKind || row.kind < 0 || row.kind > kMaxCreditFontOrKind) return std::nullopt;
    if (row.color > kMaxCreditColor || row.isLong > 1) return std::nullopt;
    return row;
}

bool FindCredits(const PeFile& exe, ExeGuiTables& out, std::string& error)
{
    auto bytes = exe.Bytes();
    std::optional<std::vector<GuiCreditRow>> found;
    for (const PeSection& section : exe.Sections()) {
        const size_t begin = section.rawOffset;
        const size_t end = std::min(bytes.size(), begin + section.rawSize);
        const auto data = bytes.subspan(begin, end - begin);
        for (size_t pos = 0; pos + kCreditRowSize <= data.size(); pos += 4) {
            auto title = CreditRowAt(data, pos);
            if (!title || title->line != kCreditTitleLine) continue;
            std::vector<GuiCreditRow> rows{*title};
            for (size_t at = pos + kCreditRowSize;; at += kCreditRowSize) {
                auto row = CreditRowAt(data, at);
                const int32_t previous = rows.size() == 1 ? -1 : rows.back().line;
                if (!row || row->line <= previous || row->line >= kCreditTitleLine) break;
                rows.push_back(*row);
            }
            if (rows.size() < 2 || rows[1].line != 0) continue;
            if (found) {
                error = "two credits tables";
                return false;
            }
            found = std::move(rows);
        }
    }
    if (!found) {
        error = "no credits table (a title row, then rows of rising line numbers from 0)";
        return false;
    }
    out.credits = std::move(*found);
    return true;
}

} // namespace

bool FindGuiTables(const PeFile& exe, ExeGuiTables& out, std::string& error)
{
    out = {};
    error.clear();
    FillBrowserDefinitions(out);
    const bool ok = FindCredits(exe, out, error);
    if (!ok) out = {};
    return ok;
}

} // namespace obl::convert
