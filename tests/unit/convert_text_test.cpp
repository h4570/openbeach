#include "unit_test.h"
#include "core/code_page.h"
#include "core/json_schema.h"
#include "game/tables/text_load.h"
#include "game/tables/text_table.h"
#include "core/code_pages.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "importer/src/writers/text_convert.h"
#include "temp_dir.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace obl::convert;
using obl::code_pages::CodePoint;
using obl::code_pages::kUndefinedCodePoint;

namespace {

// The installation's form of a text file: every byte XOR 0xFF, UTF-16LE.
std::vector<uint8_t> Encode(std::u16string_view units)
{
    std::vector<uint8_t> bytes;
    for (char16_t unit : units) {
        bytes.push_back(static_cast<uint8_t>((unit & 0xFF) ^ 0xFF));
        bytes.push_back(static_cast<uint8_t>((unit >> 8) ^ 0xFF));
    }
    return bytes;
}

// A whole table: the BOM the files start with, then the text. Units above 0x7F are written numerically (char16_t), not as escapes.
std::u16string Table(std::u16string_view text)
{
    std::u16string units{char16_t(0xFEFF)};
    units += text;
    return units;
}

// One `"key" "value"` pair; the value is UTF-16 as the file holds it.
std::u16string Pair(std::u16string_view key, std::u16string_view value)
{
    return u"\"" + std::u16string(key) + u"\" \"" + std::u16string(value) + u"\"\n";
}

obl::json_schema::Registry Schemas()
{
    auto dir = obl::formats::FindSchemaDirectory(std::filesystem::current_path());
    OBL_REQUIRE(dir);
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadSchemaDirectory(*dir, schemas, error));
    return schemas;
}

// The engine's own table of the same bytes: its walk and the modulus is any prime (slots do not change the entries).
std::vector<obl::text::Entry> EngineEntries(std::span<const uint8_t> bytes)
{
    std::u16string units;
    for (size_t i = 0; i + 1 < bytes.size(); i += 2)
        units.push_back(static_cast<char16_t>((bytes[i] ^ 0xFF) | ((bytes[i + 1] ^ 0xFF) << 8)));
    return obl::text::Parse(units.data(), static_cast<int>(units.size()), 4507).entries;
}

// The engine's units as the pack gives them back (text_load.h EngineValue): a unit above 0xFF that the language's code page has is its
// byte. The Polish table keeps 17 such units; the converter writes them with the same code point as their byte, so both fold to the byte.
std::u16string FoldedUnits(const std::u16string& units, bool polish)
{
    std::u16string folded;
    for (char16_t unit : units) {
        uint8_t byte = 0;
        folded.push_back(unit > 0xFF && obl::code_pages::ByteOf(polish, unit, byte) ? static_cast<char16_t>(byte) : unit);
    }
    return folded;
}

// The JSON that ConvertText writes for one table, parsed back.
nlohmann::json ConvertedJson(const std::string& language, std::span<const uint8_t> bytes)
{
    const auto schemas = Schemas();
    std::string json, error;
    ConvertedFileSink sink = [&](const std::string& path, std::span<const uint8_t> written, std::string&) {
        OBL_REQUIRE(path == "text/" + language + ".json");
        json.assign(reinterpret_cast<const char*>(written.data()), written.size());
        return true;
    };
    OBL_REQUIRE(ConvertText(language, bytes, schemas, sink, error));
    return nlohmann::json::parse(json);
}

} // namespace

OBL_TEST_CASE("convert.text_walk_matches_engine_parse")
{
    // A comment quote takes part in the pairing (the walk does not read lines), a duplicate key stays, and a last key without
    // a value is dropped.
    const auto units = Table(u"// \"ignored\"\n"
                             u"\"KEY_ONE\" \"value one\"\n"
                             u"\"KEY_TWO\" \"value two\"\n"
                             u"\"KEY_ONE\" \"duplicate\"\n"
                             u"\"LAST\"\n"
                             u"\"TAIL\"\n");
    const auto bytes = Encode(units);
    std::vector<TextEntry> entries;
    std::string error;
    OBL_REQUIRE(ParseTextTable(bytes, entries, error));
    const auto engine = EngineEntries(bytes);
    OBL_REQUIRE(entries.size() == engine.size());
    OBL_CHECK(entries.size() == 4);
    for (size_t i = 0; i < entries.size(); ++i) {
        OBL_CHECK(entries[i].key == engine[i].key);
        OBL_CHECK(entries[i].value == engine[i].value);
    }
    OBL_CHECK(entries[3].key == "duplicate");
}

OBL_TEST_CASE("convert.text_walk_nul_and_unterminated")
{
    // A value stops at its first NUL unit; an unterminated value runs to the end of the file.
    std::u16string nul = u"\"NUL\" \"ab";
    nul.push_back(char16_t(0));
    nul += u"cd\"\n\"OPEN\" \"tail";
    const auto units = Table(nul);
    const auto bytes = Encode(units);
    std::vector<TextEntry> entries;
    std::string error;
    OBL_REQUIRE(ParseTextTable(bytes, entries, error));
    const auto engine = EngineEntries(bytes);
    OBL_REQUIRE(entries.size() == engine.size());
    OBL_REQUIRE(entries.size() == 2);
    for (size_t i = 0; i < entries.size(); ++i) {
        OBL_CHECK(entries[i].key == engine[i].key);
        OBL_CHECK(entries[i].value == engine[i].value);
    }
    OBL_CHECK(entries[0].value == u"ab");
    OBL_CHECK(entries[1].value == u"tail");
}

