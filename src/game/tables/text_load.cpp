#include "game/tables/text_load.h"

#include "catalog_schemas.h"
#include "core/code_page.h"
#include "core/code_pages.h"
#include "core/json_schema.h"
#include "core/pack_path.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace obl::text_load {
namespace {
using nlohmann::json;

// Text table selection follows the settings language order: English, French, Italian, German, Spanish and Polish.
constexpr const char* kLanguages[] = {"en", "fr", "it", "de", "es", "pl"};
static_assert(std::size(kLanguages) == kLanguageCount);

// The key of a pair is an identifier: ASCII, as the engine's table keys are (text_table.h).
std::string AsciiKey(const json& key)
{
    const std::string text = key.get<std::string>();
    for (char c : text) {
        if (static_cast<unsigned char>(c) >= 0x80) throw std::runtime_error("key " + text + " is not ASCII");
    }
    return text;
}

json ReadDocument(const std::filesystem::path& path, const json_schema::Registry& schemas)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path.string());
    json doc = json::parse(file, nullptr, false);
    if (doc.is_discarded()) throw std::runtime_error(path.string() + ": invalid JSON");
    const auto errors = schemas.Validate("obl.text", doc);
    if (!errors.empty()) throw std::runtime_error(path.string() + ": " + errors.front());
    return doc;
}

} // namespace

const char* LanguageCode(unsigned index)
{
    return kLanguages[index < std::size(kLanguages) ? index : 0];
}

std::string PackPath(std::string_view language)
{
    return "text/" + std::string(language) + ".json";
}

std::string PackLanguage(const std::filesystem::path& pack)
{
    std::ifstream file(pack / "pack.json", std::ios::binary);
    if (!file) return {};
    const json doc = json::parse(file, nullptr, false);
    if (!doc.is_object()) return {};
    const auto it = doc.find("language");
    return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool LanguageSelection::Select(
        const std::filesystem::path& pack, unsigned& index, std::string& notice, std::string& error, std::string_view preferred)
{
    notice.clear();
    error.clear();
    const std::string requested = LanguageCode(index);
    const auto available = [&](unsigned candidate) {
        return std::filesystem::is_regular_file(pack_path::Resolve(pack, PackPath(LanguageCode(candidate))));
    };
    if (available(index)) return true;
    // The pack's own language comes first, so a copy without the requested table still gets the language it was imported in.
    unsigned order[std::size(kLanguages)];
    unsigned count = 0;
    for (unsigned candidate = 0; candidate < std::size(kLanguages); ++candidate)
        if (!preferred.empty() && preferred == kLanguages[candidate]) order[count++] = candidate;
    for (unsigned candidate = 0; candidate < std::size(kLanguages); ++candidate)
        if (preferred != kLanguages[candidate]) order[count++] = candidate;
    for (unsigned at = 0; at < count; ++at) {
        const unsigned candidate = order[at];
        if (!available(candidate)) continue;
        index = candidate;
        if (!notified) {
            notice = "The selected language (" + requested + ") was not imported from your copy of Beach Life. Using " +
                    LanguageCode(candidate) + " instead.";
            notified = true;
        }
        return true;
    }
    error = "No imported language tables in " + pack.string() + ". Import the game data again";
    return false;
}

std::u16string EngineValue(std::u16string_view value, bool polish)
{
    std::u16string units;
    units.reserve(value.size());
    for (char16_t unit : value) {
        // A unit no byte has stays as it is (a Unicode unit above 0xFF); the C1 code point of an undefined byte maps to itself.
        uint8_t byte = 0;
        units.push_back(unit >= 0x80 && code_pages::ByteOf(polish, unit, byte) ? static_cast<char16_t>(byte) : unit);
    }
    return units;
}

bool LoadPack(const std::filesystem::path& pack, std::string_view language, std::vector<text::Entry>& entries, std::string& error)
{
    error.clear();
    entries.clear();
    const auto file = pack_path::Resolve(pack, PackPath(language));
    if (!std::filesystem::exists(file)) return true;
    try {
        json_schema::Registry schemas;
        std::string schemaError;
        for (const char* schema : catalog::kSchemas)
            if (!schemas.Add(json::parse(schema), schemaError)) throw std::runtime_error(schemaError);
        const json document = ReadDocument(file, schemas);
        const bool polish = language == "pl";
        for (const json& pair : document.at("entries")) {
            text::Entry entry;
            entry.key = AsciiKey(pair.at(0));
            entry.value = EngineValue(code_page::Utf8ToUtf16(pair.at(1).get<std::string>()), polish);
            entries.push_back(std::move(entry));
        }
        return true;
    } catch (const std::exception& failure) {
        entries.clear();
        error = file.string() + ": " + failure.what();
        return false;
    }
}

} // namespace obl::text_load
