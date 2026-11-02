#pragma once

#include "playlist_logic.h"

// Track navigation and vector growth of the music playlist (Playlist::AdvanceTrack, PreviousTrack, AddTrack): the
// decisions only, the stream calls stay with the caller.
namespace obl::playlist {

struct NavResult {
    bool closeStream = false; // no track of the time-of-day filter exists: the caller closes the stream and clears `playing`
    bool play = false; // the caller runs Playlist::PlayCurrent(0) afterwards
};

// `direction` +1 = AdvanceTrack, -1 = PreviousTrack. Moves `current` (wrapping at both ends); a repeating track stays current. With the
// filter 2 ("every track") the next track is taken as it is, otherwise the walk skips tracks of the other time of day and gives up after a
// full circle. The game returns without playing when the stream is not marked `playing` (after the walk, even if it found a track).
template<class Track> NavResult Navigate(Track* const* items, int count, int& current, int filter, bool playing, int direction)
{
    NavResult result;
    if (count == 0) return result;
    const int start = current;
    auto step = [&](int index) {
        index += direction;
        if (direction > 0) return index == count ? 0 : index;
        return index < 0 ? count - 1 : index;
    };
    if (items[start]->repeat == 0) {
        current = step(start);
        if (filter != 2) {
            for (int index = current; items[index]->night != filter; index = current) {
                if (index == start) {
                    result.closeStream = true;
                    playing = false;
                    break;
                }
                current = step(index);
            }
            if (!playing) return result;
        }
    }
    current = ClampIndex(current, count);
    result.play = true;
    return result;
}

// Playlist::RestartTimeOfDay start index: the first track whose `night` equals the filter, -1 when none (the caller then plays nothing).
template<class Track> int FirstOfMode(Track* const* items, int count, int filter)
{
    for (int index = 0; index < count; ++index)
        if (items[index]->night == filter) return index;
    return -1;
}

} // namespace obl::playlist
