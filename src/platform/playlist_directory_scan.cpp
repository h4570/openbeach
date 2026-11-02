#include "game/ui/playlist_services.h"
#include "game/game/settings_store.h"

#include "game/game/game_accessors.h"
#include "game/ui/sound_types.h"
#include "game/game/game.h"
#include "game/ui/playlist_scan.h"
#include "core/call_counter.h"
#include "core/os.h"

#include <string>
#include <vector>

// Folder scan of the music playlist: finds the playable tracks of the soundtrack folder and keeps the saved playlist
// in step with the settings.
namespace obl::platform {
namespace {

using audio::Playlist;
using audio::PlaylistTrack;
using playlist::SavedTrack;
using Items = PlaylistTrack**;

// `call-report ui.playlist-scan` and `ui.playlist-sync` count the operations.
test::CallCounter g_scanCalls("ui.playlist-scan");
test::CallCounter g_syncCalls("ui.playlist-sync");

Items ItemsOf(Playlist* list)
{
    return list->tracks.data();
}
int CountOf(const Playlist* list)
{
    return static_cast<int>(list->tracks.size());
}

Playlist* PlaylistOf(audio::MusicPlayer* player)
{
    return player->playlist;
}

// The file system and the playlist as `playlist::ScanFolder` sees them.
struct Env {
    Playlist* list;

    std::vector<std::string> List(const std::string& directory, const std::string& extension) const
    {
        // FindFirstFileA(directory + "*" + extension) in NTFS order: os::ListDirectory sorts the same way.
        std::vector<std::string> names;
        for (const os::DirectoryEntry& entry : os::ListDirectory(directory))
            if (os::MatchesPattern(entry.name, "", extension.c_str())) names.push_back(entry.name);
        return names;
    }
    bool Contains(const std::string& directory, const std::string& file) const
    {
        return list->Contains(directory.c_str(), file.c_str()) != 0;
    }
    void Add(const std::string& directory, const std::string& file, int night, int lengthMs) const
    {
        list->AddTrack(directory.c_str(), file.c_str(), night, lengthMs);
    }
};

std::vector<SavedTrack> ReadTracks(Playlist* list)
{
    std::vector<SavedTrack> tracks;
    for (int i = 0; i < CountOf(list); ++i) {
        const PlaylistTrack* track = ItemsOf(list)[i];
        tracks.push_back({track->file ? track->file : "", track->directory ? track->directory : "", track->night, track->lengthMs});
    }
    return tracks;
}

// The end of the scan and of every list edit.
void SyncToSettings(audio::MusicPlayer* player)
{
    // The saved playlist becomes a copy of the whole playlist.
    game::settings_store::Current().m_playList = ReadTracks(PlaylistOf(player));
}

void Scan(audio::MusicPlayer* player, bool rescan)
{
    const game::Settings& settings = game::settings_store::Current();
    Env env{PlaylistOf(player)};
    playlist::ScanFolder(env, settings.m_playListPresent != 0, settings.m_playList, rescan);
    SyncToSettings(player);
}

} // namespace

void ScanMusicFolder(audio::MusicPlayer* player, bool rescan)
{
    g_scanCalls.Count();
    Scan(player, rescan);
}

void SyncPlaylistToSettings(audio::MusicPlayer* player)
{
    g_syncCalls.Count();
    SyncToSettings(player);
}

} // namespace obl::platform
