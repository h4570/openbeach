#include "importer/src/writers/text_convert.h"
#include "core/code_page.h"
#include "core/code_pages.h"
#include "core/ascii_case.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include <charconv>
#include <exception>
#include <filesystem>
#include <sstream>
#include <fstream>
namespace obl::convert {

using obl::code_pages::CodePoint;
using obl::code_pages::kUndefinedCodePoint;
namespace {

// The engine's text table holds this many pairs (text::kMaxPairs); the current reader reads no more.
constexpr size_t kMaxPairs = 4500;
constexpr size_t kMaxFileBytes = 16u << 20;

struct TableFile {
    const char* file;
    const char* language;
};
// The game's language order; Windows-1250 applies to "pl" alone (text.schema.json, assets.md "Texts").
constexpr TableFile kTableFiles[] = {{"text_uk.ble", "en"}, {"text_french.ble", "fr"}, {"text_italian.ble", "it"},
        {"text_german.ble", "de"}, {"text_spanish.ble", "es"}, {"text_polish.ble", "pl"}};

// The file as UTF-16 units: each byte is XOR-ed with 0xFF, then pairs are read little-endian.
bool DecodeUnits(std::span<const uint8_t> file, std::u16string& units, std::string& error)
{
    if (file.empty() || file.size() % 2 != 0) {
        error = "text table is empty or has an odd size";
        return false;
    }
    units.resize(file.size() / 2);
    for (size_t i = 0; i < units.size(); ++i) {
        const auto lo = static_cast<char16_t>(file[2 * i] ^ 0xFF), hi = static_cast<char16_t>(file[2 * i + 1] ^ 0xFF);
        units[i] = static_cast<char16_t>(lo | (hi << 8));
    }
    return true;
}

// The units [start, end) up to the first NUL, as the engine's Narrow and value copy stop there.
std::u16string Field(const std::u16string& units, int start, int end)
{
    std::u16string field;
    for (int i = start; i < end && units[i] != 0; ++i) field.push_back(units[i]);
    return field;
}

bool ToAscii(const std::u16string& text, std::string& out)
{
    out.clear();
    for (char16_t unit : text) {
        if (unit >= 0x80) return false;
        out.push_back(static_cast<char>(unit));
    }
    return true;
}

// Unpaired surrogates have no UTF-8 form; the converter refuses them instead of replacing them.
bool IsWellFormedUtf16(const std::u16string& text)
{
    for (size_t i = 0; i < text.size(); ++i) {
        const char16_t unit = text[i];
        if (unit >= 0xDC00 && unit <= 0xDFFF) return false;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 1 >= text.size() || text[i + 1] < 0xDC00 || text[i + 1] > 0xDFFF) return false;
            ++i;
        }
    }
    return true;
}

} // namespace

bool ParseTextTable(std::span<const uint8_t> file, std::vector<TextEntry>& entries, std::string& error)
{
    std::u16string units;
    if (!DecodeUnits(file, units, error)) return false;
    entries.clear();
    const int count = static_cast<int>(units.size());
    auto at = [&](int i) -> char16_t { return i >= 0 && i < count ? units[i] : u'\0'; };
    // The engine's walk (text_table.cpp, Parse): a quote after the unit at `pos` opens a string that ends at the next quote,
    // or at the end of the file. Strings alternate key and value, so quotes inside comments take part; the walk starts at
    // unit 0, which is never a quote (the BOM).
    bool haveKey = false;
    std::string key;
    for (int pos = 0; pos < count - 1;) {
        if (at(pos + 1) != u'"') {
            ++pos;
            continue;
        }
        const int start = pos + 2;
        int end = start;
        while (end < count && units[end] != u'"') ++end;
        if (!haveKey) {
            if (!ToAscii(Field(units, start, end), key)) {
                error = "text key is not ASCII";
                return false;
            }
            haveKey = true;
        } else {
            entries.push_back({key, Field(units, start, end)});
            haveKey = false;
            if (entries.size() >= kMaxPairs) break;
        }
        pos = end;
    }
    return true;
}

