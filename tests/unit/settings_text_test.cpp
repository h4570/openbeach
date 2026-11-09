// The settings window's words (src/launcher/settings_text.h): a text per game language, drawable by SDL's 8x8 font and short
// enough for its place in the layout.

#include "unit_test.h"

#include "core/code_page.h"
#include "game/tables/text_load.h"
#include "launcher/settings_text.h"

#include <set>
#include <string>
#include <string_view>

using namespace obl;
namespace words = obl::launcher::settings_text;

namespace {

bool IsDrawable(std::string_view utf8)
{
    for (const char16_t c : code_page::Utf8ToUtf16(utf8))
        if (!words::IsDrawable(c)) return false;
    return true;
}

// Columns of the 432-unit layout (src/launcher/settings_view.cpp): labels and hints from x 20 to the value boxes at 236,
// values between the arrows of a 176-unit box, the key help up to the Quit button at 222, the buttons' labels with a margin.
constexpr int kLabelColumns = 26;
constexpr int kValueColumns = 18;
constexpr int kKeyColumns = 24;
constexpr int kQuitColumns = 9;
constexpr int kPlayColumns = 10;
constexpr int kSubtitleColumns = 49;

void CheckFits(unsigned language, const char* field, const char* value, int columns)
{
    if (!IsDrawable(value)) ::obl::unit::ReportFailure(__FILE__, __LINE__, std::string("not drawable: ") + field + " = " + value);
    if (words::CountColumns(value) > columns)
        ::obl::unit::ReportFailure(__FILE__, __LINE__,
                "language " + std::to_string(language) + ": " + field + " \"" + value + "\" is longer than " + std::to_string(columns) +
                        " columns");
}

} // namespace

OBL_TEST_CASE("settings text: every language fits the layout and the font")
{
    for (unsigned language = 0; language < text_load::kLanguageCount; ++language) {
        const words::Texts& t = words::ForLanguage(language);
        CheckFits(language, "subtitle", t.subtitle, kSubtitleColumns);
        for (const char* label : {t.language, t.languageHint, t.onlyLanguageHint, t.display, t.switchHint, t.switchHintMac, t.size,
                     t.sizeHint, t.mouse, t.mouseHint, t.frameCap, t.frameCapHint, t.movie, t.movieHint, t.show, t.showHint})
            CheckFits(language, "label or hint", label, kLabelColumns);
        for (const char* value : {t.window, t.fullscreen, t.on, t.off, t.unlimited, words::LanguageName(language)})
            CheckFits(language, "value", value, kValueColumns);
        CheckFits(language, "keys", t.keys, kKeyColumns);
        CheckFits(language, "quit", t.quit, kQuitColumns);
        CheckFits(language, "play", t.play, kPlayColumns);
    }
}

OBL_TEST_CASE("settings text: each language has its own words and an unknown index speaks English")
{
    std::set<std::string> subtitles, names;
    for (unsigned language = 0; language < text_load::kLanguageCount; ++language) {
        subtitles.insert(words::ForLanguage(language).subtitle);
        names.insert(words::LanguageName(language));
    }
    OBL_CHECK_EQ(subtitles.size(), size_t{text_load::kLanguageCount});
    OBL_CHECK_EQ(names.size(), size_t{text_load::kLanguageCount});
    OBL_CHECK_EQ(std::string(words::ForLanguage(text_load::kLanguageCount).play), std::string("Play"));
    OBL_CHECK_EQ(std::string(words::LanguageName(3)), std::string("Deutsch"));
    OBL_CHECK_EQ(std::string(words::ForLanguage(5).language), std::string("Jezyk"));
}

OBL_TEST_CASE("settings text: SDL's font draws ASCII, columns count characters")
{
    OBL_CHECK(words::IsDrawable(U'e'));
    OBL_CHECK(!words::IsDrawable(U'ö')); // o umlaut: SDL draws its unsupported box
    OBL_CHECK(!words::IsDrawable(U'ę')); // e ogonek
    OBL_CHECK(!words::IsDrawable(U'\t'));
    OBL_CHECK_EQ(words::CountColumns("Gr\xc3\xb6\xc3\x9f"), 4); // two of the letters take two bytes each
}
