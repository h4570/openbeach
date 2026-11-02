// Converted pack texts (src/game/tables/text_load.h): the units the engine draws, and what the loader accepts from a pack.

#include "unit_test.h"

#include "game/tables/text_load.h"
#include "core/ini_file.h"
#include "game/game/settings_ini.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace obl;

namespace {

std::u16string Unit(char16_t unit)
{
    return std::u16string(1, unit);
}

} // namespace

OBL_TEST_CASE("text_load.engine_value_returns_code_page_bytes")
{
    // U+0105 (a with ogonek) is byte 0xB9 of Windows-1250; U+20AC (euro) is byte 0x80 of Windows-1252, and U+0105 is not in 1252.
    OBL_CHECK(text_load::EngineValue(Unit(0x0105), true) == Unit(0xB9));
    OBL_CHECK(text_load::EngineValue(Unit(0x0105), false) == Unit(0x0105));
    OBL_CHECK(text_load::EngineValue(Unit(0x20AC), false) == Unit(0x80));
    OBL_CHECK(text_load::EngineValue(Unit(0x20AC), true) == Unit(0x80));
    // The C1 code point of an undefined byte stays (the converter kept it), Latin-1 is the same unit, ASCII too, and a unit no byte
    // of the code page has passes through.
    OBL_CHECK(text_load::EngineValue(Unit(0x81), false) == Unit(0x81));
    OBL_CHECK(text_load::EngineValue(Unit(0xE9), false) == Unit(0xE9));
    OBL_CHECK(text_load::EngineValue(u"Ab", true) == u"Ab");
    OBL_CHECK(text_load::EngineValue(Unit(0x2603), true) == Unit(0x2603));
}

OBL_TEST_CASE("text_load.pack_table_reads_entries_and_refuses_bad_files")
{
    const obl::test::TempPath dir("obl_text_load");
    std::filesystem::create_directories(dir.path / "text");
    {
        std::ofstream out(dir.path / "text" / "fr.json");
        out << R"({"format": "obl.text", "version": 1, "language": "fr", "entries": [["STRING_ONE", "premier"], ["STRING_TWO", "deux"]]})";
    }
    std::vector<text::Entry> entries;
    std::string error;
    OBL_CHECK(text_load::LoadPack(dir.path, "fr", entries, error));
    OBL_CHECK(error.empty());
    OBL_CHECK_EQ(entries.size(), 2u);
    OBL_CHECK_EQ(entries[0].key, std::string("STRING_ONE"));
    OBL_CHECK(entries[1].value == u"deux");

    OBL_CHECK(text_load::LoadPack(dir.path, "de", entries, error));
    OBL_CHECK(entries.empty());

    // A file that does not follow obl.text fails, names itself and leaves no entries behind.
    {
        std::ofstream out(dir.path / "text" / "it.json");
        out << R"({"format": "obl.text", "version": 1, "language": "it", "entries": "not a list"})";
    }
    OBL_CHECK(!text_load::LoadPack(dir.path, "it", entries, error));
    OBL_CHECK(error.find("it.json") != std::string::npos);
    OBL_CHECK(entries.empty());
}

OBL_TEST_CASE("text_load.language_codes_follow_the_settings_order")
{
    // Language indices select English, French, Italian, German, Spanish or Polish; other values use English.
    OBL_CHECK(std::string(text_load::LanguageCode(0)) == "en");
    OBL_CHECK(std::string(text_load::LanguageCode(5)) == "pl");
    OBL_CHECK(std::string(text_load::LanguageCode(6)) == "en");
    OBL_CHECK(text_load::PackPath("pl") == "text/pl.json");
}

