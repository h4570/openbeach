#pragma once

#include "core/ascii_case.h"
#include "game/ui/media_pack.h"
#include "game/ui/playlist_scan.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// The ini contract of the Settings object (BeachLife.ini): the sound-settings loader, SaveSound,
// LoadVideo and SaveVideo. Keys, formats, the "default" sentinel and the order of writes are the contract;
// the object and its heap stay with the caller. `Env` provides
//   std::string Read(section, key, size)   GetPrivateProfileString(section, key, "default", buffer, size, ini): the raw result
//   void Write(section, key, value)        WritePrivateProfileString(section, key, value, ini)
namespace obl::settings {

inline constexpr const char* kSoundSection = "Sound";
inline constexpr const char* kVideoSection = "Video";
// A key that is missing reads back as this text, so a key whose value is literally "default" counts as missing too.
inline constexpr const char* kMissing = "default";
inline constexpr int kSoundBuffer = 0x400;
inline constexpr int kVideoBuffer = 0x80;

struct SoundValues {
    float musicVol = 0;
    float effectsVol = 0;
    std::string provider; // 3DProvider (Miles provider name)
    uint8_t playListMode = 0;
    bool playListPresent = false; // set when PlayListCount was read
    std::vector<playlist::SavedTrack> tracks;
    // Outputs of LoadSound: the provider string / the whole playlist is replaced only when its key exists.
    bool providerRead = false;
    bool tracksRead = false;
};

struct VideoValues {
    int resWidth = 0;
    int resHeight = 0;
    uint8_t nudgeScroll = 0;
    uint8_t seaEffects = 0;
    uint8_t lightEffects = 0;
    uint8_t keyline = 0;
};

namespace detail {
inline std::string Number(const char* format, double value)
{
    char text[64];
    std::snprintf(text, sizeof(text), format, value);
    return text;
}
inline std::string Number(int value)
{
    return std::to_string(value);
}
inline std::string Indexed(const char* key, size_t index)
{
    return std::string(key) + std::to_string(index);
}
inline std::string Indexed(const char* key, int index)
{
    return std::string(key) + std::to_string(index);
}
// Reads `key`; false when it is missing. The comparison is case-sensitive, like a strcmp loop.
template<class Env> bool Get(Env& env, const char* section, const char* key, int size, std::string& value)
{
    value = env.Read(section, key, size);
    return value != kMissing;
}
} // namespace detail

template<class Env> void LoadSound(Env& env, SoundValues& values)
{
    std::string text;
    values.providerRead = values.tracksRead = false;
    if (detail::Get(env, kSoundSection, "MusicVol", kSoundBuffer, text)) values.musicVol = static_cast<float>(std::atof(text.c_str()));
    if (detail::Get(env, kSoundSection, "EffectsVol", kSoundBuffer, text)) values.effectsVol = static_cast<float>(std::atof(text.c_str()));
    if (detail::Get(env, kSoundSection, "3DProvider", kSoundBuffer, text)) {
        values.provider = text;
        values.providerRead = true;
    }
    if (detail::Get(env, kSoundSection, "PlayListMode", kSoundBuffer, text)) values.playListMode = std::atoi(text.c_str()) != 0;
    if (!detail::Get(env, kSoundSection, "PlayListCount", kSoundBuffer, text)) return;

    values.tracksRead = true;
    values.playListPresent = true;
    values.tracks.clear();
    const int count = std::atoi(text.c_str());
    for (int i = 0; i < count; ++i) {
        playlist::SavedTrack track;
        // A missing entry is not skipped: the "default" text it got back is copied as the file or directory name.
        detail::Get(env, kSoundSection, detail::Indexed("PlayListTrack", i).c_str(), kSoundBuffer, track.file);
        detail::Get(env, kSoundSection, detail::Indexed("PlayListDirectory", i).c_str(), kSoundBuffer, track.directory);
        detail::Get(env, kSoundSection, detail::Indexed("PlayListAttribute", i).c_str(), kSoundBuffer, text);
        track.night = std::atoi(text.c_str());
        track.lengthMs = -1;
        if (detail::Get(env, kSoundSection, detail::Indexed("PlayListTime", i).c_str(), kSoundBuffer, text))
            track.lengthMs = std::atoi(text.c_str());
        values.tracks.push_back(std::move(track));
    }
}

// Writes the section in the fixed key order. Stale PlayListTrack<N> keys beyond the new count are left in the file.
template<class Env> void SaveSound(Env& env, const SoundValues& values)
{
    env.Write(kSoundSection, "MusicVol", detail::Number("%f", values.musicVol));
    env.Write(kSoundSection, "EffectsVol", detail::Number("%f", values.effectsVol));
    env.Write(kSoundSection, "3DProvider", values.provider);
    env.Write(kSoundSection, "PlayListMode", detail::Number(values.playListMode));
    env.Write(kSoundSection, "PlayListCount", detail::Number(static_cast<int>(values.tracks.size())));
    for (size_t i = 0; i < values.tracks.size(); ++i) {
        const playlist::SavedTrack& track = values.tracks[i];
        env.Write(kSoundSection, detail::Indexed("PlayListTrack", i).c_str(), track.file);
        env.Write(kSoundSection, detail::Indexed("PlayListDirectory", i).c_str(), track.directory);
        env.Write(kSoundSection, detail::Indexed("PlayListAttribute", i).c_str(), detail::Number(track.night));
        env.Write(kSoundSection, detail::Indexed("PlayListTime", i).c_str(), detail::Number(track.lengthMs));
    }
}

template<class Env> void LoadVideo(Env& env, VideoValues& values)
{
    std::string text;
    if (detail::Get(env, kVideoSection, "ResWidth", kVideoBuffer, text)) values.resWidth = std::atoi(text.c_str());
    if (detail::Get(env, kVideoSection, "ResHeight", kVideoBuffer, text)) values.resHeight = std::atoi(text.c_str());
    if (detail::Get(env, kVideoSection, "NudgeScroll", kVideoBuffer, text)) values.nudgeScroll = std::atoi(text.c_str()) != 0;
    if (detail::Get(env, kVideoSection, "SeaEffects", kVideoBuffer, text)) values.seaEffects = std::atoi(text.c_str()) != 0;
    if (detail::Get(env, kVideoSection, "LightEffects", kVideoBuffer, text)) values.lightEffects = std::atoi(text.c_str()) != 0;
    if (detail::Get(env, kVideoSection, "Keyline", kVideoBuffer, text)) values.keyline = std::atoi(text.c_str()) != 0;
}

template<class Env> void SaveVideo(Env& env, const VideoValues& values)
{
    env.Write(kVideoSection, "ResWidth", detail::Number(values.resWidth));
    env.Write(kVideoSection, "ResHeight", detail::Number(values.resHeight));
    env.Write(kVideoSection, "NudgeScroll", detail::Number(values.nudgeScroll));
    env.Write(kVideoSection, "SeaEffects", detail::Number(values.seaEffects));
    env.Write(kVideoSection, "LightEffects", detail::Number(values.lightEffects));
    env.Write(kVideoSection, "Keyline", detail::Number(values.keyline));
}

// Remembered GUI window positions, [Dialog] (the dialog-positions saver / LoadDialogPositions).
// They form a list of entries {x, y, key}; the saved index is the entry's key.
inline constexpr const char* kDialogSection = "Dialog";
inline constexpr int kDialogBuffer = 0x80;
// Walking keys 0.. until `count` of them are found would hang when the file has fewer (or the count is
// negative), so the walk stops at this key.
inline constexpr int kDialogMaxKey = 1 << 16;

struct DialogPosition {
    int x = 0;
    int y = 0;
    int key = 0;
};

// Appends the entries found; a key counts only when DialogXPos<key> exists, DialogYPos<key> is read without the
// "default" check (a missing one gives atoi("default") = 0).
template<class Env> void LoadDialogs(Env& env, std::vector<DialogPosition>& entries)
{
    std::string text;
    if (!detail::Get(env, kDialogSection, "DialogCount", kDialogBuffer, text)) return;
    const int count = std::atoi(text.c_str());
    int found = 0;
    for (int key = 0; found < count && key < kDialogMaxKey; ++key) {
        if (!detail::Get(env, kDialogSection, detail::Indexed("DialogXPos", key).c_str(), kDialogBuffer, text)) continue;
        DialogPosition entry;
        entry.key = key;
        entry.x = std::atoi(text.c_str());
        entry.y = std::atoi(env.Read(kDialogSection, detail::Indexed("DialogYPos", key).c_str(), kDialogBuffer).c_str());
        entries.push_back(entry);
        ++found;
    }
}

// Remaster extension (resolutions.md B2): the screen size the positions were saved at. The GUI scripts center their windows,
// so a window moved at one resolution lands off by half the size difference at another; Rebase undoes exactly that.
// A file without the key is taken as already current.
inline constexpr const char* kDialogScreenKey = "DialogScreen";
template<class Env> bool LoadDialogScreen(Env& env, int& width, int& height)
{
    std::string text;
    if (!detail::Get(env, kDialogSection, kDialogScreenKey, kDialogBuffer, text)) return false;
    return std::sscanf(text.c_str(), "%dx%d", &width, &height) == 2 && width > 0 && height > 0;
}
template<class Env> void SaveDialogScreen(Env& env, int width, int height)
{
    env.Write(kDialogSection, kDialogScreenKey, detail::Number(width) + "x" + detail::Number(height));
}
inline void RebaseDialogs(std::vector<DialogPosition>& entries, int fromWidth, int fromHeight, int toWidth, int toHeight)
{
    const int dx = (toWidth - fromWidth) / 2, dy = (toHeight - fromHeight) / 2;
    for (DialogPosition& entry : entries) {
        entry.x += dx;
        entry.y += dy;
    }
}

template<class Env> void SaveDialogs(Env& env, const std::vector<DialogPosition>& entries)
{
    env.Write(kDialogSection, "DialogCount", detail::Number(static_cast<int>(entries.size())));
    for (const DialogPosition& entry : entries) {
        env.Write(kDialogSection, detail::Indexed("DialogXPos", entry.key).c_str(), detail::Number(entry.x));
        env.Write(kDialogSection, detail::Indexed("DialogYPos", entry.key).c_str(), detail::Number(entry.y));
    }
}

// The built-in soundtrack that the settings constructor / Settings::ResetPlaylist add to the playlist (its
// table: file, night flag, length in ms). The files are the converted `music/` names (media_pack.h): lowercase content keys.
inline constexpr const char* kSoundtrackDirectory = media_pack::kTrackDirectory;
struct SoundtrackEntry {
    const char* file;
    int night;
    int lengthMs;
};
inline constexpr SoundtrackEntry kSoundtrack[] = {
        {"day1.mp3", 0, 232520},
        {"day2.mp3", 0, 297504},
        {"day3.mp3", 0, 249408},
        {"day4.mp3", 0, 213486},
        {"day5.mp3", 0, 272002},
        {"day6.mp3", 0, 400581},
        {"night1.mp3", 1, 572570},
        {"night2.mp3", 1, 378720},
        {"night3.mp3", 1, 214167},
        {"night4.mp3", 1, 375394},
        {"night5.mp3", 1, 344788},
        {"night6.mp3", 1, 458024},
};

// Defaults the settings constructor stores before it loads the ini (values, strings).
inline constexpr float kDefaultMusicVol = 0.5f;
inline constexpr float kDefaultEffectsVol = 0.75f;
inline constexpr const char* kDefaultProvider = "DirectSound3D Software Emulation";
inline constexpr int kDefaultResWidth = 800;
inline constexpr int kDefaultResHeight = 600;
inline constexpr const char* kIniFileName = "BeachLife.ini";

// The main section of Settings::Load / Settings::Save (also written by the settings destructor).
inline constexpr const char* kMainSection = "Beach Life Options";
inline constexpr const char* kMainVersion = "1";
inline constexpr int kMainBuffer = 0x80;

struct MainValues {
    int language = 0;
    uint8_t systemCursor = 0;
};

// `Sections` runs the other parts in this order: Sound(), Dialogs(), Video().
// Returns false when the file has no matching Version key: nothing else is read then.
template<class Env, class Sections> bool LoadMain(Env& env, MainValues& values, Sections& sections)
{
    std::string text;
    if (env.Read(kMainSection, "Version", kMainBuffer) != kMainVersion) return false;
    if (detail::Get(env, "Language", "Language", kMainBuffer, text)) values.language = std::atoi(text.c_str());
    sections.LoadSound();
    sections.LoadDialogs();
    sections.LoadVideo();
    if (detail::Get(env, "Mouse", "SystemCursor", kMainBuffer, text)) values.systemCursor = std::atoi(text.c_str()) != 0;
    return true;
}

// `Sections::Touch()` is fopen("wt")/fclose: it creates the file and TRUNCATES an existing one,
// so a full save drops foreign sections and stale keys (the per-section SaveSound/SaveVideo alone do not).
template<class Env, class Sections> void SaveMain(Env& env, const MainValues& values, Sections& sections)
{
    sections.Touch();
    env.Write(kMainSection, "Version", kMainVersion);
    env.Write("Language", "Language", detail::Number(values.language));
    sections.SaveSound();
    sections.SaveDialogs();
    sections.SaveVideo();
    env.Write("Mouse", "SystemCursor", detail::Number(values.systemCursor));
}

} // namespace obl::settings