OBL_TEST_CASE("convert.text_duplicate_keys_keep_file_order")
{
    const auto bytes = Encode(Table(Pair(u"K", u"first") + Pair(u"K", u"second")));
    std::vector<TextEntry> entries;
    std::string error;
    OBL_REQUIRE(ParseTextTable(bytes, entries, error));
    OBL_REQUIRE(entries.size() == 2);
    OBL_CHECK(entries[0].value == u"first");
    OBL_CHECK(entries[1].value == u"second");
}

OBL_TEST_CASE("convert.text_code_page_values")
{
    // Units up to 0xFF are code-page bytes: 0xB9 is U+0105 in Polish (Windows-1250) but U+00B9 in Western (Windows-1252);
    // 0x92 is U+2019 in Windows-1252; an undefined byte keeps its C1 code point; higher units and surrogate pairs pass through.
    auto value = [](const std::string& language, std::u16string_view units) {
        return ConvertedJson(language, Encode(Table(Pair(u"K", units))))["entries"][0][1].get<std::string>();
    };
    OBL_CHECK(value("pl", std::u16string(1, char16_t(0xB9))) == std::string("\xC4\x85"));
    OBL_CHECK(value("fr", std::u16string(1, char16_t(0xB9))) == std::string("\xC2\xB9"));
    OBL_CHECK(value("fr", std::u16string(1, char16_t(0x92))) == std::string("\xE2\x80\x99"));
    OBL_CHECK(value("fr", std::u16string(1, char16_t(0x81))) == std::string("\xC2\x81"));
    OBL_CHECK(value("pl", std::u16string(1, char16_t(0x0142))) == std::string("\xC5\x82"));
    OBL_CHECK(value("fr", std::u16string(1, char16_t(0x0041)) + std::u16string(1, char16_t(0x00E9))) == std::string("A\xC3\xA9"));
    OBL_CHECK(value("fr", std::u16string{char16_t(0xD83D), char16_t(0xDE00)}) == std::string("\xF0\x9F\x98\x80"));
    OBL_CHECK(obl::code_pages::CodePoint(true, 0xB9) == 0x0105);
    OBL_CHECK(obl::code_pages::CodePoint(false, 0x92) == 0x2019);
    OBL_CHECK(obl::code_pages::CodePoint(false, 0x81) == obl::code_pages::kUndefinedCodePoint);
}

OBL_TEST_CASE("convert.text_rejects_bad_input")
{
    auto convert = [](const std::vector<uint8_t>& bytes) {
        std::string json, error;
        ConvertedFileSink sink = [&](const std::string&, std::span<const uint8_t> written, std::string&) {
            json.assign(reinterpret_cast<const char*>(written.data()), written.size());
            return true;
        };
        return ConvertText("fr", bytes, Schemas(), sink, error);
    };
    OBL_CHECK(!convert({}));
    OBL_CHECK(!convert({0xFF}));
    OBL_CHECK(!convert(Encode(Table(u"// nothing here\n"))));
    std::u16string key = u"\"";
    key.push_back(char16_t(0x00E9));
    key += u"\" \"v\"";
    OBL_CHECK(!convert(Encode(Table(key))));
    std::u16string lone = u"\"K\" \"";
    lone.push_back(char16_t(0xD800));
    lone += u"\"";
    OBL_CHECK(!convert(Encode(Table(lone))));
}

