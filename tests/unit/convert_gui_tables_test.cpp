
#include "unit_test.h"

#include "importer/src/readers/gui_tables.h"
#include "importer/src/source/pe_file.h"
#include "game/ui/gui_definitions.h"
#include "importer/src/writers/gui_writer.h"
#include "formats/gui_content.h"
#include "og_data_dir.h"
#include "synthetic_exe.h"
#include "temp_dir.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using obl::convert::ExeGuiTables;
using obl::convert::FindGuiTables;
using obl::convert::GuiCreditRow;
using obl::convert::GuiListEntry;
using obl::convert::PeFile;

namespace {

constexpr uint32_t kRva = 0x1000;
constexpr uint32_t kSize = 0x2000;

// A PE32 image with one section holding a credits table (a title row, rows of rising lines, then a row that breaks the
// shape). Vary `shift` to cover section relocation between digital and CD inputs.
struct CreditsExe : obl::test::SyntheticExe {
    size_t raw = 0;
    size_t creditsAt = 0x40; // section offset of the table
    std::vector<GuiCreditRow> credits = {{2, 999, 0, 1, 0}, {0, 0, 0xFF0000, 0, 0}, {1, 7, 0x123456, 1, 3}};

    explicit CreditsExe(uint32_t shift = 0)
    {
        raw = AddSection(".data", kRva + shift, kSize, kDataFlags);
        Write(creditsAt);
    }

