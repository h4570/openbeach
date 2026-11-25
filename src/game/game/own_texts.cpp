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

// The mods screen and the main menu's Mods button (ui/own_layer.h), on every pack.
constexpr Row kModsRows[] = {
        {"OBL_MENU_MODS", {u"Mods", u"Mods", u"Mod", u"Mods", u"Mods", u"Mody"}},
        {"OBL_MODS_TITLE", {u"Mods", u"Mods", u"Mod", u"Mods", u"Mods", u"Mody"}},
        {"OBL_MODS_BACK",
                {u"Back", u"Retour", u"Indietro",
                        u"Zur\x00fc"
                        u"ck",
                        u"Volver", u"Wr\x00f3\x0107"}},
        {"OBL_MODS_HELP", {u"Help", u"Aide", u"Aiuto", u"Hilfe", u"Ayuda", u"Pomoc"}},
        {"OBL_MODS_INSTALL",
                {u"Install from file", u"Installer depuis un fichier", u"Installa da file", u"Aus Datei installieren",
                        u"Instalar desde archivo", u"Zainstaluj z pliku"}},
        {"OBL_MODS_REMOVE", {u"Remove", u"Supprimer", u"Rimuovi", u"Entfernen", u"Eliminar", u"Usu\u0144"}},
        {"OBL_MODS_CONFIRM", {u"Confirm", u"Confirmer", u"Conferma", u"Best\u00e4tigen", u"Confirmar", u"Potwierd\u017a"}},
        {"OBL_MODS_CANCEL", {u"Cancel", u"Annuler", u"Annulla", u"Abbrechen", u"Cancelar", u"Anuluj"}},
        {"OBL_MODS_REPLACE_CONFIRM",
                {u"Replace %s with the selected ZIP?", u"Remplacer %s par le ZIP s\u00e9lectionn\u00e9 ?",
                        u"Sostituire %s con lo ZIP selezionato?", u"%s durch die gew\u00e4hlte ZIP-Datei ersetzen?",
                        u"\u00bfReemplazar %s con el ZIP elegido?", u"Zast\u0105pi\u0107 %s wybranym plikiem ZIP?"}},
        {"OBL_MODS_REMOVE_CONFIRM",
                {u"Remove %s and its generated assets?", u"Supprimer %s et ses ressources g\u00e9n\u00e9r\u00e9es ?",
                        u"Rimuovere %s e le risorse generate?", u"%s und erzeugte Inhalte entfernen?",
                        u"\u00bfEliminar %s y sus recursos generados?", u"Usun\u0105\u0107 %s i wygenerowane zasoby?"}},
        {"OBL_MODS_ARCHIVE_ACTIVE",
                {u"Switch %s off first. Restart the game if it is still running.",
                        u"D\u00e9sactivez d'abord %s. Relancez le jeu si le mod est encore actif.",
                        u"Disattiva prima %s. Riavvia se la mod \u00e8 ancora attiva.",
                        u"%s zuerst abschalten. Ist der Mod noch aktiv, das Spiel neu starten.", u"Desactiva %s. Reinicia si sigue activo.",
                        u"Najpierw wy\u0142\u0105cz %s. Je\u015bli nadal dzia\u0142a, uruchom gr\u0119 ponownie."}},
        {"OBL_MODS_INSTALLED",
                {u"Installed %s", u"%s install\u00e9", u"%s installata", u"%s installiert", u"%s instalado", u"Zainstalowano %s"}},
        {"OBL_MODS_REMOVED", {u"Removed %s", u"%s supprim\u00e9", u"%s rimossa", u"%s entfernt", u"%s eliminado", u"Usuni\u0119to %s"}},
        {"OBL_MODS_INSTALL_ERROR",
                {u"Cannot install: %s", u"Installation impossible : %s", u"Impossibile installare: %s", u"Installation fehlgeschlagen: %s",
                        u"No se puede instalar: %s", u"Nie mo\u017cna zainstalowa\u0107: %s"}},
        {"OBL_MODS_REMOVE_ERROR",
                {u"Cannot remove: %s", u"Suppression impossible : %s", u"Impossibile rimuovere: %s", u"Entfernen fehlgeschlagen: %s",
                        u"No se puede eliminar: %s", u"Nie mo\u017cna usun\u0105\u0107: %s"}},
        {"OBL_MODS_NONE",
                {u"No mods installed. A mod is a folder in mods/ in the game folder.",
                        u"Aucun mod install\x00e9. Un mod est un dossier dans mods/ du dossier du jeu.",
                        u"Nessuna mod installata. Una mod \x00e8 una cartella in mods/ nella cartella del gioco.",
                        u"Keine Mods installiert. Ein Mod ist ein Ordner in mods/ im Spielordner.",
                        u"No hay mods instalados. Un mod es una carpeta en mods/ dentro de la carpeta del juego.",
                        u"Brak zainstalowanych mod\x00f3w. Mod to folder w mods/ w folderze gry."}},
        {"OBL_MODS_ON", {u"On", u"Oui", u"S\x00ec", u"An", u"S\x00ed", u"W\x0142."}},
        {"OBL_MODS_OFF", {u"Off", u"Non", u"No", u"Aus", u"No", u"Wy\x0142."}},
        {"OBL_MODS_WORLD", {u"World", u"Monde", u"Mondo", u"Welt", u"Mundo", u"\x015awiat"}},
        {"OBL_MODS_PRESENTATION",
                {u"Presentation", u"Pr\x00e9sentation", u"Presentazione", u"Darstellung", u"Presentaci\x00f3n", u"Prezentacja"}},
        {"OBL_MODS_VERSION", {u"Version %s", u"Version %s", u"Versione %s", u"Version %s", u"Versi\x00f3n %s", u"Wersja %s"}},
        {"OBL_MODS_PACKS", {u"Packs: %s", u"Packs : %s", u"Pacchetti: %s", u"Pakete: %s", u"Paquetes: %s", u"Pakiety: %s"}},
        {"OBL_MODS_SCOPE", {u"Kind: %s", u"Type : %s", u"Tipo: %s", u"Art: %s", u"Tipo: %s", u"Rodzaj: %s"}},
        {"OBL_MODS_REQUIRES",
                {u"Needs: %s",
                        u"N\x00e9"
                        u"cessite : %s",
                        u"Richiede: %s", u"Ben\x00f6tigt: %s", u"Requiere: %s", u"Wymaga: %s"}},
        {"OBL_MODS_TAKES_OVER",
                {u"Takes over: %s", u"Prend en charge : %s", u"Sostituisce: %s",
                        u"\x00dc"
                        u"bernimmt: %s",
                        u"Asume: %s", u"Przejmuje: %s"}},
        {"OBL_MODS_ACTIONS", {u"Actions: %s", u"Actions : %s", u"Azioni: %s", u"Aktionen: %s", u"Acciones: %s", u"Akcje: %s"}},
        {"OBL_MODS_BINDINGS", {u"Keys: %s", u"Touches : %s", u"Tasti: %s", u"Tasten: %s", u"Teclas: %s", u"Klawisze: %s"}},
        {"OBL_MODS_INCOMPATIBLE",
                {u"Does not work with: %s", u"Incompatible avec : %s", u"Non funziona con: %s", u"Nicht zusammen mit: %s",
                        u"No funciona con: %s",
                        u"Nie dzia\x0142"
                        u"a z: %s"}},
        {"OBL_MODS_PROBLEM_INVALID",
                {u"Broken: %s",
                        u"D\x00e9"
                        u"fectueux : %s",
                        u"Danneggiata: %s", u"Defekt: %s",
                        u"Da\x00f1"
                        u"ado: %s",
                        u"Uszkodzony: %s"}},
        {"OBL_MODS_PROBLEM_PACK",
                {u"Made for another pack (%s)", u"Fait pour un autre pack (%s)", u"Fatta per un altro pacchetto (%s)",
                        u"F\x00fcr ein anderes Paket (%s)", u"Hecho para otro paquete (%s)", u"Zrobiony dla innego pakietu (%s)"}},
        {"OBL_MODS_PROBLEM_API",
                {u"Needs a newer game (script API %s)",
                        u"N\x00e9"
                        u"cessite un jeu plus r\x00e9"
                        u"cent (API %s)",
                        u"Richiede un gioco pi\x00f9 recente (API %s)", u"Ben\x00f6tigt ein neueres Spiel (API %s)",
                        u"Requiere un juego m\x00e1s reciente (API %s)", u"Wymaga nowszej gry (API %s)"}},
        {"OBL_MODS_PROBLEM_MISSING",
                {u"Needs %s, which is off",
                        u"N\x00e9"
                        u"cessite %s, d\x00e9sactiv\x00e9",
                        u"Richiede %s, che \x00e8 disattivata", u"Ben\x00f6tigt %s, das aus ist", u"Requiere %s, que est\x00e1 desactivado",
                        u"Wymaga %s, kt\x00f3ry jest wy\x0142\x0105"
                        u"czony"}},
        {"OBL_MODS_PROBLEM_LATER",
                {u"Needs %s earlier in the order",
                        u"N\x00e9"
                        u"cessite %s plus haut dans l'ordre",
                        u"Richiede %s prima nell'ordine", u"Ben\x00f6tigt %s weiter oben", u"Requiere %s antes en el orden",
                        u"Wymaga %s wy\x017c"
                        u"ej na li\x015b"
                        u"cie"}},
        {"OBL_MODS_PROBLEM_CONFLICT",
                {u"Does not work with %s", u"Incompatible avec %s", u"Non funziona con %s", u"Funktioniert nicht mit %s",
                        u"No funciona con %s",
                        u"Nie dzia\x0142"
                        u"a z %s"}},
        {"OBL_MODS_SETTINGS", {u"Settings", u"R\x00e9glages", u"Impostazioni", u"Einstellungen", u"Ajustes", u"Ustawienia"}},
        {"OBL_MODS_SETTINGS_OFF",
                {u"Switch the mod on to change its settings.", u"Activez le mod pour changer ses r\x00e9glages.",
                        u"Attiva la mod per cambiarne le impostazioni.", u"Schalte den Mod ein, um ihn einzustellen.",
                        u"Activa el mod para cambiar sus ajustes.",
                        u"W\x0142\x0105"
                        u"cz mod, aby zmieni\x0107 jego ustawienia."}},
        {"OBL_MODS_NO_SETTINGS",
                {u"This mod has no settings.", u"Ce mod n'a pas de r\x00e9glages.", u"Questa mod non ha impostazioni.",
                        u"Dieser Mod hat keine Einstellungen.", u"Este mod no tiene ajustes.", u"Ten mod nie ma ustawie\x0144."}},
        {"OBL_MODS_CONFLICTS", {u"Conflicts", u"Conflits", u"Conflitti", u"Konflikte", u"Conflictos", u"Konflikty"}},
        {"OBL_MODS_NO_CONFLICTS",
                {u"No conflicts between the mods that are on.", u"Aucun conflit entre les mods actifs.",
                        u"Nessun conflitto tra le mod attive.", u"Keine Konflikte zwischen aktiven Mods.",
                        u"No hay conflictos entre los mods activos.",
                        u"Brak konflikt\x00f3w mi\x0119"
                        u"dzy w\x0142\x0105"
                        u"czonymi modami."}},
        {"OBL_MODS_CONFLICT_VALUE",
                {u"%s wins over %s: %s", u"%s l'emporte sur %s : %s", u"%s prevale su %s: %s", u"%s gewinnt gegen %s: %s",
                        u"%s gana a %s: %s", u"%s wygrywa z %s: %s"}},
        {"OBL_MODS_CONFLICT_HINT",
                {u"The later mod in the order wins; move a mod to change that.",
                        u"Le dernier mod de l'ordre l'emporte ; d\x00e9placez un mod pour changer.",
                        u"Vince la mod pi\x00f9 in basso; sposta una mod per cambiare.",
                        u"Der sp\x00e4tere Mod gewinnt; verschiebe einen Mod, um das zu \x00e4ndern.",
                        u"Gana el mod posterior; mueve un mod para cambiarlo.",
                        u"Wygrywa mod ni\x017c"
                        u"ej na li\x015b"
                        u"cie; przesu\x0144 mod, aby to zmieni\x0107."}},
        {"OBL_MODS_WORLD_NEXT",
                {u"Changes to world mods apply when the game starts again.", u"Les mods de monde changent au prochain lancement du jeu.",
                        u"Le mod del mondo cambiano al prossimo avvio del gioco.",
                        u"Welt-Mods \x00e4ndern sich beim n\x00e4"
                        u"chsten Spielstart.",
                        u"Los mods de mundo cambian al volver a iniciar el juego.",
                        u"Zmiany mod\x00f3w \x015bwiata zadzia\x0142"
                        u"aj\x0105 po ponownym uruchomieniu gry."}},
        {"OBL_MODS_FIX_FIRST",
                {u"Switch off or fix the mods marked in red first.", u"D\x00e9sactivez ou corrigez d'abord les mods en rouge.",
                        u"Prima disattiva o correggi le mod in rosso.", u"Schalte die rot markierten Mods zuerst aus oder repariere sie.",
                        u"Primero desactiva o arregla los mods en rojo.",
                        u"Najpierw wy\x0142\x0105"
                        u"cz lub napraw mody oznaczone na czerwono."}},
        {"OBL_MODS_REFUSED",
                {u"%s cannot be switched on: %s", u"%s ne peut pas \x00eatre activ\x00e9 : %s", u"%s non pu\x00f2 essere attivata: %s",
                        u"%s kann nicht eingeschaltet werden: %s", u"%s no se puede activar: %s",
                        u"Nie mo\x017cna w\x0142\x0105"
                        u"czy\x0107 %s: %s"}},
        {"OBL_MODS_CANNOT_START",
                {u"The mods cannot start: %s", u"Les mods ne peuvent pas d\x00e9marrer : %s", u"Le mod non possono avviarsi: %s",
                        u"Die Mods k\x00f6nnen nicht starten: %s", u"Los mods no pueden iniciarse: %s",
                        u"Mody nie mog\x0105 wystartowa\x0107: %s"}},
        {"OBL_MODS_PREVIOUS",
                {u"Previous",
                        u"Pr\x00e9"
                        u"c\x00e9"
                        u"dent",
                        u"Precedente", u"Vorherige", u"Anterior", u"Poprzednie"}},
        {"OBL_MODS_NEXT",
                {u"Next", u"Suivant", u"Successivo",
                        u"N\x00e4"
                        u"chste",
                        u"Siguiente", u"Nast\x0119pne"}},
        {"OBL_MODS_HELP_OPENED",
                {u"The help page opens in your browser: %s", u"La page d'aide s'ouvre dans votre navigateur : %s",
                        u"La pagina di aiuto si apre nel browser: %s",
                        u"Die Hilfeseite \x00f6"
                        u"ffnet sich im Browser: %s",
                        u"La p\x00e1gina de ayuda se abre en el navegador: %s",
                        u"Strona pomocy otworzy si\x0119 w przegl\x0105"
                        u"darce: %s"}},
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

std::vector<text::Entry> ModsScreenTexts(std::string_view language)
{
    const size_t lang = LanguageIndex(language);
    std::vector<text::Entry> texts;
    for (const Row& row : kModsRows) texts.push_back({row.key, text_load::EngineValue(row.text[lang], language == "pl")});
    return texts;
}

} // namespace obl::own_texts