OBL_TEST_CASE("text_load.missing_language_falls_back_and_notifies_once")
{
    const obl::test::TempPath dir("obl_missing_language");
    const auto pack = dir.path / "packs" / "base";
    std::filesystem::create_directories(pack / "text");
    std::ofstream(pack / "text/pl.json") << R"({"format":"obl.text","version":1,"language":"pl","entries":[["HELLO","Imported text"]]})";
    text_load::LanguageSelection selection;
    unsigned language = 0; // English selected in BeachLife.ini, Polish copy imported.
    std::string notice;
    std::string error;
    OBL_REQUIRE(selection.Select(pack, language, notice, error));
    OBL_CHECK_EQ(language, 5u);
    OBL_CHECK(notice.find("was not imported from your copy") != std::string::npos);
    OBL_CHECK(notice.find("(en)") != std::string::npos);
    OBL_CHECK(notice.find("Using pl") != std::string::npos);
    OBL_CHECK(error.empty());
    std::vector<text::Entry> entries;
    OBL_REQUIRE(text_load::LoadPack(pack, text_load::LanguageCode(language), entries, error));
    OBL_REQUIRE(entries.size() == 1u);
    OBL_CHECK(entries[0].value == u"Imported text");
    language = 0;
    OBL_REQUIRE(selection.Select(pack, language, notice, error));
    OBL_CHECK_EQ(language, 5u);
    OBL_CHECK(notice.empty());

    // A requested imported table stays selected; mod tables count as available too.
    text_load::LanguageSelection fresh;
    OBL_REQUIRE(fresh.Select(pack, language, notice, error));
    OBL_CHECK(!fresh.notified);
    OBL_CHECK(notice.empty());
    std::filesystem::create_directories(dir.path / "mod/text");
    std::filesystem::copy_file(pack / "text/pl.json", dir.path / "mod/text/en.json");
    language = 0;
    OBL_REQUIRE(fresh.Select(pack, language, notice, error));
    OBL_CHECK_EQ(language, 0u);
    OBL_CHECK(notice.empty());

    std::filesystem::remove(dir.path / "mod/text/en.json");
    // Fallback order is deterministic; an imported but broken table still reports its loader error.
    std::ofstream(pack / "text/fr.json") << "invalid JSON";
    OBL_REQUIRE(fresh.Select(pack, language, notice, error));
    OBL_CHECK_EQ(language, 1u);
    OBL_CHECK(!text_load::LoadPack(pack, text_load::LanguageCode(language), entries, error));
    OBL_CHECK(error.find("fr.json") != std::string::npos);
    std::filesystem::remove(pack / "text/fr.json");
    std::filesystem::remove(pack / "text/pl.json");
    OBL_CHECK(!fresh.Select(pack, language, notice, error));
    OBL_CHECK(error.find("No imported language tables") != std::string::npos);
    OBL_CHECK(notice.empty());
}

OBL_TEST_CASE("text_load.fallback_prefers_the_packs_language")
{
    const obl::test::TempPath dir("obl_pack_language");
    std::filesystem::create_directories(dir.path / "text");
    for (const char* code : {"fr", "pl"}) std::ofstream(dir.path / "text" / (std::string(code) + ".json")) << "{}";
    std::ofstream(dir.path / "pack.json") << R"({"language": "pl"})";
    OBL_CHECK(text_load::PackLanguage(dir.path) == "pl");
    text_load::LanguageSelection selection;
    unsigned language = 0; // the INI says English, the copy has French and Polish tables, and was imported in Polish
    std::string notice;
    std::string error;
    OBL_REQUIRE(selection.Select(dir.path, language, notice, error, text_load::PackLanguage(dir.path)));
    OBL_CHECK_EQ(language, 5u);
    text_load::LanguageSelection plain;
    language = 0;
    OBL_REQUIRE(plain.Select(dir.path, language, notice, error));
    OBL_CHECK_EQ(language, 1u); // without a preference the first table in settings order
    OBL_CHECK(text_load::PackLanguage(dir.path / "none").empty());
}

OBL_TEST_CASE("text_load.l0_fallback_keeps_requested_language")
{
    const obl::test::TempPath dir("obl_l0_language");
    std::filesystem::create_directories(dir.path / "text");
    std::ofstream(dir.path / "text/en.json")
            << R"({"format":"obl.text","version":1,"language":"en","entries":[["L0_PLAY_EXITDIALOG","Close"]]})";
    struct IniEnv {
        std::filesystem::path path;
        void Write(const char* section, const char* key, const std::string& value)
        {
            OBL_REQUIRE(ini::WriteString(path, section, key, value));
        }
    } iniEnv{dir.path / "BeachLife.ini"};
    struct Sections {
        void Touch() {}
        void SaveSound() {}
        void SaveDialogs() {}
        void SaveVideo() {}
    } sections;
    OBL_REQUIRE(ini::WriteString(iniEnv.path, "Language", "Language", "5"));
    settings::MainValues settings{ini::ReadInt(iniEnv.path, "Language", "Language", 0), 0};
    unsigned tableLanguage = static_cast<unsigned>(settings.language);
    text_load::LanguageSelection selection;
    std::string notice, error;
    OBL_REQUIRE(selection.Select(dir.path, tableLanguage, notice, error));
    OBL_CHECK_EQ(settings.language, 5);
    settings::SaveMain(iniEnv, settings, sections);
    OBL_CHECK_EQ(ini::ReadInt(iniEnv.path, "Language", "Language", 0), 5);
    OBL_CHECK_EQ(tableLanguage, 0u);
    std::vector<text::Entry> entries;
    OBL_REQUIRE(text_load::LoadPack(dir.path, text_load::LanguageCode(tableLanguage), entries, error));
    OBL_REQUIRE(entries.size() == 1u);
    OBL_CHECK(entries[0].value == u"Close");
}
