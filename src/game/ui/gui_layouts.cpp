#include "game/ui/gui_layouts.h"
#include "catalog_schemas.h"
#include "core/code_page.h"
#include "core/code_pages.h"
#include "core/json_schema.h"
#include "core/pack_path.h"

#include <fstream>
#include <iterator>
#include <utility>

namespace obl::gui_layouts {
namespace {
using nlohmann::json;

// The first line of every converted layout (the converter writes it, the reader checks it).
constexpr std::string_view kHeader = "#obl-gui 1\n";
constexpr std::string_view kFallback = "*";
// The one language whose choice is a Windows-1250 file (the Polish scripts).
constexpr std::string_view kPolish = "pl";

bool ReadFile(const std::filesystem::path& path, std::string& text, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path.string();
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

bool LoadLayouts(const std::filesystem::path& pack, Layouts& out, std::string& error)
{
    error.clear();
    const auto file = pack_path::Resolve(pack, "gui/layouts.json");
    std::string text;
    if (!ReadFile(file, text, error)) return false;
    const json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded()) {
        error = file.string() + ": invalid JSON";
        return false;
    }
    json_schema::Registry schemas;
    for (const char* schema : catalog::kSchemas)
        if (!schemas.Add(json::parse(schema), error)) return false;
    const auto errors = schemas.Validate("obl.gui_layouts", doc);
    if (!errors.empty()) {
        error = file.string() + ": " + errors.front();
        return false;
    }
    Layouts result;
    for (const auto& [layout, languages] : doc.at("layouts").items()) {
        for (const auto& [language, name] : languages.items())
            result[layout].emplace(language, Choice{name.get<std::string>(), language == kPolish});
    }
    out = std::move(result);
    return true;
}

std::string_view LanguageCode(int language)
{
    switch (language) {
    case 1: return "fr";
    case 2: return "it";
    case 3: return "de";
    case 4: return "es";
    case 5: return "pl";
    default: return kFallback;
    }
}

const Choice* Resolve(const Layouts& layouts, std::string_view layout, int language)
{
    const auto found = layouts.find(layout);
    if (found == layouts.end()) return nullptr;
    const auto own = found->second.find(LanguageCode(language));
    if (own != found->second.end()) return &own->second;
    const auto fallback = found->second.find(kFallback);
    return fallback == found->second.end() ? nullptr : &fallback->second;
}

const Choice* FindFile(const Layouts& layouts, std::string_view file)
{
    for (const auto& [layout, choices] : layouts)
        for (const auto& [language, choice] : choices)
            if (choice.file == file) return &choice;
    return nullptr;
}

bool ReadText(const std::filesystem::path& pack, const Choice& choice, std::string& bytes, std::string& error)
{
    error.clear();
    const auto file = pack_path::Resolve(pack, std::filesystem::path("gui") / choice.file);
    std::string text;
    if (!ReadFile(file, text, error)) return false;
    if (text.compare(0, kHeader.size(), kHeader) != 0) {
        error = file.string() + ": not a converted GUI layout (missing #obl-gui 1)";
        return false;
    }
    const std::u16string units = code_page::Utf8ToUtf16(std::string_view(text).substr(kHeader.size()));
    if (!code_pages::Encode(choice.polish, units, bytes)) {
        error = file.string() + ": a character the code page does not have";
        return false;
    }
    return true;
}

} // namespace obl::gui_layouts
