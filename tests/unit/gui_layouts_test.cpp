#include "game/ui/gui_layouts.h"
#include "core/code_pages.h"
#include "importer/src/source/source.h"
#include "temp_dir.h"
#include "unit_test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {
namespace gl = obl::gui_layouts;
namespace fs = std::filesystem;

void WriteFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
}

std::string ReadBytes(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

gl::Layouts Sample()
{
    gl::Layouts layouts;
    layouts["main_menu"]["*"] = {"main_menu.gui", false};
    layouts["main_menu"]["fr"] = {"main_menu_fr.gui", false};
    layouts["main_menu"]["pl"] = {"main_menu_pl.gui", true};
    layouts["intro"]["*"] = {"intro.gui", false};
    return layouts;
}
} // namespace

OBL_TEST_CASE("gui_layouts.resolve_follows_language_with_fallback")
{
    const gl::Layouts layouts = Sample();
    OBL_CHECK_EQ(std::string(gl::LanguageCode(0)), "*");
    OBL_CHECK_EQ(std::string(gl::LanguageCode(1)), "fr");
    OBL_CHECK_EQ(std::string(gl::LanguageCode(5)), "pl");
    OBL_CHECK_EQ(std::string(gl::LanguageCode(9)), "*");

    OBL_CHECK_EQ(gl::Resolve(layouts, "main_menu", 0)->file, "main_menu.gui");
    OBL_CHECK_EQ(gl::Resolve(layouts, "main_menu", 1)->file, "main_menu_fr.gui");
    OBL_CHECK_EQ(gl::Resolve(layouts, "main_menu", 2)->file, "main_menu.gui");
    OBL_CHECK_EQ(gl::Resolve(layouts, "main_menu", 5)->file, "main_menu_pl.gui");
    OBL_CHECK(gl::Resolve(layouts, "main_menu", 5)->polish);
    OBL_CHECK(!(gl::Resolve(layouts, "main_menu", 1)->polish));
    // A layout without its own language takes the fallback; an unrecognized layout has no file at all.
    OBL_CHECK_EQ(gl::Resolve(layouts, "intro", 5)->file, "intro.gui");
    OBL_CHECK(gl::Resolve(layouts, "game_won", 0) == nullptr);
    OBL_CHECK(gl::FindFile(layouts, "intro.gui") != nullptr);
    OBL_CHECK(gl::FindFile(layouts, "missing.gui") == nullptr);
}

OBL_TEST_CASE("gui_layouts.read_text_encodes_the_code_page")
{
    const obl::test::TempPath tmp("obl-gui-layouts-text");
    const auto& pack = tmp.path;
    // The body after the header line, in UTF-8: a Polish letter (U+0105, byte 0xB9 in Windows-1250), an e-acute (U+00E9, 0xE9 in
    // Windows-1252) and an undefined byte kept as its C1 code point (U+0081, byte 0x81).
    WriteFile(pack / "gui" / "pl.gui", "#obl-gui 1\n\xc4\x85");
    WriteFile(pack / "gui" / "fr.gui", "#obl-gui 1\nC\xc3\xa9\xc2\x81");
    WriteFile(pack / "gui" / "bad.gui", "no header\n");
    WriteFile(pack / "gui" / "wide.gui", "#obl-gui 1\n\xc4\x85");
    std::string bytes, error;

    OBL_REQUIRE(gl::ReadText(pack, {"pl.gui", true}, bytes, error));
    OBL_CHECK_EQ(bytes, std::string("\xb9", 1));

    OBL_REQUIRE(gl::ReadText(pack, {"fr.gui", false}, bytes, error));
    OBL_CHECK_EQ(bytes, std::string("C\xe9\x81", 3));

    // U+0105 is not in Windows-1252: the file is refused rather than written with a guessed byte.
    OBL_CHECK(!(gl::ReadText(pack, {"wide.gui", false}, bytes, error)));
    OBL_CHECK(!(error.empty()));
    OBL_CHECK(!(gl::ReadText(pack, {"bad.gui", false}, bytes, error)));
    OBL_CHECK(!(gl::ReadText(pack, {"absent.gui", false}, bytes, error)));
}

