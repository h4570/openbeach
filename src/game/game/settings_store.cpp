#include "game/game/settings_store.h"
#include "game/game/game_accessors.h"
#include "game/game/game.h"
#include "core/log.h"
#include "core/ini_file.h"
#include "core/os.h"

#include <cstdio>
#include <string>
#include <vector>

namespace obl::game {
namespace {

struct IniEnv {
    const char* path;
    std::string Read(const char* section, const char* key, int size) const
    {
        return ini::ReadString(path, section, key, settings::kMissing, static_cast<size_t>(size));
    }
    void Write(const char* section, const char* key, const std::string& value) const { ini::WriteString(path, section, key, value); }
};

settings::SoundValues CaptureSound(const Settings& s)
{
    settings::SoundValues values;
    values.musicVol = s.m_musicVolume;
    values.effectsVol = s.m_effectsVolume;
    values.provider = s.m_audioProvider;
    values.playListMode = s.m_playlistMode;
    values.playListPresent = s.m_playListPresent != 0;
    values.tracks = s.m_playList;
    return values;
}
void LoadSound(Settings& s)
{
    settings::SoundValues values = CaptureSound(s);
    IniEnv env{s.m_iniPath.c_str()};
    settings::LoadSound(env, values);
    s.m_musicVolume = values.musicVol;
    s.m_effectsVolume = values.effectsVol;
    s.m_playlistMode = values.playListMode;
    if (values.providerRead) s.m_audioProvider = values.provider;
    if (!values.tracksRead) return;
    s.m_playListPresent = 1;
    s.m_playList = std::move(values.tracks);
}
void SaveSound(const Settings& s)
{
    IniEnv env{s.m_iniPath.c_str()};
    settings::SaveSound(env, CaptureSound(s));
}
void LoadVideo(Settings& s)
{
    settings::VideoValues values{s.m_resolutionWidth, s.m_resolutionHeight, s.m_nudgeScroll, s.m_seaEffects, s.m_lightEffects, s.m_keyline};
    IniEnv env{s.m_iniPath.c_str()};
    settings::LoadVideo(env, values);
    s.m_resolutionWidth = values.resWidth;
    s.m_resolutionHeight = values.resHeight;
    s.m_nudgeScroll = values.nudgeScroll;
    s.m_seaEffects = values.seaEffects;
    s.m_lightEffects = values.lightEffects;
    s.m_keyline = values.keyline;
}
void SaveVideo(const Settings& s)
{
    IniEnv env{s.m_iniPath.c_str()};
    settings::SaveVideo(env,
            settings::VideoValues{
                    s.m_resolutionWidth, s.m_resolutionHeight, s.m_nudgeScroll, s.m_seaEffects, s.m_lightEffects, s.m_keyline});
}

// The built-in tracks are added without a file check: the playlist checks a track when it opens it (media_files.h).
void AddSoundtrack(Settings& s)
{
    for (const auto& track : settings::kSoundtrack)
        s.m_playList.push_back({track.file, settings::kSoundtrackDirectory, track.night, track.lengthMs});
}

// Resolution re-basing of the remembered positions (resolutions.md B2): a deliberate remaster change.
int g_savedScreenWidth = 0, g_savedScreenHeight = 0;
bool g_positionsRebased = true; // nothing pending until a load has seen a DialogScreen key
void LoadDialogs(Settings& s)
{
    IniEnv env{s.m_iniPath.c_str()};
    settings::LoadDialogs(env, s.m_dialogPositions);
    g_positionsRebased = !settings::LoadDialogScreen(env, g_savedScreenWidth, g_savedScreenHeight);
}
// Runs once, on first use after the load: the resolution of Settings is final by then, which it is not yet during the load itself.
void RebaseDialogPositions(Settings& s)
{
    if (g_positionsRebased) return;
    g_positionsRebased = true;
    if (s.m_resolutionWidth <= 0 || s.m_resolutionHeight <= 0) return;
    settings::RebaseDialogs(s.m_dialogPositions, g_savedScreenWidth, g_savedScreenHeight, s.m_resolutionWidth, s.m_resolutionHeight);
}
void SaveDialogs(Settings& s)
{
    RebaseDialogPositions(s);
    IniEnv env{s.m_iniPath.c_str()};
    settings::SaveDialogs(env, s.m_dialogPositions);
    settings::SaveDialogScreen(env, s.m_resolutionWidth, s.m_resolutionHeight);
}

struct MainSections {
    Settings& s;
    void LoadSound() { game::LoadSound(s); }
    void LoadDialogs() { game::LoadDialogs(s); }
    void LoadVideo() { game::LoadVideo(s); }
    void Touch()
    {
        if (FILE* file = os::OpenFile(s.m_iniPath.c_str(), "wt")) std::fclose(file);
    }
    void SaveSound() { game::SaveSound(s); }
    void SaveDialogs() { game::SaveDialogs(s); }
    void SaveVideo() { game::SaveVideo(s); }
};
bool Load(Settings& s)
{
    IniEnv env{s.m_iniPath.c_str()};
    MainSections sections{s};
    settings::MainValues values{s.m_language, s.m_systemCursor};
    const bool loaded = settings::LoadMain(env, values, sections);
    s.m_language = values.language;
    s.m_shownLanguage = values.language;
    s.m_systemCursor = values.systemCursor;
    return loaded;
}
} // namespace

namespace settings_store {
std::unique_ptr<Settings> Create()
{
    auto s = std::make_unique<Settings>();
    AddSoundtrack(*s);
    s->m_iniPath = os::CurrentDirectory() + os::kPathSeparator + settings::kIniFileName;
    g_positionsRebased = true;
    s->m_loaded = Load(*s);
    return s;
}
Settings& Current()
{
    return *GetGame()->m_settings;
}
bool Exists()
{
    const game::Game* game = GetGame();
    return game && game->m_settings;
}
void Save(Settings& s)
{
    // Scripted hosts also run directly from ctest, without autotest's snapshot/restore.
    // Music list edits and L0 language selection must never become the next run's defaults.
    if (os::HasEnv("OBL_SCRIPT") || os::HasEnv("OBL_HEADLESS")) {
        OBL_LOG("Settings: skip persistent save in scripted host");
        return;
    }
    OBL_LOG("Settings: save %s Language.Language=%d", s.m_iniPath.c_str(), s.m_language);
    IniEnv env{s.m_iniPath.c_str()};
    MainSections sections{s};
    settings::SaveMain(env, settings::MainValues{s.m_language, s.m_systemCursor}, sections);
}
void ClearDialogPositions(Settings& s)
{
    s.m_dialogPositions.clear();
}
// An existing key keeps its entry and gets the new position; a new one goes to the end of the list.
void StoreDialogPosition(Settings& s, int x, int y, int key)
{
    RebaseDialogPositions(s);
    for (auto& entry : s.m_dialogPositions) {
        if (entry.key != key) continue;
        entry.x = x;
        entry.y = y;
        return;
    }
    s.m_dialogPositions.push_back({x, y, key});
}
bool FindDialogPosition(Settings& s, int key, int* x, int* y)
{
    RebaseDialogPositions(s);
    for (const auto& entry : s.m_dialogPositions) {
        if (entry.key != key) continue;
        *x = entry.x;
        *y = entry.y;
        return true;
    }
    return false;
}
void ResetPlaylist(Settings& s)
{
    s.m_playList.clear();
    AddSoundtrack(s);
}
} // namespace settings_store
} // namespace obl::game
