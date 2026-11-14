#pragma once

#include "game/ui/playlist_scan.h"
#include "game/game/settings_ini.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace obl::game {

struct Settings {
    bool m_loaded = false; // the ini existed and had the main section
    std::string m_iniPath;
    float m_musicVolume = settings::kDefaultMusicVol;
    float m_effectsVolume = settings::kDefaultEffectsVol;
    std::string m_audioProvider = settings::kDefaultProvider; // Miles provider name
    uint8_t m_playlistMode = 1;
    std::vector<playlist::SavedTrack> m_playList;
    uint8_t m_playListPresent = 1;
    std::vector<settings::DialogPosition> m_dialogPositions; // moved GUI windows ([Dialog]), in insertion order
    int m_resolutionWidth = settings::kDefaultResWidth;
    int m_resolutionHeight = settings::kDefaultResHeight;
    int m_cursorSpeed = settings::kDefaultCursorSpeed; // percent; the handheld stick (Video options slider)
    int m_scrollSpeed = settings::kDefaultScrollSpeed; // percent; the map's scrolling (Video options slider)
    uint8_t m_nudgeScroll = 1;
    uint8_t m_seaEffects = 1;
    uint8_t m_lightEffects = 1;
    uint8_t m_keyline = 0;
    uint8_t m_systemCursor = 1;
    int m_language = 0; // the INI's choice: 0 English, 1 French, 2 Italian, 3 German, 4 Spanish, 5 Polish
    int m_shownLanguage = 0; // m_language, or the pack's own when it has no table for it; never written to the INI
};

namespace settings_store {

// Startup defaults, the built-in playlist and the ini overrides from BeachLife.ini in the current directory.
std::unique_ptr<Settings> Create();
// The Game's settings; valid between the game constructor and the game destructor.
Settings& Current();
// True while a Game with settings exists (test probes read it before boot).
bool Exists();

void Save(Settings& settings);
void ClearDialogPositions(Settings& settings);
void StoreDialogPosition(Settings& settings, int x, int y, int key);
bool FindDialogPosition(Settings& settings, int key, int* x, int* y);
void ResetPlaylist(Settings& settings);

} // namespace settings_store
} // namespace obl::game