OBL_TEST_CASE("data.convert_texts_of_installation")
{
    // The tables the installation has (a revision may lack some); the JSON is compared with the engine's own table of the same file.
    const std::map<std::string, std::string> files = {{"text_french.ble", "fr"}, {"text_italian.ble", "it"}, {"text_german.ble", "de"},
            {"text_spanish.ble", "es"}, {"text_polish.ble", "pl"}, {"text_uk.ble", "en"}};
    int present = 0;
    for (const auto& [file, language] : files) {
        const auto path = FindInstalledFile(OBL_GAME_DIR, "Data/" + file);
        if (!path) continue;
        ++present;
        std::ifstream in(*path, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        OBL_REQUIRE(!bytes.empty());
        std::vector<TextEntry> entries;
        std::string error;
        OBL_REQUIRE(ParseTextTable(bytes, entries, error));
        const auto engine = EngineEntries(bytes);
        OBL_REQUIRE(entries.size() == engine.size());
        const auto json = ConvertedJson(language, bytes);
        OBL_REQUIRE(json["entries"].size() == engine.size());
        const bool polish = language == "pl";
        for (size_t i = 0; i < engine.size(); ++i) {
            const auto& pair = json["entries"][i];
            OBL_CHECK(pair[0].get<std::string>() == engine[i].key);
            std::u16string expected;
            for (char16_t unit : engine[i].value) {
                unsigned code = unit;
                if (unit <= 0xFF && obl::code_pages::CodePoint(polish, unit) != obl::code_pages::kUndefinedCodePoint)
                    code = obl::code_pages::CodePoint(polish, unit);
                expected.push_back(static_cast<char16_t>(code));
            }
            OBL_CHECK(obl::code_page::Utf8ToUtf16(pair[1].get<std::string>()) == expected);
        }
    }
    if (present == 0) OBL_SKIP("no installation text tables");
}

OBL_TEST_CASE("data.pack_texts_match_engine_table")
{
    // The converted pack's table of every installed text file, read back by the engine's loader (text_load.h), is the engine's own table
    // (units above 0xFF folded to their code-page bytes, FoldedUnits).
    const std::map<std::string, std::string> files = {{"text_french.ble", "fr"}, {"text_italian.ble", "it"}, {"text_german.ble", "de"},
            {"text_spanish.ble", "es"}, {"text_polish.ble", "pl"}, {"text_uk.ble", "en"}};
    int present = 0;
    for (const auto& [file, language] : files) {
        const auto path = FindInstalledFile(OBL_GAME_DIR, "Data/" + file);
        if (!path) continue;
        ++present;
        std::ifstream in(*path, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        OBL_REQUIRE(!bytes.empty());
        const obl::test::TempPath dir("obl_pack_texts");
        std::filesystem::create_directories(dir.path / "text");
        std::ofstream(dir.path / "text" / (language + ".json"), std::ios::binary) << ConvertedJson(language, bytes).dump();
        std::vector<obl::text::Entry> loaded;
        std::string error;
        OBL_REQUIRE(obl::text_load::LoadPack(dir.path, language, loaded, error));
        const auto engine = EngineEntries(bytes);
        OBL_REQUIRE(loaded.size() == engine.size());
        const bool polish = language == "pl";
        for (size_t i = 0; i < engine.size(); ++i) {
            OBL_CHECK(loaded[i].key == engine[i].key);
            OBL_CHECK(loaded[i].value == FoldedUnits(engine[i].value, polish));
        }
    }
    if (present == 0) OBL_SKIP("no installation text tables");
}

OBL_TEST_CASE("convert.texts_installation_paths_and_failures")
{
    const obl::test::TempPath tmp("obl-convert-text-unit");
    const auto& root = tmp.path;
    std::filesystem::create_directories(root / "empty");
    int files = -1;
    std::string error;
    const auto schemas = Schemas();
    OBL_CHECK(!ConvertTexts(
            *obl::convert::OpenDirectorySource(root / "empty"), *obl::convert::OpenDirectorySink(root / "out"), schemas, files, error));
    OBL_CHECK(files == 0);
    OBL_CHECK(!error.empty());
}

OBL_TEST_CASE("convert.choose_language_takes_the_ini_then_the_preferred_then_the_first_table")
{
    const obl::test::TempPath root("obl-choose-language");
    std::filesystem::create_directories(root.path / "Data");
    const auto choose = [&](std::string_view preferred) {
        return ChooseLanguage(*obl::convert::OpenDirectorySource(root.path), preferred);
    };
    OBL_CHECK(choose("en").empty()); // no table at all
    std::ofstream(root.path / "Data" / "text_polish.ble", std::ios::binary) << "x";
    std::ofstream(root.path / "Data" / "text_french.ble", std::ios::binary) << "x";
    OBL_CHECK(choose("en") == "fr"); // a copy without English: the first table of the order
    OBL_CHECK(choose("xx") == "fr");
    OBL_CHECK(choose("pl") == "pl");
    std::ofstream(root.path / "BeachLife.ini", std::ios::binary) << "[Language]\nLanguage=3\n";
    OBL_CHECK(choose("pl") == "pl"); // the INI's German has no table
    std::ofstream(root.path / "BeachLife.ini", std::ios::binary) << "[Language]\nLanguage=1\n";
    OBL_CHECK(choose("pl") == "fr");
}

OBL_TEST_CASE("convert.installed_language_reads_the_game_ini")
{
    const obl::test::TempPath root("obl-installed-language");
    std::filesystem::create_directories(root.path);
    OBL_CHECK(InstalledLanguage(*obl::convert::OpenDirectorySource(root.path)).empty()); // no BeachLife.ini
    const auto write = [&](const char* text) { std::ofstream(root.path / "BeachLife.ini", std::ios::binary) << text; };
    write("[Video]\r\nLanguage=1\r\n[Language]\r\nLanguage=5\r\n");
    OBL_CHECK(InstalledLanguage(*obl::convert::OpenDirectorySource(root.path)) == "pl");
    write("[Language]\nLanguage=0\n");
    OBL_CHECK(InstalledLanguage(*obl::convert::OpenDirectorySource(root.path)) == "en");
    write("[Language]\r\nLanguage=6\r\n");
    OBL_CHECK(InstalledLanguage(*obl::convert::OpenDirectorySource(root.path)).empty());
    write("[Language]\nLanguage=0garbage\n");
    OBL_CHECK(InstalledLanguage(*obl::convert::OpenDirectorySource(root.path)).empty());
}
