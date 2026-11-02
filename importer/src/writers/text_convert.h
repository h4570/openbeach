#pragma once
#include "importer/src/writers/sprite_convert.h"
namespace obl::convert {

// One pair of the table: the key is ASCII, the value stays UTF-16 as the file has it (code-page bytes not yet mapped).
struct TextEntry {
    std::string key;
    std::u16string value;
};

// The effective table of a text file (`Data/text_<language>.ble`): the same walk as the engine's text table loader, in file
// order with duplicates kept, at most as many pairs as the current reader builds. False on an empty or odd-sized file and on a
// key that is not ASCII.
bool ParseTextTable(std::span<const uint8_t> file, std::vector<TextEntry>& entries, std::string& error);

// `text/<language>.json` (obl.text) for one table, validated before the sink gets it. Values with units up to 0xFF are code-page
// bytes (Windows-1250 for "pl", Windows-1252 otherwise); undefined bytes keep their C1 code point; higher units pass through.
bool ConvertText(const std::string& language, std::span<const uint8_t> file, const json_schema::Registry& schemas,
        const ConvertedFileSink& sink, std::string& error);

// Converts every text table of the installation that exists (the revisions differ, sources.md); none is an error.
bool ConvertTexts(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error);

// The language the installation's game runs in: `[Language] Language=<n>` of `BeachLife.ini` at the root of `source`, an index into the game's
// table order (en, fr, it, de, es, pl). Empty without the file, the key or a known index.
std::string InstalledLanguage(const ByteSource& source);

// True when the installation has the text table of `language` (a code of the table order above).
bool HasTextTable(const ByteSource& source, std::string_view language);

// The language of a pack whose player chooses after the import: the first of InstalledLanguage, `preferred` and the table
// order whose table the installation has. Empty when it has no table.
std::string ChooseLanguage(const ByteSource& source, std::string_view preferred);

// The language whose text table word (`french` of `text_french.ble`) starts with `abbreviation`, in any case and at least three
// letters long (`fre`, `Polish`); empty if none. Installed files name their language variants this way (`MainMenu_ger.ble`).
std::string LanguageOfAbbreviation(std::string_view abbreviation);

} // namespace obl::convert
