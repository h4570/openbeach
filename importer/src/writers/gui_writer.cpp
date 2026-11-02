#include "importer/src/writers/gui_writer.h"
#include "formats/gui_content.h"
#include "core/json_schema.h"
#include "formats/schemas.h"
#include <fstream>

namespace obl::convert {
bool GuiDocuments(const ExeGuiTables& tables, nlohmann::json& browser, nlohmann::json& credits, std::string& error)
{
    using nlohmann::json;
    json b = {{"format", "obl.object_browser"}, {"version", 2}, {"provenance", "converted"}, {"lists", json::object()},
            {"headings", json::array()}, {"buttons", tables.rowButtons}};
    const std::vector<GuiListEntry>* lists[] = {
            &tables.guestFilters, &tables.buildingFilters, &tables.cellTypes, &tables.staffFilters, &tables.hireTypes, &tables.events};
    const char* names[] = {"guest", "building", "cell", "staff", "hire", "event"};
    for (size_t i = 0; i < std::size(lists); ++i) {
        auto& rows = b["lists"][names[i]] = json::array();
        for (const auto& row : *lists[i]) {
            if (row.Separator())
                rows.push_back({{"separator", true}});
            else if (i < 2)
                rows.push_back({{"key", row.key}, {"filter", gui_content::FilterName(row.value)}});
            else
                rows.push_back({{"key", row.key}, {"value", row.value}});
        }
    }
    for (const auto& row : tables.headings) b["headings"].push_back({{"key", row.key}, {"filter", gui_content::FilterName(row.value)}});
    json c = {{"format", "obl.credits"}, {"version", 2}, {"provenance", "converted"}, {"rows", json::array()}};
    for (const auto& row : tables.credits)
        c["rows"].push_back(
                {{"font", row.font}, {"line", row.line}, {"color", row.color}, {"long_spacing", row.isLong != 0}, {"kind", row.kind}});
    gui_content::Content checked;
    if (!gui_content::Parse(b, c, checked, error)) return false;
    browser = std::move(b);
    credits = std::move(c);
    return true;
}

bool WriteGuiTables(const ExeGuiTables& tables, PackSink& out, std::string& error)
{
    nlohmann::json browser, credits;
    if (!GuiDocuments(tables, browser, credits, error)) return false;
    // Every JSON the converter writes is checked against data/schemas/ before it is written (gui_content::Parse checks only the content).
    json_schema::Registry schemas;
    if (!formats::LoadEmbeddedSchemas(schemas, error)) return false;
    if (!formats::ValidateDocument(schemas, "obl.object_browser", browser, error)) return false;
    if (!formats::ValidateDocument(schemas, "obl.credits", credits, error)) return false;
    for (const auto& pair : {std::pair{"object_browser.json", &browser}, std::pair{"credits.json", &credits}})
        if (!out.WriteText(std::string("gui/") + pair.first, pair.second->dump(2) + '\n', error)) return false;
    return true;
}
} // namespace obl::convert