bool ConvertText(const std::string& language, std::span<const uint8_t> file, const json_schema::Registry& schemas,
        const ConvertedFileSink& sink, std::string& error)
{
    std::vector<TextEntry> entries;
    if (!ParseTextTable(file, entries, error)) return false;
    if (entries.empty()) {
        error = "text table has no pairs";
        return false;
    }
    const bool polish = language == "pl";
    auto list = nlohmann::ordered_json::array();
    for (const auto& entry : entries) {
        std::u16string value;
        value.reserve(entry.value.size());
        for (char16_t unit : entry.value) {
            unsigned code = unit;
            if (unit <= 0xFF) {
                code = CodePoint(polish, unit);
                // Undefined bytes keep their C1 code point (U+0080..U+009F), as code_page.h does for Windows-1252.
                if (code == kUndefinedCodePoint) code = unit;
            }
            value.push_back(static_cast<char16_t>(code));
        }
        if (!IsWellFormedUtf16(value)) {
            error = "text value of " + entry.key + " has an unpaired surrogate";
            return false;
        }
        list.push_back({entry.key, code_page::Utf16ToUtf8(value)});
    }
    nlohmann::ordered_json doc = {{"format", "obl.text"}, {"version", 1}, {"language", language}, {"entries", list}};
    if (!formats::ValidateDocument(schemas, "obl.text", doc, error)) return false;
    const auto text = doc.dump(2) + "\n";
    return sink("text/" + language + ".json", std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()), error);
}

bool ConvertTexts(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error)
{
    files = 0;
    try {
        ConvertedFileSink sink = [&](const std::string& name, std::span<const uint8_t> bytes, std::string& why) {
            if (!out.Write(name, bytes, why)) return false;
            ++files;
            return true;
        };
        int converted = 0;
        for (const auto& table : kTableFiles) {
            const auto path = FindFile(source, std::string("Data/") + table.file);
            if (!path) continue;
            std::vector<uint8_t> bytes;
            if (!ReadBoundedFile(source, *path, kMaxFileBytes, bytes, error)) return false;
            if (!ConvertText(table.language, bytes, schemas, sink, error)) {
                error = std::string(table.file) + ": " + error;
                return false;
            }
            ++converted;
        }
        if (converted == 0) {
            error = "no text tables found in installation Data/";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::string InstalledLanguage(const ByteSource& source)
{
    const auto bytes = source.ReadAll("BeachLife.ini");
    if (!bytes) return {};
    std::istringstream in(std::string(bytes->begin(), bytes->end()));
    std::string line;
    bool section = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with('[')) {
            section = line == "[Language]";
        } else if (section && line.starts_with("Language=")) {
            const std::string value = line.substr(9);
            int index = -1;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), index);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || index < 0 ||
                    index >= static_cast<int>(std::size(kTableFiles)))
                return {};
            return kTableFiles[index].language;
        }
    }
    return {};
}

bool HasTextTable(const ByteSource& source, std::string_view language)
{
    for (const auto& table : kTableFiles)
        if (language == table.language) return FindFile(source, std::string("Data/") + table.file).has_value();
    return false;
}

std::string ChooseLanguage(const ByteSource& source, std::string_view preferred)
{
    if (std::string installed = InstalledLanguage(source); !installed.empty() && HasTextTable(source, installed)) return installed;
    if (HasTextTable(source, preferred)) return std::string(preferred);
    for (const auto& table : kTableFiles)
        if (HasTextTable(source, table.language)) return table.language;
    return {};
}

std::string LanguageOfAbbreviation(std::string_view abbreviation)
{
    if (abbreviation.size() < 3) return {};
    for (const auto& table : kTableFiles) {
        std::string_view word = table.file;
        word = word.substr(5, word.size() - 5 - 4); // text_<word>.ble
        if (abbreviation.size() <= word.size() && ascii::EqualsNoCase(abbreviation, word.substr(0, abbreviation.size())))
            return table.language;
    }
    return {};
}

} // namespace obl::convert
