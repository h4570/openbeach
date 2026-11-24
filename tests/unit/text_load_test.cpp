// Converted pack texts (src/game/tables/text_load.h): the units the engine draws, and what the loader accepts from a pack.

#include "unit_test.h"

#include "core/pack_path.h"
#include "game/tables/text_load.h"
#include "formats/mod_pack.h"
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

void Write(const std::filesystem::path& file, const std::string& text)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string Patch(const std::string& entries)
{
    return R"({"format":"obl.text_patch","version":1,"entries":)" + entries + "}";
}

// Every test leaves no mod active for the next one.
struct ModsGuard {
    ~ModsGuard() { pack_path::SetActiveMods({}); }
};

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

OBL_TEST_CASE("text_load.mod_patches_apply_in_load_order")
{
    ModsGuard guard;
    const obl::test::TempPath game("obl_text_patch");
    const auto pack = game.path / "packs" / "l0";
    Write(pack / "text/en.json",
            R"({"format":"obl.text","version":1,"language":"en","entries":[["A","base a"],["B","base b"],["C","base c"]]})");
    Write(game.path / "mods/first/text/en.patch.json", Patch(R"({"B":"first b","NEW":"first new"})"));
    Write(game.path / "mods/second/text/en.patch.json", Patch(R"({"b":"second b","C":"second c"})"));
    std::vector<text::Entry> entries;
    std::string error;

    // No active mods: the patch files are not read.
    OBL_REQUIRE(text_load::LoadPack(pack, "en", entries, error));
    OBL_CHECK_EQ(entries.size(), 3u);
    OBL_CHECK(entries[1].value == u"base b");

    pack_path::SetActiveMods({"first", "second"});
    OBL_REQUIRE(text_load::LoadPack(pack, "en", entries, error));
    OBL_REQUIRE(entries.size() == 4u);
    OBL_CHECK(entries[0].value == u"base a");
    OBL_CHECK(entries[1].value == u"second b"); // the later mod wins, and the key matches ignoring case
    OBL_CHECK(entries[2].value == u"second c");
    OBL_CHECK_EQ(entries[3].key, std::string("NEW")); // a key the base lacks is new content
    OBL_CHECK(entries[3].value == u"first new");

    pack_path::SetActiveMods({"second", "first"});
    OBL_REQUIRE(text_load::LoadPack(pack, "en", entries, error));
    OBL_CHECK(entries[1].value == u"first b");
}

OBL_TEST_CASE("text_load.mod_patch_of_a_language_the_pack_lacks_uses_the_pack_language")
{
    ModsGuard guard;
    const obl::test::TempPath game("obl_text_patch_language");
    const auto pack = game.path / "packs" / "l0";
    Write(pack / "text/en.json", R"({"format":"obl.text","version":1,"language":"en","entries":[["A","base a"],["B","base b"]]})");
    Write(game.path / "mods/fr/text/fr.patch.json", Patch(R"({"B":"bière"})"));
    pack_path::SetActiveMods({"fr"});
    std::vector<text::Entry> entries;
    std::string error;
    OBL_REQUIRE(text_load::LoadPack(pack, "fr", entries, error));
    OBL_REQUIRE(entries.size() == 2u);
    OBL_CHECK(entries[0].value == u"base a"); // untranslated keys still show text
    OBL_CHECK(entries[1].value == std::u16string(u"bi") + char16_t(0xE8) + u"re");

    // The language counts as available for the start-up selection, without a notice.
    text_load::LanguageSelection selection;
    unsigned language = 1;
    std::string notice;
    OBL_REQUIRE(selection.Select(pack, language, notice, error));
    OBL_CHECK_EQ(language, 1u);
    OBL_CHECK(notice.empty());
}

OBL_TEST_CASE("text_load.malformed_mod_patch_names_mod_file_and_key")
{
    ModsGuard guard;
    const obl::test::TempPath game("obl_text_patch_bad");
    const auto pack = game.path / "packs" / "l0";
    Write(pack / "text/en.json", R"({"format":"obl.text","version":1,"language":"en","entries":[["A","base a"]]})");
    const auto patch = game.path / "mods/bad/text/en.patch.json";
    pack_path::SetActiveMods({"bad"});
    std::vector<text::Entry> entries;
    std::string error;

    Write(patch, Patch(R"({"A":"fine","KEY":7})"));
    OBL_CHECK(!text_load::LoadPack(pack, "en", entries, error));
    OBL_CHECK(entries.empty());
    OBL_CHECK(error.find("mod bad") != std::string::npos);
    OBL_CHECK(error.find("en.patch.json") != std::string::npos);
    OBL_CHECK(error.find("key KEY") != std::string::npos);

    Write(patch, "not json");
    OBL_CHECK(!text_load::LoadPack(pack, "en", entries, error));
    OBL_CHECK(error.find("invalid JSON") != std::string::npos);

    Write(patch, R"({"format":"obl.text","version":1,"entries":{}})");
    OBL_CHECK(!text_load::LoadPack(pack, "en", entries, error));
    OBL_CHECK(error.find("unknown format") != std::string::npos);
}

OBL_TEST_CASE("text_load.presentation_mod_patch_only_adds_keys")
{
    ModsGuard guard;
    const obl::test::TempPath game("obl_text_patch_presentation");
    const auto pack = game.path / "packs" / "l0";
    Write(pack / "text/en.json", R"({"format":"obl.text","version":1,"language":"en","entries":[["A","base a"]]})");
    Write(game.path / "mods/look/text/en.patch.json", Patch(R"({"LOOK_NEW":"new"})"));
    mod_pack::ActiveMod look;
    look.manifest.id = "look";
    look.manifest.scope = mod_pack::Scope::Presentation;
    mod_pack::OverrideActive({look});
    pack_path::SetActiveMods({"look"});
    std::vector<text::Entry> entries;
    std::string error;
    OBL_CHECK(text_load::LoadPack(pack, "en", entries, error));
    OBL_CHECK_EQ(entries.size(), 2u);

    // Feedback messages keep their formatted text in the world hash: a presentation mod must not change an existing value.
    Write(game.path / "mods/look/text/en.patch.json", Patch(R"({"a":"changed"})"));
    OBL_CHECK(!text_load::LoadPack(pack, "en", entries, error));
    OBL_CHECK(error.find("look") != std::string::npos);
    OBL_CHECK(error.find("only add keys") != std::string::npos);
    mod_pack::RestoreActive();
}
