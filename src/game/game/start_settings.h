#pragma once

#include <filesystem>
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

// The frame caps the window offers, 0 = unlimited.
inline constexpr int kFrameCaps[] = {30, 60, 75, 120, 144, 240, 0};
// The window size multipliers it offers ([Display] Scale is 1..4).
inline constexpr double kScales[] = {1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0};

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
