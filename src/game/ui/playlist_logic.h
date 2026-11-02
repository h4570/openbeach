#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// State operations of the music playlist (behind the Playlist members), without the stream. The list is an array
// of track pointers; `Track` needs `file`/`directory` (C strings), `int night`, `uint8_t repeat` and `uint8_t marked` (audio::PlaylistTrack).
// `count` is the vector size, `current` the playlist's current index. Out-of-range indices are the caller's contract: the game indexes the vector unchecked.
namespace obl::playlist {

// Playlist::SetCurrentIndex: clamps to the last track, then to 0 (an empty list ends at 0).
inline int ClampIndex(int index, int count)
{
    if (index >= count) index = count - 1;
    return index < 0 ? 0 : index;
}

// Playlist::SetRepeat(index, value): a non-zero value first clears the flag of every track, so at most one track repeats.
template<class Track> void SetRepeat(Track* const* items, int count, int index, uint8_t value)
{
    if (value != 0)
        for (int i = 0; i < count; ++i) items[i]->repeat = 0;
    items[index]->repeat = value;
}

// Playlist::Shuffle: the sequence of draws is the game's (`rand() % (i + 1)` for i = 1..count-1, swap with i), so a recorded tape replays it.
template<class Track, class Rand> void Shuffle(Track** items, int count, Rand&& rand)
{
    for (int i = 1; i < count; ++i) {
        const int j = rand() % (i + 1);
        Track* picked = items[i];
        items[i] = items[j];
        items[j] = picked;
    }
}

// Playlist::MoveUp(index): swaps with the previous track; `current` follows its track. No-op for index <= 0.
template<class Track> void MoveUp(Track** items, int& current, int index)
{
    if (index <= 0) return;
    if (current == index)
        --current;
    else if (current == index - 1)
        ++current;
    Track* moved = items[index];
    items[index] = items[index - 1];
    items[index - 1] = moved;
}

// Playlist::MoveDown(index): swaps with the next track; no-op for the last track (and beyond).
template<class Track> void MoveDown(Track** items, int count, int& current, int index)
{
    if (index >= count - 1) return;
    if (current == index)
        ++current;
    else if (current == index + 1)
        --current;
    Track* moved = items[index];
    items[index] = items[index + 1];
    items[index + 1] = moved;
}

// Playlist::RemoveMarked: keeps the unmarked tracks in order, hands each marked one to `release` (the game frees its strings and the
// record) and returns the new count. `current` is not touched: the caller (MusicPlayer::DeleteSelected) corrects it.
template<class Track, class Release> int RemoveMarked(Track** items, int count, Release&& release)
{
    int kept = 0;
    for (int i = 0; i < count; ++i) {
        Track* track = items[i];
        if (track->marked)
            release(track);
        else
            items[kept++] = track;
    }
    return kept;
}

// MusicPlayer::DeleteSelected: what the selected rows (`rows`, selection order) do to the current index. `index` is the new current index
// (decremented once per deleted row below it; compared against the already decremented value, as in the game), `currentDeleted` says
// a deleted row was the current track, `lastRow` is the last selected row (the replacement row when the current track went away) and
// `reselect` the row selected afterwards (the last row moved up by the other deleted rows, as the game computes it).
struct DeletePlan {
    int index;
    bool currentDeleted;
    int lastRow;
    int reselect;
};

inline DeletePlan PlanDelete(const int* rows, int count, int current)
{
    DeletePlan plan{current, false, 0, 0};
    for (int i = 0; i < count; ++i) {
        const int row = rows[i];
        if (row == current) plan.currentDeleted = true;
        if (row < plan.index) --plan.index;
        plan.lastRow = row;
    }
    plan.reselect = count > 1 ? plan.lastRow + (1 - count) : plan.lastRow;
    return plan;
}

// Playlist::Contains(directory, file): exact (case-sensitive) match of both strings.
template<class Track> bool Contains(Track* const* items, int count, const char* directory, const char* file)
{
    for (int i = 0; i < count; ++i)
        if (std::strcmp(items[i]->directory, directory) == 0 && std::strcmp(items[i]->file, file) == 0) return true;
    return false;
}

// Playlist::HasMode: a track of the current time-of-day filter exists. Playlist::HasOtherMode: a track of another one exists.
template<class Track> bool HasMode(Track* const* items, int count, int filter)
{
    for (int i = 0; i < count; ++i)
        if (items[i]->night == filter) return true;
    return false;
}

template<class Track> bool HasOtherMode(Track* const* items, int count, int filter)
{
    for (int i = 0; i < count; ++i)
        if (items[i]->night != filter) return true;
    return false;
}

} // namespace obl::playlist