    // Writes the table at section offset `at`, followed by a row that breaks the shape (a font above the limit).
    void Write(size_t at)
    {
        for (const GuiCreditRow& row : credits) {
            for (uint32_t v : {static_cast<uint32_t>(row.font), static_cast<uint32_t>(row.line), row.color, row.isLong,
                         static_cast<uint32_t>(row.kind)}) {
                obl::test::PutU32(bytes, raw + at, v);
                at += 4;
            }
        }
        obl::test::PutU32(bytes, raw + at, 0xFFFF);
    }
};

bool Find(const std::vector<uint8_t>& bytes, ExeGuiTables& out, std::string& error)
{
    auto exe = PeFile::Parse(bytes, error);
    if (!exe) return false;
    const bool ok = FindGuiTables(*exe, out, error);
    if (!ok) std::printf("  error: %s\n", error.c_str());
    return ok;
}

void CheckEqual(const ExeGuiTables& a, const ExeGuiTables& b)
{
    OBL_CHECK(a.guestFilters == b.guestFilters);
    OBL_CHECK(a.buildingFilters == b.buildingFilters);
    OBL_CHECK(a.cellTypes == b.cellTypes);
    OBL_CHECK(a.staffFilters == b.staffFilters);
    OBL_CHECK(a.hireTypes == b.hireTypes);
    OBL_CHECK(a.events == b.events);
    OBL_CHECK(a.headings == b.headings);
    OBL_CHECK(a.rowButtons == b.rowButtons);
    OBL_CHECK(a.credits == b.credits);
}

ExeGuiTables TablesOf(const std::filesystem::path& exePath)
{
    std::ifstream in(exePath, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ExeGuiTables out;
    std::string error;
    if (!Find(bytes, out, error)) std::printf("  in %s\n", exePath.string().c_str());
    nlohmann::json browser, credits;
    OBL_CHECK(obl::convert::GuiDocuments(out, browser, credits, error));
    obl::gui_content::Content decoded;
    if (obl::gui_content::Parse(browser, credits, decoded, error)) {
        // Converter/reader equivalence: the engine adapter sees the converter's records unchanged.
        const std::vector<GuiListEntry>* lists[] = {
                &out.guestFilters, &out.buildingFilters, &out.cellTypes, &out.staffFilters, &out.hireTypes, &out.events};
        for (size_t l = 0; l < std::size(lists); ++l) {
            OBL_REQUIRE(decoded.lists[l].size() == lists[l]->size());
            for (size_t i = 0; i < lists[l]->size(); ++i) {
                OBL_CHECK_EQ(decoded.lists[l][i].key, (*lists[l])[i].key);
                OBL_CHECK_EQ(decoded.lists[l][i].value, (*lists[l])[i].value);
            }
        }
        OBL_REQUIRE(decoded.headings.size() == out.headings.size());
        for (size_t i = 0; i < out.headings.size(); ++i) {
            OBL_CHECK_EQ(decoded.headings[i].key, out.headings[i].key);
            OBL_CHECK_EQ(decoded.headings[i].filter, out.headings[i].value);
        }
        OBL_REQUIRE(out.rowButtons.size() == decoded.buttons.size());
        for (size_t i = 0; i < decoded.buttons.size(); ++i) OBL_CHECK_EQ(decoded.buttons[i], out.rowButtons[i]);
        OBL_CHECK_EQ(decoded.credits.size(), out.credits.size());
        for (size_t i = 0; i < out.credits.size(); ++i) {
            OBL_CHECK_EQ(decoded.credits[i].line, out.credits[i].line);
            OBL_CHECK_EQ(decoded.credits[i].font, out.credits[i].font);
            OBL_CHECK_EQ(decoded.credits[i].color, out.credits[i].color);
            OBL_CHECK_EQ(decoded.credits[i].longSpacing, out.credits[i].isLong != 0);
            OBL_CHECK_EQ(decoded.credits[i].kind, out.credits[i].kind);
        }
    } else
        OBL_CHECK(error.empty());
    return out;
}

} // namespace

OBL_TEST_CASE("convert.gui_tables_credits_found_by_structure")
{
    for (uint32_t shift : {0u, 0x1000u, 0x23000u}) {
        CreditsExe fixture(shift);
        ExeGuiTables out;
        std::string error;
        OBL_CHECK(Find(fixture.bytes, out, error));
        OBL_CHECK(error.empty());
        OBL_CHECK(out.credits == fixture.credits);
    }
}

OBL_TEST_CASE("convert.gui_tables_browser_definitions_are_ours")
{
    CreditsExe exe;
    ExeGuiTables out;
    std::string error;
    OBL_REQUIRE(Find(exe.bytes, out, error));
    OBL_REQUIRE(!out.guestFilters.empty());
    OBL_REQUIRE(!out.buildingFilters.empty());
    OBL_CHECK_EQ(out.guestFilters.front().key, std::string("OB_GF_HAPPY"));
    OBL_CHECK_EQ(out.buildingFilters.front().key, std::string("OB_BF_ALL"));
    OBL_CHECK_EQ(out.rowButtons.size(), size_t{8});
    OBL_CHECK_EQ(out.headings.size(), size_t{27});
    for (const auto* list : {&out.guestFilters, &out.buildingFilters, &out.headings})
        for (const GuiListEntry& row : *list) OBL_CHECK(row.Separator() || row.value > 0);
}

OBL_TEST_CASE("convert.gui_tables_refuse_broken_credits")
{
    ExeGuiTables out;
    std::string error;

    CreditsExe twoCredits; // a copy of the credits table elsewhere in the section
    twoCredits.Write(0x400);
    OBL_CHECK(!Find(twoCredits.bytes, out, error));
    OBL_CHECK(error.find("two credits tables") != std::string::npos);
    OBL_CHECK(out.credits.empty());
    OBL_CHECK(out.guestFilters.empty());

    CreditsExe noTitle; // the title row's line is no longer 999
    obl::test::PutU32(noTitle.bytes, noTitle.raw + noTitle.creditsAt + 4, 998);
    OBL_CHECK(!Find(noTitle.bytes, out, error));
    OBL_CHECK(error.find("no credits table") != std::string::npos);

    CreditsExe unordered; // the line numbers after the title no longer start at 0
    obl::test::PutU32(unordered.bytes, unordered.raw + unordered.creditsAt + 24, 3);
    OBL_CHECK(!Find(unordered.bytes, out, error));
    OBL_CHECK(error.find("no credits table") != std::string::npos);
}

OBL_TEST_CASE("data.convert_gui_tables_match_gui_code")
{
    // TablesOf must round-trip the digital release, and every filter named by a heading must have a value
    // cell definition in our GUI code (ours also define filter 17, which no heading names).
    const std::filesystem::path exePath = std::filesystem::path(OBL_GAME_DIR) / "BeachLife.exe";
    if (!std::filesystem::exists(exePath)) OBL_SKIP("run/ has no imported game data");
    const ExeGuiTables found = TablesOf(exePath);
    OBL_REQUIRE(!found.headings.empty());
    for (const GuiListEntry& heading : found.headings) {
        const bool defined = std::any_of(std::begin(obl::gui_data::kFilterDefinitions), std::end(obl::gui_data::kFilterDefinitions),
                [&](const auto& d) { return static_cast<int>(d.m_filter) == heading.value; });
        if (!defined) std::printf("  heading %s: filter %d has no definition\n", heading.key.c_str(), heading.value);
        OBL_CHECK(defined);
    }
    OBL_REQUIRE(!found.credits.empty());
    OBL_CHECK_EQ(found.credits.back().kind, 1);
}

namespace {
// Row-by-row comparison of two converted contents, so a difference names the list, row or field it sits in.
void CheckSameContent(const obl::gui_content::Content& got, const obl::gui_content::Content& expected)
{
    for (size_t i = 0; i < expected.lists.size(); ++i) {
        OBL_REQUIRE(got.lists[i].size() == expected.lists[i].size());
        for (size_t j = 0; j < expected.lists[i].size(); ++j) {
            OBL_CHECK(got.lists[i][j].key == expected.lists[i][j].key);
            OBL_CHECK(got.lists[i][j].value == expected.lists[i][j].value);
        }
    }
    OBL_REQUIRE(got.headings.size() == expected.headings.size());
    for (size_t i = 0; i < expected.headings.size(); ++i) {
        OBL_CHECK(got.headings[i].key == expected.headings[i].key);
        OBL_CHECK(got.headings[i].filter == expected.headings[i].filter);
    }
    OBL_CHECK(got.buttons == expected.buttons);
    OBL_REQUIRE(got.credits.size() == expected.credits.size());
    for (size_t i = 0; i < expected.credits.size(); ++i) {
        const auto& g = got.credits[i];
        const auto& e = expected.credits[i];
        OBL_CHECK(g.font == e.font);
        OBL_CHECK(g.line == e.line);
        OBL_CHECK(g.color == e.color);
        OBL_CHECK(g.longSpacing == e.longSpacing);
        OBL_CHECK(g.kind == e.kind);
    }
}
} // namespace

OBL_TEST_CASE("data.gui_pack_matches_exe_tables")
{
    const std::filesystem::path exePath = std::filesystem::path(OBL_GAME_DIR) / "BeachLife.exe";
    if (!std::filesystem::exists(exePath)) OBL_SKIP("run/ has no imported game data");
    const ExeGuiTables found = TablesOf(exePath);
    nlohmann::json browser, credits;
    std::string error;
    OBL_REQUIRE(obl::convert::GuiDocuments(found, browser, credits, error));
    obl::gui_content::Content expected;
    OBL_REQUIRE(obl::gui_content::Parse(browser, credits, expected, error));

    const obl::test::TempPath tmp("obl-gui-pack-test");
    OBL_REQUIRE(obl::convert::WriteGuiTables(found, *obl::convert::OpenDirectorySink(tmp.path), error));
    obl::gui_content::Content loaded;
    OBL_REQUIRE(obl::gui_content::LoadPack(tmp.path, loaded, error));
    CheckSameContent(loaded, expected);

    const std::filesystem::path installed = std::filesystem::path(OBL_GAME_DIR) / "packs" / "base";
    if (!std::filesystem::exists(installed / "gui" / "object_browser.json")) {
        std::printf("  run/packs/base/gui is not written yet\n");
        return;
    }
    obl::gui_content::Content fromRun;
    OBL_REQUIRE(obl::gui_content::LoadPack(installed, fromRun, error));
    CheckSameContent(fromRun, expected);
}

OBL_TEST_CASE("data.convert_gui_tables_of_cd_executables")
{
    // Compare both CDs with the digital release: credits match, while browser lists are our
    // own definitions and do not depend on the executable.
    auto ogData = obl::test::FindOgData();
    if (!ogData) OBL_SKIP("the installation was not found (set OBL_OGDATA)");
    const std::filesystem::path digital = *ogData / "program-files" / "Beach Life" / "BeachLife.exe";
    if (!std::filesystem::exists(digital)) OBL_SKIP("the installation has no BeachLife.exe");
    const ExeGuiTables expected = TablesOf(digital);

    int checked = 0;
    for (const char* cd : obl::test::kCdCopies) {
        const std::filesystem::path exePath = *ogData / cd / "BeachLife.exe";
        if (!std::filesystem::exists(exePath)) continue;
        ++checked;
        CheckEqual(TablesOf(exePath), expected);
    }
    if (checked == 0) std::printf("  no second executable found; only the main one was checked\n");
}
