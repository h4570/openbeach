#include "game/tables/text_load.h"

#include "catalog_schemas.h"
#include "core/code_page.h"
#include "core/code_pages.h"
#include "core/json_schema.h"
#include "core/pack_path.h"
#include "formats/mod_pack.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
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

// A refused mod patch: the message already names the mod, the file and the key, so LoadPack reports it as it is.
struct PatchFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::string PatchPath(std::string_view language)
{
    return "text/" + std::string(language) + ".patch.json";
}

bool IsFile(const std::filesystem::path& file)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(file, ec);
}

// The patch files of a language, in load order: the pack's layers from the bottom up, so a later mod overrides an earlier one.
std::vector<std::filesystem::path> PatchFiles(const std::filesystem::path& pack, std::string_view language)
{
    std::vector<std::filesystem::path> files;
    const auto layers = pack_path::Layers(pack);
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        const auto file = *layer / PatchPath(language);
        if (IsFile(file)) files.push_back(file);
    }
    return files;
}

// A language is there when a full table or a mod's patch exists: a patch of a language the pack lacks still shows text.
bool HasLanguage(const std::filesystem::path& pack, std::string_view language)
{
    return IsFile(pack_path::Resolve(pack, PackPath(language))) || !PatchFiles(pack, language).empty();
}

std::string LowerKey(const std::string& key)
{
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    return lower;
}

// The mod a patch file belongs to, for messages: `<game dir>/mods/<id>/text/<lang>.patch.json` is `<id>`.
std::string ModName(const std::filesystem::path& file)
{
    return file.parent_path().parent_path().filename().string();
}

// Whether the patch comes from a presentation mod: such a patch may only add keys. Feedback messages keep their formatted text and
// the world hash covers it, so a changed value would make a replay depend on the mod.
bool OnlyAdds(const std::filesystem::path& file)
{
    const std::string mod = ModName(file);
    const auto& active = mod_pack::Active();
    return std::any_of(active.begin(), active.end(),
            [&](const mod_pack::ActiveMod& m) { return m.manifest.id == mod && m.manifest.scope == mod_pack::Scope::Presentation; });
}

void ApplyPatch(const std::filesystem::path& file, bool polish, std::vector<text::Entry>& entries, std::map<std::string, size_t>& index)
{
    const std::string where = "mod " + ModName(file) + ": " + file.string();
    const bool onlyAdds = OnlyAdds(file);
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw PatchFailure(where + ": cannot open");
    const json doc = json::parse(stream, nullptr, false);
    if (doc.is_discarded()) throw PatchFailure(where + ": invalid JSON");
    if (!doc.is_object()) throw PatchFailure(where + ": not an object");
    const auto format = doc.find("format");
    if (format == doc.end() || !format->is_string() || format->get<std::string>() != "obl.text_patch")
        throw PatchFailure(where + ": unknown format (expected obl.text_patch)");
    const auto version = doc.find("version");
    if (version == doc.end() || !version->is_number_integer() || version->get<int>() != 1)
        throw PatchFailure(where + ": unsupported version");
    const auto table = doc.find("entries");
    if (table == doc.end() || !table->is_object()) throw PatchFailure(where + ": entries is not an object");
    for (const auto& [key, value] : table->items()) {
        if (!value.is_string()) throw PatchFailure(where + ": key " + key + ": value is not a string");
        for (char c : key)
            if (static_cast<unsigned char>(c) >= 0x80) throw PatchFailure(where + ": key " + key + ": not ASCII");
        const std::u16string units = EngineValue(code_page::Utf8ToUtf16(value.get<std::string>()), polish);
        // The table looks keys up ignoring case, so a patch for "Key" replaces "KEY".
        const auto found = index.find(LowerKey(key));
        if (found != index.end()) {
            if (onlyAdds) throw PatchFailure(where + ": key " + key + ": a presentation mod may only add keys, and this one exists");
            entries[found->second].value = units;
            continue;
        }
        index[LowerKey(key)] = entries.size();
        entries.push_back({key, units});
    }
}

bool ReadBase(const std::filesystem::path& file, std::string_view language, const json_schema::Registry& schemas,
        std::vector<text::Entry>& entries)
{
    if (!IsFile(file)) return false;
    const json document = ReadDocument(file, schemas);
    const bool polish = language == "pl";
    for (const json& pair : document.at("entries")) {
        text::Entry entry;
        entry.key = AsciiKey(pair.at(0));
        entry.value = EngineValue(code_page::Utf8ToUtf16(pair.at(1).get<std::string>()), polish);
        entries.push_back(std::move(entry));
    }
    return true;
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
    const auto available = [&](unsigned candidate) { return HasLanguage(pack, LanguageCode(candidate)); };
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
    const auto patches = PatchFiles(pack, language);
    if (!IsFile(file) && patches.empty()) return true;
    try {
        json_schema::Registry schemas;
        std::string schemaError;
        for (const char* schema : catalog::kSchemas)
            if (!schemas.Add(json::parse(schema), schemaError)) throw std::runtime_error(schemaError);
        if (!ReadBase(file, language, schemas, entries)) {
            // A language only a mod has: its patches go on top of the pack's own language (else the first table in settings
            // order), so keys the mod does not translate still show text.
            std::vector<std::string_view> order;
            const std::string own = PackLanguage(pack);
            order.push_back(own);
            for (const char* candidate : kLanguages) order.push_back(candidate);
            for (std::string_view candidate : order) {
                if (candidate.empty() || candidate == language) continue;
                const auto fallback = pack_path::Resolve(pack, PackPath(candidate));
                if (ReadBase(fallback, candidate, schemas, entries)) break;
            }
        }
        std::map<std::string, size_t> index;
        // The first of equal keys is the one a lookup finds, so a patch changes that one.
        for (size_t at = 0; at < entries.size(); ++at) index.emplace(LowerKey(entries[at].key), at);
        for (const auto& patch : patches) ApplyPatch(patch, language == "pl", entries, index);
        return true;
    } catch (const PatchFailure& failure) {
        entries.clear();
        error = failure.what();
        return false;
    } catch (const std::exception& failure) {
        entries.clear();
        error = file.string() + ": " + failure.what();
        return false;
    }
}

} // namespace obl::text_load
