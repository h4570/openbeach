#pragma once

#include <string>
#include <vector>

// The folder scan of the music playlist (behind game::ScanMusicFolder): which tracks reach the playlist and in
// which order. The file system and the playlist itself stay with the caller (`Env`), so the walk is testable without a game. Shortcut (`.lnk`)
// files in the music folder are not followed: resolving them needs the Windows shell, and the portable host has none.
namespace obl::playlist {

// One entry of the saved playlist in the Settings object (`PlayListTrack<N>` ... `PlayListTime<N>`).
struct SavedTrack {
    std::string file;
    std::string directory;
    int night = 0;
    int lengthMs = -1;
};

inline constexpr const char* kMusicDirectory = "music\\";
inline constexpr const char* kTrackExtension = ".mp3";

// `Env` provides:
//   std::vector<std::string> List(dir, extension)      names matching `<dir>*<extension>` in the order of findfirst/findnext (files and directories)
//   bool Contains(dir, file) / Add(dir, file, night, lengthMs)   the playlist (Playlist::Contains / AddTrack)
template<class Env> void ScanFolder(Env& env, bool hasSavedTracks, const std::vector<SavedTrack>& saved, bool rescan)
{
    if (hasSavedTracks)
        for (const SavedTrack& track : saved) env.Add(track.directory, track.file, track.night, track.lengthMs);
    if (!rescan) return;

    for (const std::string& name : env.List(kMusicDirectory, kTrackExtension)) {
        if (name == "." || name == ".." || env.Contains(kMusicDirectory, name)) continue;
        env.Add(kMusicDirectory, name, 0, -1);
    }
}

} // namespace obl::playlist
