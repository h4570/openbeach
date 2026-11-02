#include "unit_test.h"
#include "core/json_schema.h"
#include "core/code_pages.h"
#include "importer/src/writers/gui_layout_convert.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "temp_dir.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace obl::convert;
using obl::code_pages::CodePoint;
using obl::code_pages::kUndefinedCodePoint;

namespace {

obl::json_schema::Registry Schemas()
{
    auto dir = obl::formats::FindSchemaDirectory(std::filesystem::current_path());
    OBL_REQUIRE(dir);
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadSchemaDirectory(*dir, schemas, error));
    return schemas;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

// The installed form of text: every byte XOR 0xFF.
std::vector<uint8_t> Installed(const std::vector<uint8_t>& plain)
{
    std::vector<uint8_t> bytes;
    for (uint8_t b : plain) bytes.push_back(b ^ 0xFF);
    return bytes;
}

// The units of a UTF-8 string of one to three bytes per code point (the converter writes nothing longer).
std::u16string FromUtf8(const std::string& text)
{
    std::u16string units;
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<uint8_t>(text[i]);
        if (lead < 0x80) {
            units.push_back(lead);
            i += 1;
        } else if (lead < 0xE0) {
            units.push_back(static_cast<char16_t>(((lead & 0x1F) << 6) | (text[i + 1] & 0x3F)));
            i += 2;
        } else {
            units.push_back(static_cast<char16_t>(((lead & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) | (text[i + 2] & 0x3F)));
            i += 3;
        }
    }
    return units;
}

// The installed byte of each code point: the inverse of the code page, identity for ASCII and for undefined bytes (C1).
std::map<char16_t, uint8_t> Inverse(bool polish)
{
    std::map<char16_t, uint8_t> inverse;
    for (unsigned byte = 0; byte < 256; ++byte) {
        const unsigned code = byte < 0x80 ? byte : CodePoint(polish, byte);
        if (code == kUndefinedCodePoint)
            inverse.emplace(static_cast<char16_t>(byte), static_cast<uint8_t>(byte));
        else
            inverse.emplace(static_cast<char16_t>(code), static_cast<uint8_t>(byte));
    }
    return inverse;
}

} // namespace

OBL_TEST_CASE("convert.gui_layout_decode_code_pages")
{
    // Polish: 0xB9 is a with ogonek (U+0105), 0x80 the euro sign; Western: 0xB9 is the superscript one. 0x81 is undefined in both.
    const std::vector<uint8_t> plain = {'A', 0xB9, 0x80, 0x81, '\r', '\n'};
    std::u16string units;
    std::string error;
    OBL_REQUIRE(DecodeGuiLayout(Installed(plain), true, units, error));
    OBL_CHECK(units == std::u16string({u'A', 0x0105, 0x20AC, 0x0081, u'\r', u'\n'}));
    OBL_REQUIRE(DecodeGuiLayout(Installed(plain), false, units, error));
    OBL_CHECK(units == std::u16string({u'A', 0x00B9, 0x20AC, 0x0081, u'\r', u'\n'}));
    OBL_CHECK(!DecodeGuiLayout({}, false, units, error));
}

OBL_TEST_CASE("convert.gui_layout_refuses_without_fallback")
{
    // A revision with only Polish variants has no fallback for any layout: nothing is written, the error names it.
    const obl::test::TempPath tmp("obl-gui-layout-no-fallback");
    const auto& root = tmp.path;
    std::filesystem::create_directories(root / "Data" / "Script" / "GUI");
    std::ofstream(root / "Data" / "Script" / "GUI" / "gamewon_pol.ble", std::ios::binary).write("\xFE\xFF", 2);
    const auto out = root / "out";
    int files = 0;
    std::string error;
    OBL_CHECK(!ConvertGuiLayouts(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(out), Schemas(), files, error));
    OBL_CHECK(error == "no GUI layout has a fallback file");
    OBL_CHECK(files == 0);
    OBL_CHECK(!std::filesystem::exists(out / "gui" / "layouts.json"));
    // An installation with no GUI scripts at all is an error too.
    std::filesystem::remove(root / "Data" / "Script" / "GUI" / "gamewon_pol.ble");
    OBL_CHECK(!ConvertGuiLayouts(*obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(out), Schemas(), files, error));
    OBL_CHECK(files == 0);
}

