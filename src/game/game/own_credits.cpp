#include "game/game/own_credits.h"
#include "game/tables/text_load.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <string>

namespace obl::own_credits {

namespace {

// The languages of text_load::LanguageCode, in the order of Row::text.
constexpr std::string_view kLanguages[] = {"en", "fr", "it", "de", "es", "pl"};
constexpr size_t kLanguageCount = std::size(kLanguages);

struct Row {
    std::array<const char16_t*, kLanguageCount> text; // per kLanguages; names repeat in every language
    int font; // 2: large title font
    uint32_t color; // 0x00RRGGBB; role headings red like the converted roll's
    bool longSpacing; // the last row of a group
};

constexpr const char16_t* kName = u"Sandro Sobczy\x0144ski (h4570)";

constexpr Row kRows[] = {
        {{u"OpenBeach", u"OpenBeach", u"OpenBeach", u"OpenBeach", u"OpenBeach", u"OpenBeach"}, 2, 0x00000000, true},
        {{u"Project Lead", u"Chef de projet", u"Capo progetto", u"Projektleitung", u"Jefe de proyecto", u"Kierownik Projektu"}, 0,
                0x00ff0000, false},
        {{kName, kName, kName, kName, kName, kName}, 0, 0x00000000, true},
        {{u"Programming", u"Programmation", u"Programmazione", u"Programmierung", u"Programaci\x00f3n", u"Programowanie"}, 0, 0x00ff0000,
                false},
        {{u"Claude 5.5 family (Haiku, Sonnet, Opus)", u"Famille Claude 5.5 (Haiku, Sonnet, Opus)",
                 u"Famiglia Claude 5.5 (Haiku, Sonnet, Opus)", u"Claude-5.5-Familie (Haiku, Sonnet, Opus)",
                 u"Familia Claude 5.5 (Haiku, Sonnet, Opus)", u"Rodzina Claude 5.5 (Haiku, Sonnet, Opus)"},
                0, 0x00000000, false},
        {{u"GPT-6 family (Sol, Astra)", u"Famille GPT-6 (Sol, Astra)", u"Famiglia GPT-6 (Sol, Astra)", u"GPT-6-Familie (Sol, Astra)",
                 u"Familia GPT-6 (Sol, Astra)", u"Rodzina GPT-6 (Sol, Astra)"},
                0, 0x00000000, true},
};

size_t LanguageIndex(std::string_view language)
{
    for (size_t i = 0; i < kLanguageCount; ++i)
        if (kLanguages[i] == language) return i;
    return 0; // English, as text_load falls back to it
}

} // namespace

std::vector<gui_content::Credit> Merge(const std::vector<gui_content::Credit>& credits)
{
    std::vector<gui_content::Credit> merged = credits;
    // Hand the imported roll's end marker to the final own row. With no
    // imported roll, our block is a complete roll by itself.
    if (!merged.empty()) {
        merged.back().kind = 0;
        merged.back().longSpacing = true;
    }
    for (size_t i = 0; i < std::size(kRows); ++i)
        merged.push_back({kRows[i].font, kFirstLine + static_cast<int>(i), kRows[i].color, kRows[i].longSpacing, 0});
    merged.back().kind = 1;
    return merged;
}

std::vector<text::Entry> Texts(std::string_view language)
{
    const size_t lang = LanguageIndex(language);
    std::vector<text::Entry> texts;
    for (size_t i = 0; i < std::size(kRows); ++i)
        texts.push_back({"CREDIT_ENTRY_" + std::to_string(kFirstLine + static_cast<int>(i)),
                text_load::EngineValue(kRows[i].text[lang], language == "pl")});
    return texts;
}

} // namespace obl::own_credits
