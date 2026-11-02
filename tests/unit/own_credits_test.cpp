// OpenBeach's own credits merged into an invented converted roll (src/game/game/own_credits.h).

#include "game/game/own_credits.h"
#include "unit_test.h"

#include <set>
#include <string>

using obl::gui_content::Credit;

OBL_TEST_CASE("own_credits.appended_after_the_final_row")
{
    const std::vector<Credit> converted = {{2, 5, 0x000000, true, 0}, {0, 6, 0xff0000, false, 0}, {0, 7, 0x000000, false, 1}};
    const std::vector<Credit> merged = obl::own_credits::Merge(converted);
    const auto texts = obl::own_credits::Texts("en");
    OBL_REQUIRE(!texts.empty());
    OBL_REQUIRE(merged.size() == converted.size() + texts.size());
    OBL_CHECK_EQ(merged[0].line, 5);
    OBL_CHECK_EQ(merged[1].line, 6);
    OBL_CHECK_EQ(merged[2].line, 7);
    OBL_CHECK_EQ(merged[2].kind, 0); // and hands the end mark on
    OBL_CHECK(merged[2].longSpacing); // a gap before our block
    for (size_t i = 0; i < texts.size(); ++i) {
        const Credit& ours = merged[3 + i];
        OBL_CHECK_EQ(ours.line, obl::own_credits::kFirstLine + static_cast<int>(i));
        OBL_CHECK_EQ(ours.kind, i + 1 == texts.size() ? 1 : 0); // our last row ends the roll
        OBL_CHECK_EQ(texts[i].key, "CREDIT_ENTRY_" + std::to_string(ours.line));
        OBL_CHECK(!texts[i].value.empty());
    }
    OBL_CHECK_EQ(merged[3].font, 2); // our title in the large font, like the roll's own title
    const auto own = obl::own_credits::Merge({});
    OBL_REQUIRE(own.size() == texts.size());
    OBL_CHECK_EQ(own.front().line, obl::own_credits::kFirstLine);
    OBL_CHECK_EQ(own.back().kind, 1);
}

OBL_TEST_CASE("own_credits.texts_follow_the_language")
{
    const auto en = obl::own_credits::Texts("en");
    for (const char* language : {"fr", "it", "de", "es", "pl"}) {
        const auto translated = obl::own_credits::Texts(language);
        OBL_REQUIRE(translated.size() == en.size());
        for (size_t i = 0; i < en.size(); ++i) OBL_CHECK_EQ(translated[i].key, en[i].key);
        OBL_CHECK(translated[1].value != en[1].value); // the role heading is translated
        OBL_CHECK(translated[2].value.substr(0, 13) == en[2].value.substr(0, 13)); // the name is not translated
    }
    OBL_CHECK(obl::own_credits::Texts("pl")[3].value == u"Programowanie");
    OBL_CHECK(obl::own_credits::Texts("xx")[1].value == en[1].value); // an unrecognized language gives English
}

OBL_TEST_CASE("own_credits.polish_renderer_units")
{
    OBL_CHECK(obl::own_credits::Texts("pl")[2].value == u"Sandro Sobczy\x00f1ski (h4570)");
}

namespace {
void CheckTranslation(const char* language, const char16_t* lead, const char16_t* programming, const char16_t* claude, const char16_t* gpt)
{
    const auto rows = obl::own_credits::Texts(language);
    OBL_REQUIRE(rows.size() == 6);
    OBL_CHECK(rows[0].value == u"OpenBeach");
    OBL_CHECK(rows[1].value == lead);
    OBL_CHECK(rows[3].value == programming);
    OBL_CHECK(rows[4].value == claude);
    OBL_CHECK(rows[5].value == gpt);
}
} // namespace

OBL_TEST_CASE("own_credits.pl")
{
    CheckTranslation(
            "pl", u"Kierownik Projektu", u"Programowanie", u"Rodzina Claude 5.5 (Haiku, Sonnet, Opus)", u"Rodzina GPT-6 (Sol, Astra)");
}
OBL_TEST_CASE("own_credits.de")
{
    CheckTranslation(
            "de", u"Projektleitung", u"Programmierung", u"Claude-5.5-Familie (Haiku, Sonnet, Opus)", u"GPT-6-Familie (Sol, Astra)");
}
OBL_TEST_CASE("own_credits.fr")
{
    CheckTranslation("fr", u"Chef de projet", u"Programmation", u"Famille Claude 5.5 (Haiku, Sonnet, Opus)", u"Famille GPT-6 (Sol, Astra)");
}
OBL_TEST_CASE("own_credits.it")
{
    CheckTranslation(
            "it", u"Capo progetto", u"Programmazione", u"Famiglia Claude 5.5 (Haiku, Sonnet, Opus)", u"Famiglia GPT-6 (Sol, Astra)");
}
OBL_TEST_CASE("own_credits.es")
{
    CheckTranslation(
            "es", u"Jefe de proyecto", u"Programaci\x00f3n", u"Familia Claude 5.5 (Haiku, Sonnet, Opus)", u"Familia GPT-6 (Sol, Astra)");
}
