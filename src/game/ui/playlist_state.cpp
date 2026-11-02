
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/sound_types.h"
#include "game/ui/playlist_logic.h"
#include "core/call_counter.h"

// Playlist state without the stream.
namespace obl::audio {
namespace {

using audio::Playlist;
using audio::PlaylistTrack;
using Items = PlaylistTrack**;

// `call-report ui.playlist-state` counts the operations.
test::CallCounter g_counter("ui.playlist-state");

Items ItemsOf(Playlist* playlist)
{
    return playlist->tracks.data();
}
int CountOf(Playlist* playlist)
{
    return static_cast<int>(playlist->tracks.size());
}

// Each track owns its copied strings and its record.
void ReleaseTrack(PlaylistTrack* track)
{
    const char* strings[] = {track->file, track->directory, track->artist, track->title};
    for (const char* text : strings) delete[] text;
    delete track;
}

// Runs a list-changing operation on the real list.
// `op(items, count, current, release)` is the whole C++ behavior; `release` frees a removed track.
template<class Op> void Mutate(Playlist* playlist, Op&& op)
{
    g_counter.Count();
    int count = CountOf(playlist);
    int current = playlist->current;
    op(ItemsOf(playlist), count, current, [](PlaylistTrack* track) { ReleaseTrack(track); });
    playlist->tracks.resize(static_cast<size_t>(count));
    playlist->current = current;
}

// Returns the text pointer a query computes (the game's getters hand out the track's own non-const strings).
template<class Compute> char* QueryText(Compute&& compute)
{
    g_counter.Count();
    return const_cast<char*>(static_cast<const char*>(compute()));
}

// Returns the value a query computes.
template<class Value, class Compute> Value Query(Compute&& compute)
{
    g_counter.Count();
    return compute();
}

} // namespace
} // namespace obl::audio

namespace obl::audio {

Playlist* Playlist::Construct(Sound* owner)
{
    sound = owner;
    tracks.clear();
    current = 0;
    repeatMode = 1;
    playing = 0;
    modeFilter = 2;
    failed = 0;
    return this;
}

void Playlist::Destruct()
{
    for (PlaylistTrack* track : tracks)
        if (track) ReleaseTrack(track);
    tracks = {};
}

char* Playlist::GetDisplayTitle(int index)
{
    return QueryText([&] {
        const PlaylistTrack* track = ItemsOf(this)[index];
        return track->title ? track->title : track->file;
    });
}

char* Playlist::GetArtist(int index)
{
    return QueryText([&] { return ItemsOf(this)[index]->artist; });
}

int Playlist::GetCount()
{
    return Query<int>([&] { return CountOf(this); });
}

void Playlist::SetCurrentIndex(int index)
{
    Mutate(this, [&](Items, int& count, int& current, auto&&) { current = playlist::ClampIndex(index, count); });
}

int Playlist::GetNightFlag(int index)
{
    return Query<int>([&] { return ItemsOf(this)[index]->night; });
}

int Playlist::GetLengthMs(int index)
{
    return Query<int>([&] { return ItemsOf(this)[index]->lengthMs; });
}

void Playlist::SetNightFlag(int index, int night)
{
    Mutate(this, [&](Items items, int&, int&, auto&&) { items[index]->night = night; });
}

void Playlist::SetRepeat(int index, int repeat)
{
    Mutate(this, [&](Items items, int count, int&, auto&&) { playlist::SetRepeat(items, count, index, static_cast<uint8_t>(repeat)); });
}

char Playlist::IsRepeat(int index)
{
    return Query<char>([&] { return static_cast<char>(ItemsOf(this)[index]->repeat); });
}

void Playlist::Shuffle()
{
    Mutate(this, [&](Items items, int count, int&, auto&&) { playlist::Shuffle(items, count, [] { return game::Rand(); }); });
}

void Playlist::MarkForDelete(int index)
{
    Mutate(this, [&](Items items, int&, int&, auto&&) { items[index]->marked = 1; });
}

void Playlist::RemoveMarked()
{
    Mutate(this, [&](Items items, int& count, int&, auto&& release) { count = playlist::RemoveMarked(items, count, release); });
}

void Playlist::MoveUp(int index)
{
    Mutate(this, [&](Items items, int, int& current, auto&&) { playlist::MoveUp(items, current, index); });
}

void Playlist::MoveDown(int index)
{
    Mutate(this, [&](Items items, int count, int& current, auto&&) { playlist::MoveDown(items, count, current, index); });
}

char Playlist::Contains(const char* directory, const char* file)
{
    return Query<char>([&] { return static_cast<char>(playlist::Contains(ItemsOf(this), CountOf(this), directory, file)); });
}

void Playlist::Clear()
{
    // Every track is released and the list ends up empty.
    Mutate(this, [&](Items items, int& count, int&, auto&& release) {
        for (int i = 0; i < count; ++i) release(items[i]);
        count = 0;
    });
}

char Playlist::HasMode()
{
    return Query<char>([&] { return static_cast<char>(playlist::HasMode(ItemsOf(this), CountOf(this), modeFilter)); });
}

char Playlist::HasOtherMode()
{
    return Query<char>([&] { return static_cast<char>(playlist::HasOtherMode(ItemsOf(this), CountOf(this), modeFilter)); });
}
} // namespace obl::audio