OBL_TEST_CASE("convert.gui_layout_routes_languages_by_file_name")
{
    // Invented scripts: the suffix names the language through the text table words, in any case; an unrecognized suffix is no layout,
    // and the reset screen keeps only the fallback and the Polish variant of the main menu.
    const obl::test::TempPath tmp("obl-gui-layout-routing");
    const auto dir = tmp.path / "Data" / "Script" / "GUI";
    std::filesystem::create_directories(dir);
    for (const char* name : {"mainmenu.BLE", "MainMenu_GERMAN.ble", "MainMenu_spa.ble", "MainMenu_Pol.ble", "MainMenu_xyz.ble",
                 "MainMenu_uk.ble", "GameWon.ble", "notes.txt"})
        std::ofstream(dir / name, std::ios::binary).write("\xBE", 1);
    const auto out = tmp.path / "out";
    int files = 0;
    std::string error;
    OBL_REQUIRE(ConvertGuiLayouts(
            *obl::convert::OpenDirectorySource(tmp.path), *obl::convert::OpenDirectorySink(out), Schemas(), files, error));
    const auto doc = nlohmann::ordered_json::parse(ReadFile(out / "gui" / "layouts.json"));
    const nlohmann::ordered_json expected = {
            {"main_menu", {{"*", "main_menu.gui"}, {"de", "main_menu_de.gui"}, {"es", "main_menu_es.gui"}, {"pl", "main_menu_pl.gui"}}},
            {"reset_menu", {{"*", "main_menu.gui"}, {"pl", "main_menu_pl.gui"}}}, {"game_won", {{"*", "game_won.gui"}}}};
    OBL_CHECK(doc["layouts"] == expected);
    OBL_CHECK(files == 6); // five scripts and layouts.json
}

OBL_TEST_CASE("data.convert_gui_layouts_of_installation")
{
    // Every installed GUI script the table names: the .gui file is the installed bytes decoded, and encoding the text back gives
    // the installed file exactly (line ends, odd bytes and all). layouts.json names the files and each layout has its fallback.
    const auto dir = FindInstalledFile(OBL_GAME_DIR, "Data/Script/GUI");
    if (!dir) OBL_SKIP("no installation GUI scripts");
    const obl::test::TempPath tmp("obl-gui-layout-installation");
    const auto& out = tmp.path;
    int files = 0;
    std::string error;
    OBL_REQUIRE(ConvertGuiLayouts(
            *obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(out), Schemas(), files, error));
    OBL_CHECK(files > 0);

    const auto doc = nlohmann::ordered_json::parse(ReadFile(out / "gui" / "layouts.json"));
    OBL_CHECK(doc["format"] == "obl.gui_layouts");
    OBL_CHECK(doc["layouts"]["main_menu"]["pl"] == "main_menu_pl.gui");
    OBL_CHECK(doc["layouts"]["main_menu"]["fr"] == "main_menu_fr.gui");
    OBL_CHECK(doc["layouts"]["reset_menu"]["*"] == "main_menu.gui");
    OBL_CHECK(doc["layouts"]["game_won"]["pl"] == "game_won_pl.gui");
    OBL_CHECK(doc["layouts"]["in_game"]["pl"] == "in_game_pl.gui");
    OBL_CHECK(doc["layouts"]["intro"]["*"] == "intro.gui");
    for (const auto& [layout, choices] : doc["layouts"].items()) OBL_CHECK(choices.contains("*"));

    // Round trip of each written file against its installed script (the script names are the converter's table).
    const std::map<std::string, std::string> scripts = {{"GUI.ble", "in_game.gui"}, {"GUI_Polish.ble", "in_game_pl.gui"},
            {"Intro.ble", "intro.gui"}, {"MainMenu.ble", "main_menu.gui"}, {"MainMenu_fre.ble", "main_menu_fr.gui"},
            {"MainMenu_ita.ble", "main_menu_it.gui"}, {"MainMenu_ger.ble", "main_menu_de.gui"}, {"MainMenu_spa.ble", "main_menu_es.gui"},
            {"MainMenu_pol.ble", "main_menu_pl.gui"}, {"gamewon.ble", "game_won.gui"}, {"gamewon_pol.ble", "game_won_pl.gui"}};
    int present = 0;
    for (const auto& [script, out_name] : scripts) {
        const auto path = FindInstalledFile(OBL_GAME_DIR, "Data/Script/GUI/" + script);
        if (!path) continue;
        ++present;
        const auto installed = ReadFile(*path);
        const bool polish = script.find("pol") != std::string::npos || script.find("Pol") != std::string::npos;
        const auto text = ReadFile(out / "gui" / out_name);
        const std::string header = "#obl-gui 1\n";
        OBL_REQUIRE(text.size() > header.size());
        OBL_CHECK(std::string(text.begin(), text.begin() + header.size()) == header);
        const std::string body(text.begin() + header.size(), text.end());
        std::u16string units;
        OBL_REQUIRE(DecodeGuiLayout(installed, polish, units, error));
        OBL_CHECK(FromUtf8(body) == units);
        const auto inverse = Inverse(polish);
        std::vector<uint8_t> back;
        for (char16_t unit : FromUtf8(body)) back.push_back(static_cast<uint8_t>(inverse.at(unit) ^ 0xFF));
        OBL_CHECK(back == installed);
    }
    OBL_CHECK(present > 0);
}
