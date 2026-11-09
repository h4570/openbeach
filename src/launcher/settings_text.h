#pragma once

// The words of the settings window (docs/build/settings-window.md) in the six languages the game has, so the window speaks
// the language selected in it. Every string is drawn with SDL's built-in 8x8 font, which draws ASCII only, so the texts are
// written without accents, umlauts and Polish diacritics.

#include <string_view>

namespace obl::launcher::settings_text {

struct Texts {
    const char* subtitle;
    const char* language;
    const char* languageHint; // the copy has several languages
    const char* onlyLanguageHint; // it has one
    const char* display;
    const char* switchHint; // Alt+Enter
    const char* switchHintMac; // Option+Return
    const char* size;
    const char* sizeHint;
    const char* mouse;
    const char* mouseHint;
    const char* frameCap;
    const char* frameCapHint;
    const char* movie;
    const char* movieHint;
    const char* show;
    const char* showHint;
    const char* window;
    const char* fullscreen;
    const char* on;
    const char* off;
    const char* unlimited;
    const char* keys; // the key help left of the buttons
    const char* quit;
    const char* play;
};

// The texts for a game language index (start_settings::LanguageName order); English for any other index.
const Texts& ForLanguage(unsigned language);

// The language's name in itself, so a player who lands on a language they cannot read still finds their own.
const char* LanguageName(unsigned language);

// True when SDL's font, and so the window, can draw `c`.
bool IsDrawable(char32_t c);

// The width of UTF-8 text in font cells (one per character).
int CountColumns(std::string_view utf8);

} // namespace obl::launcher::settings_text
