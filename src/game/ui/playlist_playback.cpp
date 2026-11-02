
#include "game/ui/sound_api.h"

#include "game/game/game_accessors.h"
#include "core/os.h"
#include "game/ui/media_files.h"
#include "game/ui/sound_types.h"
#include "game/ui/media_pack.h"
#include "core/pack_path.h"
#include "core/mp3info.h"
#include "game/ui/playlist_nav.h"
#include "game/ui/playlist_stream.h"
#include "core/call_counter.h"

#include <cstdio>
#include <cstring>

// Stream side of the playlist: playback decisions live in playlist_stream.h, this file binds them to the game's Sound.
namespace obl::audio {
namespace {

using audio::Playlist;
using audio::PlaylistTrack;
using audio::Sound;
using Items = PlaylistTrack**;

// `call-report ui.playlist-playback` counts the operations.
test::CallCounter g_counter("ui.playlist-playback");

Items ItemsOf(Playlist* playlist)
{
    return playlist->tracks.data();
}
int CountOf(const Playlist* playlist)
{
    return static_cast<int>(playlist->tracks.size());
}

// The playlist's Sound as `playlist::PlayCurrent`/`Update`/... see it.
struct StreamHost {
    Sound* sound;
    bool Active() { return sound->IsStreamActive() != 0; }
    bool Stopped() { return sound->IsStreamStopped() != 0; }
    void Resume() { sound->ResumeStream(); }
    void Pause() { sound->PauseStream(); }
    void Close() { sound->CloseStream(); }
    char Play(const char* path) { return sound->PlayStream(path); }
};

// Copied track strings belong to the track and are released with delete[].
char* NewString(const char* text)
{
    const size_t size = std::strlen(text) + 1;
    auto* copy = new char[size];
    std::memcpy(copy, text, size);
    return copy;
}

} // namespace
} // namespace obl::audio

namespace obl::audio {

char Playlist::PlayCurrent(int resume)
{
    g_counter.Count();
    StreamHost host{sound};
    return playlist::PlayCurrent(*this, ItemsOf(this), CountOf(this), static_cast<char>(resume), host);
}

void Playlist::AdvanceTrack()
{
    g_counter.Count();
    StreamHost host{sound};
    playlist::Step(*this, ItemsOf(this), CountOf(this), +1, host);
}

void Playlist::PreviousTrack()
{
    g_counter.Count();
    StreamHost host{sound};
    playlist::Step(*this, ItemsOf(this), CountOf(this), -1, host);
}

char Playlist::Update()
{
    g_counter.Count();
    StreamHost host{sound};
    return playlist::Update(*this, ItemsOf(this), CountOf(this), host);
}

char Playlist::IsStreamActive()
{
    g_counter.Count();
    return sound->IsStreamActive();
}

void Playlist::StopStream()
{
    g_counter.Count();
    StreamHost host{sound};
    playlist::Stop(*this, host);
}

void Playlist::TogglePause()
{
    g_counter.Count();
    StreamHost host{sound};
    playlist::TogglePause(host);
}

// An unreadable file or (for an unknown length, -1) a file that is not Layer III is refused.
char Playlist::AddTrack(const char* directory, const char* file, int night, int lengthMs)
{
    g_counter.Count();
    // The converted soundtrack is opened from the pack layers (media_pack.h); the directory of the record stays as saved.
    const std::string openDirectory = media_pack::TrackDirectory(pack_path::Selected(), directory, file);
    const std::string path = openDirectory + file;
    std::FILE* handle = os::OpenFile(path, "rb");
    if (!handle) {
        // A converted track that is missing stops the windowed game; the other unreadable files are refused as before.
        if (std::strcmp(directory, media_pack::kTrackDirectory) == 0) ui::media_files::RequireWindowedFile(path);
        return 0;
    }
    std::fclose(handle);
    if (lengthMs == -1) {
        mp3::Mp3Info info;
        mp3::Init(info);
        mp3::ScanFile(info, openDirectory.c_str(), file);
        if (!mp3::IsValidLayer3(info)) return 0;
        lengthMs = info.lengthMs;
    }
    auto* track = new PlaylistTrack;
    std::memset(track, 0, sizeof(*track));
    track->file = NewString(file);
    track->directory = NewString(directory);
    track->night = night;
    track->lengthMs = lengthMs;
    tracks.push_back(track);
    track->ReadId3Tag();
    return 1;
}
} // namespace obl::audio
