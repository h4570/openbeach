#include "game/game/own_texts.h"
#include "game/tables/text_load.h"

#include <array>
#include <iterator>
#include <string>

namespace obl::own_texts {

namespace {

// The languages of text_load::LanguageCode, in the order of Row::text.
constexpr std::string_view kLanguages[] = {"en", "fr", "it", "de", "es", "pl"};
constexpr size_t kLanguageCount = std::size(kLanguages);

struct Row {
    const char* key;
    std::array<const char16_t*, kLanguageCount> text;
};

constexpr Row kRows[] = {
        {"OBL_CURSOR_SPEED",
                {u"Cursor speed", u"Vitesse du curseur", u"Velocit\x00e0 del cursore", u"Cursorgeschwindigkeit", u"Velocidad del cursor",
                        u"Szybko\x015b\x0107 kursora"}},
        {"OBL_TIPTEXT_CURSOR_SPEED",
                {u"How fast the stick moves the cursor", u"La vitesse du curseur avec le joystick",
                        u"Quanto veloce il joystick muove il cursore", u"Wie schnell der Stick den Cursor bewegt",
                        u"Qu\x00e9 tan r\x00e1pido mueve el cursor el joystick",
                        u"Jak szybko dr\x0105\x017c"
                        u"ek porusza kursorem"}},
        {"OBL_SCROLL_SPEED",
                {u"Scroll speed",
                        u"Vitesse de d\x00e9"
                        u"filement",
                        u"Velocit\x00e0 di scorrimento", u"Scrollgeschwindigkeit", u"Velocidad de desplazamiento",
                        u"Szybko\x015b\x0107 przewijania"}},
        {"OBL_TIPTEXT_SCROLL_SPEED",
                {u"How fast the map scrolls",
                        u"La vitesse de d\x00e9"
                        u"filement de la carte",
                        u"Quanto veloce scorre la mappa", u"Wie schnell die Karte scrollt", u"Qu\x00e9 tan r\x00e1pido se desplaza el mapa",
                        u"Jak szybko przewija si\x0119 plansza"}},
};

size_t LanguageIndex(std::string_view language)
{
    for (size_t i = 0; i < kLanguageCount; ++i)
        if (kLanguages[i] == language) return i;
    return 0;
}

} // namespace

std::vector<text::Entry> Texts(std::string_view language)
{
    const size_t lang = LanguageIndex(language);
    std::vector<text::Entry> texts;
    for (const Row& row : kRows) texts.push_back({row.key, text_load::EngineValue(row.text[lang], language == "pl")});
    return texts;
}

} // namespace obl::own_texts
