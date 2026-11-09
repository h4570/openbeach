#pragma once

#include "game/tables/text_table.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The language tables of a converted pack (`text/<language>.json`, obl.text, assets.md "Texts"): what the engine's text table reads
// instead of the installed `text_<language>.ble`. The converter maps the table's code-page bytes to Unicode; the game draws a value
// by the unit (the font's glyph index), so the engine gets the bytes back.
namespace obl::text_load {

// The settings language indices are 0 .. kLanguageCount - 1.
inline constexpr unsigned kLanguageCount = 6;

// Convert the settings language index to en, fr, it, de, es or pl; unsupported indices fall back to English.
const char* LanguageCode(unsigned index);

// Startup selection: keep an imported language, otherwise use the first available language in settings order.
// The notice is emitted once per selection state (one game process). No tables is an error, not an empty UI.
struct LanguageSelection {
    bool notified = false;
    // `preferred` (a language code, e.g. PackLanguage) is tried first when the requested table is missing.
    bool Select(
            const std::filesystem::path& pack, unsigned& index, std::string& notice, std::string& error, std::string_view preferred = {});
};

// The `language` of `<pack>/pack.json` (the language the pack was imported in); empty when unreadable.
std::string PackLanguage(const std::filesystem::path& pack);

// The pack-relative file of a language's table: `text/<language>.json`.
std::string PackPath(std::string_view language);

// One converted value in the engine's units: ASCII stays, a code point of the language's code page (Windows-1250 for "pl",
// Windows-1252 otherwise) becomes its byte, the C1 code point of an undefined byte stays, and any other unit passes through. The Polish
// table has 17 units above 0xFF: the converter writes them with the code point of their byte, so they fold to that byte as well.
std::u16string EngineValue(std::u16string_view value, bool polish);

// Reads `<pack>/text/<language>.json` (validated against obl.text) into `entries` in file order, with the engine's units. A pack
// without this language's file gives no entries and no error, as an empty table is given for a missing file. An invalid
// file fails, with its name and the reason in `error`.
bool LoadPack(const std::filesystem::path& pack, std::string_view language, std::vector<text::Entry>& entries, std::string& error);

} // namespace obl::text_load
