#pragma once

#include "game/ui/media_pack.h"
#include "core/pack_path.h"
#include "playlist_nav.h"

#include <string>

// Stream side of the music playlist (Playlist::Update, PlayCurrent, StopStream, TogglePause, AdvanceTrack/PreviousTrack):
// the decisions and the playlist fields they write; the Sound stays with the caller.
// `List` needs `int current`, `int modeFilter`, `int repeatMode`, `uint8_t playing`, `uint8_t failed` (audio::Playlist); `items`/`count` are
// its track vector. `Host` provides the stream calls on the playlist's Sound:
//   bool Active()  Sound::IsStreamActive     bool Stopped()  Sound::IsStreamStopped (paused)
//   void Resume() / Pause() / Close()        char Play(path)  Sound::PlayStream
namespace obl::playlist {

// Playlist::PlayCurrent(resume): with `resume` a paused stream just continues; otherwise `directory` + `file` of the current track is opened.
template<class List, class Track, class Host> char PlayCurrent(List& list, Track* const* items, int count, char resume, Host& host)
{
    if (count == 0) return 0;
    if (resume && host.Stopped()) {
        host.Resume();
        return 1;
    }
    const Track* track = items[list.current];
    // A converted track is opened from the pack layers (media_pack.h), the others as `directory` + `file`.
    const std::string path = media_pack::TrackPath(pack_path::Selected(), track->directory, track->file);
    const char started = host.Play(path.c_str());
    list.playing = started ? 1 : 0;
    list.failed = started ? 0 : 1;
    return started;
}

// AdvanceTrack (+1) / PreviousTrack (-1): `Navigate` decides, the stream calls follow its result.
template<class List, class Track, class Host> void Step(List& list, Track* const* items, int count, int direction, Host& host)
{
    const NavResult result = Navigate(items, count, list.current, list.modeFilter, list.playing != 0, direction);
    if (result.closeStream) {
        host.Close();
        list.playing = 0;
    }
    if (result.play) PlayCurrent(list, items, count, 0, host);
}

// Playlist::Update: a finished stream either stops the playlist (repeat mode 0) or advances it (mode 1, returns 1); other modes do nothing.
template<class List, class Track, class Host> char Update(List& list, Track* const* items, int count, Host& host)
{
    if (list.playing && !host.Active()) {
        if (list.repeatMode == 0) {
            list.playing = 0;
        } else if (list.repeatMode == 1) {
            Step(list, items, count, +1, host);
            return 1;
        }
    }
    return 0;
}

template<class List, class Host> void Stop(List& list, Host& host)
{
    host.Close();
    list.playing = 0;
}

// Playlist::TogglePause: a paused stream resumes, anything else is paused.
template<class Host> void TogglePause(Host& host)
{
    if (host.Stopped())
        host.Resume();
    else
        host.Pause();
}

} // namespace obl::playlist
