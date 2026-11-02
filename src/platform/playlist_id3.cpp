#include "game/game/game_accessors.h"
#include "core/id3tag.h"
#include "game/ui/sound_types.h"
#include "game/ui/media_pack.h"
#include "core/pack_path.h"
#include "core/call_counter.h"

#include <cstring>
#include <string>

namespace obl::platform {
namespace {

// `call-report ui.playlist-id3` counts the tag reads.
test::CallCounter g_readTagsCalls("ui.playlist-id3");

// Copied tag strings belong to the track and are released with delete[].
char* NewString(const char* text)
{
    const size_t size = std::strlen(text) + 1;
    auto* copy = new char[size];
    std::memcpy(copy, text, size);
    return copy;
}

} // namespace
} // namespace obl::platform

namespace obl::audio {
void PlaylistTrack::ReadId3Tag()
{
    platform::g_readTagsCalls.Count();
    id3::Id3v1Text text;
    // The tag of the file the stream opens: a converted track may be shadowed by the mod layer (media_pack.h).
    const std::string trackDirectory = media_pack::TrackDirectory(pack_path::Selected(), directory, file);
    if (!id3::ReadTag(trackDirectory.c_str(), file, text)) return;
    artist = platform::NewString(text.artist);
    title = platform::NewString(text.title);
}
} // namespace obl::audio
