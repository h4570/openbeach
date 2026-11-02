#include "formats/gui_content.h"
#include "importer/src/writers/gui_writer.h"
#include "unit_test.h"
#include <filesystem>
#include <fstream>

namespace {
obl::convert::ExeGuiTables Example()
{
    obl::convert::ExeGuiTables t;
    t.guestFilters = {{"SAMPLE_HAPPINESS", 1}, {"", -1}, {"SAMPLE_CASH", 2}};
    t.buildingFilters = {{"SAMPLE_ALL", 17}};
    t.cellTypes = {{"SAMPLE_LAND", -2147483647}};
    t.staffFilters = {{"SAMPLE_STAFF", 6}};
    t.hireTypes = {{"SAMPLE_CLEANER", 0}};
    t.events = {{"SAMPLE_SHOW", 0}};
    t.headings = {{"SAMPLE_COLUMN", 2}};
    t.rowButtons = {"a", "b", "c", "d", "e", "f", "g", "h"};
    t.credits = {{0, 1, 0x123456, 0, 0}, {1, 2, 0xffffff, 1, 1}};
    return t;
}
} // namespace

OBL_TEST_CASE("gui_content.convert_round_trip")
{
    auto source = Example();
    nlohmann::json b, c;
    std::string error;
    OBL_REQUIRE(obl::convert::GuiDocuments(source, b, c, error));
    OBL_CHECK_EQ(b["lists"]["guest"][0]["filter"].get<std::string>(), "guest_happiness");
    OBL_CHECK(b["lists"]["guest"][1]["separator"].get<bool>());
    obl::gui_content::Content out;
    OBL_REQUIRE(obl::gui_content::Parse(b, c, out, error));
    OBL_CHECK_EQ(out.lists[0].size(), size_t{3});
    OBL_CHECK_EQ(out.lists[0][2].value, 2);
    OBL_CHECK_EQ(out.lists[2][0].value, -2147483647);
    OBL_CHECK_EQ(out.headings[0].key, "SAMPLE_COLUMN");
    OBL_CHECK_EQ(out.headings[0].filter, 2);
    OBL_CHECK_EQ(out.buttons[7], "h");
    OBL_CHECK_EQ(out.credits.size(), size_t{2});
    OBL_CHECK_EQ(out.credits[0].color, 0x123456u);
    OBL_CHECK(out.credits[1].longSpacing);
    OBL_CHECK_EQ(out.credits[1].kind, 1);
}

OBL_TEST_CASE("gui_content.refuse_invalid_content_atomically")
{
    nlohmann::json b, c;
    std::string error;
    OBL_REQUIRE(obl::convert::GuiDocuments(Example(), b, c, error));
    obl::gui_content::Content out;
    OBL_REQUIRE(obl::gui_content::Parse(b, c, out, error));
    for (int mutation = 0; mutation < 8; ++mutation) {
        auto badB = b, badC = c;
        switch (mutation) {
        case 0: badB["version"] = 1; break;
        case 1: badB["lists"]["guest"][0]["filter"] = "unrecognized"; break;
        case 2: badB["lists"]["guest"][0]["filter"] = "building_all"; break;
        case 3: badB["buttons"].erase(0); break;
        case 4: badC["rows"][0]["kind"] = 1; break;
        case 5: badC["rows"][1]["kind"] = 0; break;
        case 6: badC["rows"][0]["color"] = -1; break;
        case 7: badB["lists"]["hire"][0]["value"] = 99; break;
        }
        OBL_CHECK(!obl::gui_content::Parse(badB, badC, out, error));
        OBL_CHECK(!error.empty());
        OBL_CHECK_EQ(out.credits.size(), size_t{2});
        OBL_CHECK_EQ(out.lists[0][0].key, "SAMPLE_HAPPINESS");
    }
}

OBL_TEST_CASE("gui_content.writer_files_and_failure")
{
    const auto root = std::filesystem::path(OBL_REPO_DIR) / "run/tmp/gui-writer-test";
    std::string error;
    OBL_REQUIRE(obl::convert::WriteGuiTables(Example(), *obl::convert::OpenDirectorySink(root), error));
    nlohmann::json b, c;
    std::ifstream(root / "gui/object_browser.json") >> b;
    std::ifstream(root / "gui/credits.json") >> c;
    obl::gui_content::Content out;
    OBL_REQUIRE(obl::gui_content::Parse(b, c, out, error));
    auto bad = Example();
    bad.guestFilters[0].value = 999;
    OBL_CHECK(!obl::convert::WriteGuiTables(bad, *obl::convert::OpenDirectorySink(root), error));
    nlohmann::json unchanged;
    std::ifstream(root / "gui/object_browser.json") >> unchanged;
    OBL_CHECK(b == unchanged);
    OBL_CHECK(!obl::convert::WriteGuiTables(Example(), *obl::convert::OpenDirectorySink(root / "gui/credits.json"), error));
}
