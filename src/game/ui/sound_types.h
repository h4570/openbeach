#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Sound, its channels and sample bank, the playlist and the music player.
namespace obl::ui {
struct GuiListBox; // game/ui/gui_lists_types.h
} // namespace obl::ui

namespace obl::audio {

// One playing sample (Sound::channels).
struct SoundChannel {
    std::string name; // the bank entry it holds
    const uint8_t* data;
    int type;
    uint32_t size;
    int fade;
    float scale, target, start, current, total, remaining;
    int owner;
};

struct SoundBankSlot {
    void* key;
    void* entry;
    uint8_t empty;
};

// Sample bank header (Sound::bank): the game hash table over the sample file entries; audio::SoundBank (sound_bank.h) holds the samples.
struct SoundBankRecord {
    uint32_t slots, capacity;
    SoundBankSlot* table;
    uint32_t modulus, count;
    void* file;
    uint32_t cached;

    // The sample table over the converted pack (sample_mixer.cpp).
    SoundBankRecord* Construct();
    void LoadHeader();
    void Destruct();
    const uint8_t* GetSample(const char* name, uint32_t* size);
    char HasSample(const char* name);
    void ReleaseSample(const char* name);
    void Update(float dt);
};

// g_Sound: the sample channels, the music stream and the listener.
struct Sound {
    uint8_t enabled, spatial;
    void* driver;
    uint32_t provider;
    void* stream;
    float musicVolume;
    void* samples[24];
    std::unique_ptr<SoundChannel> channels[24];
    int effectsVolume;
    float effectsScale;
    void* listener;
    float position[3], minDistance, maxDistance;
    SoundBankRecord* bank;

    // Lifecycle (sound_lifecycle.cpp). Samples and the music stream must be closed before Destruct.
    Sound* Construct(uint8_t enabled);
    void Destruct();
    void ReleaseRecords();

    // Sample mixer (sample_mixer.cpp). Channel indexes are not range-checked.
    int PlaySample(const char* name, int volume, uint8_t flags, float seconds, float scale);
    int Play3DSample(const char* name, int volume, const float* xyz, uint8_t flags, int owner);
    int Play3D(const char* name, const float* xyz, uint8_t flags, int owner);
    int Play3DScaled(const char* name, float scale, const float* xyz, uint8_t flags, int owner);
    void SetListenerPosition(const float* xyz);
    void Update(float dt);
    void UpdateFades(float dt);
    void SetEffectsVolume(float value);
    void SetChannelVolume(int i, float value);
    void FadeChannelTo(int i, float value, float seconds);
    void FadeChannelOut(int i, float seconds);
    void StartChannel(int i);
    void StopAllSamples();
    int FindChannel(const char* name);
    char HasSample(const char* name);
    int Play(const char* name, uint8_t flags, float seconds);
    int PlayScaled(const char* name, float volume, uint8_t flags, float seconds);
    int CountOwnerSamples(int owner);
    float GetEffectsVolume();

    // Music stream (music_backend.cpp).
    char PlayStream(const char* path);
    char IsStreamActive();
    char IsStreamStopped();
    void PauseStream();
    void ResumeStream();
    void CloseStream();
    void SetMusicVolume(float value);
    void Mute();
    void Unmute();
    // Unconditionally closes the music stream before platform sound teardown.
    void ShutdownMusic();
};

// One playlist track (`Playlist::AddTrack` allocates it): the strings live on the game heap.
struct PlaylistTrack {
    const char* file;
    const char* directory; // ends with a path separator
    char* artist; // ID3v1 artist, null without a tag
    char* title; // ID3v1 title, null without a tag
    int night; // night track flag
    uint8_t repeat; // repeat flag (Playlist::SetRepeat/IsRepeat read and write one byte)
    uint8_t marked; // delete mark (Playlist::MarkForDelete, consumed by Playlist::RemoveMarked)
    int lengthMs;

    // Fills artist and title from the ID3v1 tag of the file (playlist_id3.cpp); both stay null without a tag.
    void ReadId3Tag();
};

// Playlist (MusicPlayer's track list): Update, PlayCurrent and AdvanceTrack work over PlaylistTrack records.
struct Playlist {
    Sound* sound;
    std::vector<PlaylistTrack*> tracks;
    int current; // index into tracks
    int repeatMode; // 0 stop after the track, 1 advance to the next one
    uint8_t playing;
    int modeFilter; // time-of-day filter: 0 day, 1 night, 2 every track
    uint8_t failed; // the last Sound::PlayStream failed

    // Track list (playlist_state.cpp). Indexes are not range-checked; callers pass rows they got from the list.
    Playlist* Construct(Sound* owner);
    void Destruct();
    char* GetDisplayTitle(int index);
    char* GetArtist(int index);
    int GetCount();
    void SetCurrentIndex(int index);
    int GetNightFlag(int index);
    int GetLengthMs(int index);
    void SetNightFlag(int index, int night);
    void SetRepeat(int index, int repeat);
    char IsRepeat(int index);
    void Shuffle();
    void MarkForDelete(int index);
    void RemoveMarked();
    void MoveUp(int index);
    void MoveDown(int index);
    char Contains(const char* directory, const char* file);
    void Clear();
    char HasMode();
    char HasOtherMode();

    // Stream side (playlist_playback.cpp).
    char Update();
    char IsStreamActive();
    char PlayCurrent(int resume);
    void StopStream();
    void TogglePause();
    void AdvanceTrack();
    void PreviousTrack();
    char AddTrack(const char* directory, const char* file, int night, int lengthMs);
    // Starts the first track of the current time of day from the top (music_player.cpp).
    void RestartTimeOfDay();
};

// MusicPlayer (the GuiManager member): the playlist window's state around the Playlist. `rescan` is the "rescan requested"
// flag of command 0xe (not the playlist's filter).
struct MusicPlayer {
    Playlist* playlist;
    ui::GuiListBox* list; // the window's track list
    int selectedCount; // rows selected at the last MusicPlayer::Update (arrow buttons follow the 1 / 2+ switch)
    uint8_t panelVisible;
    int manual; // nonzero: the day/night switch is off (command 9)
    int night; // current time of day, 0 day / 1 night
    int forced; // 1 once the game forced a time of day
    int rescan;
    int lastIndex; // index of the track playing before the last switch, -1 none

    MusicPlayer* Construct();
    void Destruct();
    void SetNight(int night);
    void DeleteSelected();
    void ShuffleSelected();
    void ToggleRepeatSelected();
    void OnPanelVisibilityChanged(int visible);
    void Command(int command);
    void Update();
    void EnableTrackButtons();
    // Rebuild the panel rows and synchronize playlist settings. The script driver uses this same entry.
    void RebuildList(char clearSelection);
};

} // namespace obl::audio
