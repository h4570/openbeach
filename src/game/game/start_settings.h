#pragma once

#include "core/settings_schema.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The settings a player picks in the settings window before the game starts (docs/build/settings-window.md): the language from
// BeachLife.ini and the display and start-up keys of openbeach.ini. Only these files carry them; this reads and writes them
// with the rules of their readers (config.cpp, settings_store.cpp), so the game sees exactly what the window showed.
namespace obl::game::start_settings {

inline constexpr const char* kFileName = "openbeach.ini";

// The commented default openbeach.ini. The game and the settings window both create it from this text, so the player always
// gets the comments, whoever writes the file first.
extern const char kDefaultFile[];

// Writes kDefaultFile to `path` when no file is there. False when it was missing and could not be written.
bool CreateDefaultFile(const std::filesystem::path& path);

// The engine's own settings of openbeach.ini, described the way a mod's are (core/settings_schema.h), so the settings window and
// the mods screen draw rows from one kind of description. The language is not here: it lives in BeachLife.ini and depends on the pack.
enum Setting { Windowed, Scale, ConfineMouse, MaxFps, PlayMovie, ShowAtStart, kSettingCount };

// The field of a setting: its id is the ini key, its options the values the window offers, its default what the game uses without
// a file. No setting of openbeach.ini changes the simulation (docs/build/settings.md).
const settings_schema::Field& EngineField(Setting setting);
// The section of openbeach.ini that holds it.
const char* EngineSection(Setting setting);

struct Values {
    // Settings language indices (text_load::LanguageCode) whose table the pack has, in settings order.
    std::vector<unsigned> languages;
    unsigned language = 0; // what the game will show: the requested language, or its fallback when the pack lacks it
    bool windowed = true;
    double scale = 1.0;
    bool confineMouse = true;
    int maxFps = 60;
    bool playMovie = true;
    bool showAtStart = true; // [Launcher] ShowSettings

    bool operator==(const Values&) const = default;
};

// A setting of `values` as the canonical text of its field, and back (a text the field does not accept leaves it alone).
std::string Get(const Values& values, Setting setting);
void Set(Values& values, Setting setting, std::string_view canonical);

// The current values for the game directory `gameDir` and its pack directory `pack` (absolute, or relative to the current
// directory). Missing files and keys give the defaults; nothing is written.
Values Load(const std::filesystem::path& gameDir, const std::filesystem::path& pack);

// Writes the values of `after` that differ from `before` (the result of Load) and leaves every other key alone. openbeach.ini
// is created from kDefaultFile first; BeachLife.ini gets its Version key when it has none, or the game would not read the
// language. False when a file could not be written.
bool Save(const std::filesystem::path& gameDir, const Values& before, const Values& after);

// The English name of a settings language index ("English", "French", ...).
const char* LanguageName(unsigned index);

} // namespace obl::game::start_settings
