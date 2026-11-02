#include "importer/src/writers/gui_layout_convert.h"
#include "core/ascii_case.h"
#include "core/code_pages.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "importer/src/writers/text_convert.h"
#include <exception>
#include <fstream>
#include <map>
#include <set>
#include <vector>
namespace obl::convert {

using obl::code_pages::CodePoint;
using obl::code_pages::kUndefinedCodePoint;
namespace {

// Scripts are a few hundred kilobytes; the cap only guards against a wrong file.
constexpr size_t kMaxScriptBytes = 4u << 20;

// The layouts the engine shows and the installed script each one reads (game_states.h: GameWonGuiScript, ResetGuiScript,
// MainMenuGuiScript; world_test_state for the in-game GUI). The roles and output names are ours; the reset menu reuses the
// main menu's script and file, but the engine's reset screen takes only its Polish variant (ResetGuiScript).
struct LayoutRole {
    const char* layout;
    const char* script; // installed base name, any case
    const char* out; // output stem
    bool polishOnly; // the fallback and the Polish variant; other languages use the fallback
};
constexpr LayoutRole kRoles[] = {{"in_game", "GUI", "in_game", false}, {"intro", "Intro", "intro", false},
        {"main_menu", "MainMenu", "main_menu", false}, {"reset_menu", "MainMenu", "main_menu", true},
        {"game_won", "gamewon", "game_won", false}};

// The language order of the layouts file (text.schema.json): the fallback "*" first.
constexpr const char* kLanguages[] = {"*", "en", "fr", "it", "de", "es", "pl"};

// One installed script of a role: the base name alone is the fallback "*", `<base>_<language abbreviation>.ble` a language
// variant (LanguageOfAbbreviation); Polish files use Windows-1250.
struct LayoutChoice {
    std::string language;
    std::string file; // in the source
    std::string out;
};

constexpr const char* kHeader = "#obl-gui 1\n";

void AppendUtf8(std::string& out, char16_t unit)
{
    if (unit < 0x80) {
        out.push_back(static_cast<char>(unit));
    } else if (unit < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
        out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
        out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
    }
}

} // namespace

bool DecodeGuiLayout(std::span<const uint8_t> file, bool polish, std::u16string& units, std::string& error)
{
    if (file.empty()) {
        error = "GUI layout is empty";
        return false;
    }
    units.clear();
    units.reserve(file.size());
    for (uint8_t raw : file) {
        const unsigned byte = raw ^ 0xFFu;
        unsigned code = CodePoint(polish, byte);
        if (code == kUndefinedCodePoint) code = byte;
        units.push_back(static_cast<char16_t>(code));
    }
    return true;
}

bool ConvertGuiLayouts(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error)
{
    files = 0;
    try {
        // The installed scripts by lowercase base name and language; files of no known role or language are not layouts.
        std::map<std::string, std::map<std::string, std::string>> scripts;
        const auto dir = FindFile(source, "Data/Script/GUI");
        if (const auto entries = dir ? source.List(*dir) : std::nullopt) {
            for (const SourceEntry& entry : *entries) {
                if (entry.directory) continue;
                std::string name = entry.name;
                if (name.size() <= 4 || !ascii::EqualsNoCase(std::string_view(name).substr(name.size() - 4), ".ble")) continue;
                name.resize(name.size() - 4);
                const size_t split = name.find('_');
                std::string language = "*";
                if (split != std::string::npos) {
                    language = LanguageOfAbbreviation(std::string_view(name).substr(split + 1));
                    if (language.empty()) continue;
                    name.resize(split);
                }
                for (auto& c : name) c = ascii::Lower(c);
                scripts[name].emplace(language, *dir + "/" + entry.name);
            }
        }
        // Which choices exist in this revision; a layout without its "*" fallback is dropped rather than guessed.
        std::vector<std::pair<const char*, LayoutChoice>> present;
        for (const auto& role : kRoles) {
            std::string base = role.script;
            for (auto& c : base) c = ascii::Lower(c);
            const auto found = scripts.find(base);
            if (found == scripts.end()) continue;
            for (const char* language : kLanguages) {
                if (role.polishOnly && std::string_view(language) != "*" && std::string_view(language) != "pl") continue;
                const auto file = found->second.find(language);
                if (file == found->second.end()) continue;
                const std::string suffix = std::string(language) == "*" ? "" : std::string("_") + language;
                present.push_back({role.layout, {language, file->second, std::string(role.out) + suffix + ".gui"}});
            }
        }
        if (present.empty()) {
            error = "no GUI scripts found in installation Data/Script/GUI";
            return false;
        }
        std::set<std::string> fallbacks;
        for (const auto& [layout, choice] : present)
            if (choice.language == "*") fallbacks.insert(layout);
        nlohmann::ordered_json layouts = nlohmann::ordered_json::object();
        std::set<std::string> written;
        for (const auto& [layout, choice] : present) {
            if (!fallbacks.contains(layout)) continue;
            if (!layouts.contains(layout)) layouts[layout] = nlohmann::ordered_json::object();
            layouts[layout][choice.language] = choice.out;
            if (!written.insert(choice.out).second) continue;
            std::vector<uint8_t> bytes;
            if (!ReadBoundedFile(source, choice.file, kMaxScriptBytes, bytes, error)) return false;
            std::u16string units;
            if (!DecodeGuiLayout(bytes, choice.language == "pl", units, error)) {
                error = choice.file.substr(choice.file.rfind('/') + 1) + ": " + error;
                return false;
            }
            std::string text = kHeader;
            for (char16_t unit : units) AppendUtf8(text, unit);
            if (!out.WriteText("gui/" + choice.out, text, error)) return false;
            ++files;
        }
        if (layouts.empty()) {
            error = "no GUI layout has a fallback file";
            return false;
        }
        nlohmann::ordered_json doc = {{"format", "obl.gui_layouts"}, {"version", 1}, {"layouts", layouts}};
        if (!formats::ValidateDocument(schemas, "obl.gui_layouts", doc, error)) return false;
        if (!out.WriteText("gui/layouts.json", doc.dump(2) + "\n", error)) return false;
        ++files;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace obl::convert
