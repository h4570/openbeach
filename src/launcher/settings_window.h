#pragma once

// The settings window before the game starts (docs/build/settings-window.md): language, window or fullscreen, window size,
// mouse, frame cap and intro movie, so a player never has to edit an ini file. Drawn with SDL's renderer and its built-in
// font, so it needs nothing beyond what the game links, on Windows, Linux and macOS alike.

#include <string>

namespace obl::launcher {

enum class SettingsChoice {
    Play, // start the game (the changed settings are stored)
    Quit, // the player closed the window: start nothing
};

// True when `gameDir` holds the selected pack and its openbeach.ini asks for the window ([Launcher] ShowSettings, on by
// default). `force` (--settings) ignores ShowSettings.
bool ShouldShowSettings(const std::string& gameDir, bool force);

// Shows the window and blocks until the player starts the game or closes the window. Every SDL resource is released before it
// returns, as the game initializes SDL itself in the same process on Linux and macOS. A window that cannot be opened is Play.
SettingsChoice RunSettingsWindow(const std::string& gameDir);

} // namespace obl::launcher