OBL_TEST_CASE("gui_layouts.code_page_inverse_round_trips")
{
    // Every byte of both code pages decodes and encodes back to itself; the undefined ones through their C1 code point.
    for (bool polish : {false, true}) {
        for (unsigned byte = 0; byte < 256; ++byte) {
            const unsigned code = obl::code_pages::CodePoint(polish, byte);
            const char16_t unit = static_cast<char16_t>(code == obl::code_pages::kUndefinedCodePoint ? byte : code);
            uint8_t back = 0;
            OBL_REQUIRE(obl::code_pages::ByteOf(polish, unit, back));
            OBL_CHECK_EQ(static_cast<unsigned>(back), byte);
        }
    }
    std::string bytes;
    OBL_CHECK(obl::code_pages::Encode(true, u"ąz", bytes));
    OBL_CHECK_EQ(bytes, std::string("\xb9z", 2));
    OBL_CHECK(!(obl::code_pages::Encode(false, u"ą", bytes)));
}

OBL_TEST_CASE("gui_layouts.load_refuses_a_layout_without_fallback")
{
    const obl::test::TempPath tmp("obl-gui-layouts-document");
    const auto& pack = tmp.path;
    std::string error;
    gl::Layouts out;

    WriteFile(pack / "gui" / "layouts.json",
            R"({"format": "obl.gui_layouts", "version": 1, "layouts": {"intro": {"*": "intro.gui"}, "main_menu": {"fr": "main_menu_fr.gui"}}})");
    OBL_CHECK(!(gl::LoadLayouts(pack, out, error)));
    OBL_CHECK(out.empty());

    WriteFile(pack / "gui" / "layouts.json",
            R"({"format": "obl.gui_layouts", "version": 1, "layouts": {"intro": {"*": "intro.gui"}, "main_menu": {"*": "main_menu.gui", "pl": "main_menu_pl.gui"}}})");
    OBL_REQUIRE(gl::LoadLayouts(pack, out, error));
    OBL_CHECK_EQ(out.size(), 2u);
    OBL_CHECK(gl::Resolve(out, "main_menu", 5)->polish);
}

OBL_TEST_CASE("data.gui_layouts_match_engine_loader")
{
    const fs::path root = OBL_GAME_DIR;
    const auto pack = root / "packs" / "base";
    if (!obl::convert::FindInstalledFile(root, "Data/Script/GUI")) OBL_SKIP("no installation GUI scripts");
    if (!fs::exists(pack / "gui" / "layouts.json")) OBL_SKIP("converted GUI layouts unavailable");
    // The installed script of each converted file (the converter's table).
    const std::map<std::string, std::string> scripts = {{"in_game.gui", "GUI.ble"}, {"in_game_pl.gui", "GUI_Polish.ble"},
            {"intro.gui", "Intro.ble"}, {"main_menu.gui", "MainMenu.ble"}, {"main_menu_fr.gui", "MainMenu_fre.ble"},
            {"main_menu_it.gui", "MainMenu_ita.ble"}, {"main_menu_de.gui", "MainMenu_ger.ble"}, {"main_menu_es.gui", "MainMenu_spa.ble"},
            {"main_menu_pl.gui", "MainMenu_pol.ble"}, {"game_won.gui", "gamewon.ble"}, {"game_won_pl.gui", "gamewon_pol.ble"}};
    gl::Layouts layouts;
    std::string error;
    OBL_REQUIRE(gl::LoadLayouts(pack, layouts, error));
    int checked = 0;
    for (const auto& [layout, languages] : layouts) {
        for (const auto& [language, choice] : languages) {
            const auto script = scripts.find(choice.file);
            if (script == scripts.end()) continue;
            const auto path = obl::convert::FindInstalledFile(root, "Data/Script/GUI/" + script->second);
            if (!path) continue;
            std::string installed = ReadBytes(*path);
            for (char& c : installed) c = static_cast<char>(static_cast<unsigned char>(c) ^ 0xFFu);
            std::string text;
            OBL_REQUIRE(gl::ReadText(pack, choice, text, error));
            OBL_CHECK_EQ(text, installed);
            ++checked;
        }
    }
    OBL_CHECK(checked > 0);
}
