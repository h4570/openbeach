#include "settings_text.h"

#include "core/code_page.h"

#include <iterator>
#include <string>

namespace obl::launcher::settings_text {

namespace {

// Written in ASCII: SDL's font has Latin-1 glyphs, but SDL_RenderDebugText draws every character from U+00BE on as its
// "unsupported" box, so accents and umlauts are left out (German umlauts as ae, oe, ue and ss).
// Each string fits its place in the 432-unit layout (tests/unit/settings_text_test.cpp checks the budgets): labels and hints
// 26 cells, values 18, the key help 24, Quit 9 and Play 10.
constexpr Texts kEnglish{
        "Settings before you play",
        "Language",
        "imported from your copy",
        "the only one in this copy",
        "Display",
        "Alt+Enter switches in game",
        "Option+Return in game too",
        "Window size",
        "capped at the screen size",
        "Keep mouse in window",
        "the edge scrolls the map",
        "Frame rate cap",
        "frames per second",
        "Intro movie",
        "the movie before the menu",
        "Show this window",
        "off: start with --settings",
        "Window",
        "Fullscreen",
        "On",
        "Off",
        "Unlimited",
        "Enter: play  Esc: quit",
        "Quit",
        "Play",
};

constexpr Texts kFrench{
        "Reglages avant de jouer",
        "Langue",
        "importee de votre copie",
        "la seule de cette copie",
        "Affichage",
        "Alt+Entree aussi en jeu",
        "Option+Retour aussi en jeu",
        "Taille de la fenetre",
        "limitee a l'ecran",
        "Souris dans la fenetre",
        "le bord fait defiler",
        "Limite d'images",
        "images par seconde",
        "Film d'intro",
        "le film avant le menu",
        "Afficher cette fenetre",
        "non : lancer --settings",
        "Fenetre",
        "Plein ecran",
        "Oui",
        "Non",
        "Illimitee",
        "Entree: jouer Echap: fin",
        "Quitter",
        "Jouer",
};

constexpr Texts kItalian{
        "Impostazioni prima di giocare",
        "Lingua",
        "importata dalla tua copia",
        "l'unica di questa copia",
        "Schermo",
        "Alt+Invio anche in gioco",
        "Opzione+Invio in gioco",
        "Dimensione finestra",
        "al massimo lo schermo",
        "Mouse nella finestra",
        "il bordo scorre la mappa",
        "Limite fotogrammi",
        "fotogrammi al secondo",
        "Filmato iniziale",
        "il filmato prima del menu",
        "Mostra questa finestra",
        "no: avvia con --settings",
        "Finestra",
        "Schermo intero",
        "Si",
        "No",
        "Illimitato",
        "Invio: gioca  Esc: esci",
        "Esci",
        "Gioca",
};

constexpr Texts kGerman{
        "Einstellungen vor dem Spiel",
        "Sprache",
        "aus Ihrer Kopie importiert",
        "die einzige dieser Kopie",
        "Anzeige",
        "Alt+Enter auch im Spiel",
        "Option+Return im Spiel",
        "Fenstergroesse",
        "max. Bildschirmgroesse",
        "Maus im Fenster halten",
        "der Rand scrollt die Karte",
        "Bildratenlimit",
        "Bilder pro Sekunde",
        "Intro-Film",
        "der Film vor dem Menue",
        "Dieses Fenster zeigen",
        "aus: Start mit --settings",
        "Fenster",
        "Vollbild",
        "Ein",
        "Aus",
        "Unbegrenzt",
        "Enter: Spiel  Esc: Ende",
        "Beenden",
        "Spielen",
};

constexpr Texts kSpanish{
        "Ajustes antes de jugar",
        "Idioma",
        "importado de tu copia",
        "el unico de esta copia",
        "Pantalla",
        "Alt+Intro tambien en juego",
        "Opcion+Retorno en juego",
        "Tamano de ventana",
        "limitado a la pantalla",
        "Raton dentro de ventana",
        "el borde mueve el mapa",
        "Limite de fotogramas",
        "fotogramas por segundo",
        "Video de introduccion",
        "el video antes del menu",
        "Mostrar esta ventana",
        "no: inicia con --settings",
        "Ventana",
        "Pantalla completa",
        "Si",
        "No",
        "Ilimitado",
        "Intro: jugar  Esc: salir",
        "Salir",
        "Jugar",
};

constexpr Texts kPolish{
        "Ustawienia przed gra",
        "Jezyk",
        "z Twojej kopii gry",
        "jedyny w tej kopii",
        "Ekran",
        "Alt+Enter przelacza w grze",
        "Option+Return tez w grze",
        "Rozmiar okna",
        "najwyzej rozmiar ekranu",
        "Mysz zostaje w oknie",
        "krawedz przesuwa mape",
        "Limit klatek",
        "klatki na sekunde",
        "Film na poczatku",
        "film przed menu",
        "Pokazuj to okno",
        "nie: start z --settings",
        "Okno",
        "Pelny ekran",
        "Tak",
        "Nie",
        "Bez limitu",
        "Enter: graj  Esc: wyjdz",
        "Wyjdz",
        "Graj",
};

// In the game's language order: English, French, Italian, German, Spanish, Polish.
constexpr const Texts* kTexts[] = {&kEnglish, &kFrench, &kItalian, &kGerman, &kSpanish, &kPolish};
constexpr const char* kLanguageNames[] = {"English", "Francais", "Italiano", "Deutsch", "Espanol", "Polski"};
static_assert(std::size(kTexts) == std::size(kLanguageNames));

} // namespace

const Texts& ForLanguage(unsigned language)
{
    return *kTexts[language < std::size(kTexts) ? language : 0];
}

const char* LanguageName(unsigned language)
{
    return kLanguageNames[language < std::size(kLanguageNames) ? language : 0];
}

bool IsDrawable(char32_t c)
{
    // Printable ASCII: SDL_RenderDebugText also has a few Latin-1 signs, but none of the accented letters.
    return c >= 0x20 && c < 0x7f;
}

int CountColumns(std::string_view utf8)
{
    // Every letter the window can draw is in the Basic Multilingual Plane: one UTF-16 unit is one cell.
    return static_cast<int>(code_page::Utf8ToUtf16(utf8).size());
}

} // namespace obl::launcher::settings_text
