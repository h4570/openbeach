#pragma once

#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace obl::gui_content {

// Stable format names; numeric ids remain only at the boundary to the current GUI engine.
std::string_view FilterName(int id);
int FilterId(std::string_view name);

struct ListEntry {
    std::string key;
    int value;
};
struct Heading {
    std::string key;
    int filter;
};
struct Credit {
    int font, line;
    uint32_t color;
    bool longSpacing;
    int kind;
};
struct Content {
    // guest, building, cell, staff, hire, event
    std::array<std::vector<ListEntry>, 6> lists;
    std::vector<Heading> headings;
    std::array<std::string, 8> buttons;
    std::vector<Credit> credits;
};

// Validates both documents, including engine semantics, before replacing out.
bool Parse(const nlohmann::json& browser, const nlohmann::json& credits, Content& out, std::string& error);

// Reads <packDir>/gui/object_browser.json and <packDir>/gui/credits.json (the converted pack, docs/build/game-data.md) and
// parses them. Nothing is replaced when a file is missing, is not JSON or is refused by Parse.
bool LoadPack(const std::filesystem::path& packDir, Content& out, std::string& error);

} // namespace obl::gui_content
