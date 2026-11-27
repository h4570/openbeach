// Texts of OpenBeach's own controls and of the mods screen in every language (src/game/game/own_texts.h).

#include "unit_test.h"

#include "game/game/own_texts.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace obl;

namespace {

int Placeholders(const std::u16string& text)
{
    int count = 0;
    for (size_t at = text.find(u"%s"); at != std::u16string::npos; at = text.find(u"%s", at + 2)) ++count;
    return count;
}

void CheckLikeEnglish(const std::vector<text::Entry>& english, const std::vector<text::Entry>& texts, std::string_view language)
{
    OBL_REQUIRE(texts.size() == english.size());
    for (size_t i = 0; i < texts.size(); ++i) {
        OBL_CHECK_EQ(texts[i].key, english[i].key);
        const bool same = !texts[i].value.empty() && Placeholders(texts[i].value) == Placeholders(english[i].value);
        if (!same) std::printf("  %.*s %s\n", static_cast<int>(language.size()), language.data(), texts[i].key.c_str());
        OBL_CHECK(same);
    }
}

} // namespace

// A translation keeps every %s of the English text: a stray NUL in a literal once cut four translations of
// OBL_MODS_PROBLEM_ENGINE short before their %s.
OBL_TEST_CASE("own_texts.translations_keep_the_placeholders_of_english")
{
    const std::vector<text::Entry> english = own_texts::Texts("en");
    const std::vector<text::Entry> englishMods = own_texts::ModsScreenTexts("en");
    for (const std::string_view language : {"fr", "it", "de", "es", "pl"}) {
        CheckLikeEnglish(english, own_texts::Texts(language), language);
        CheckLikeEnglish(englishMods, own_texts::ModsScreenTexts(language), language);
    }
}
