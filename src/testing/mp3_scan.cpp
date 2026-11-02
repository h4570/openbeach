// mp3-scan <directory> <file>: runs the MP3 length scan (the parser behind PlayListTime<N>) on one file
// and logs the frame count and length, to compare with an independent parser (docs/architecture/platform.md).
// id3-read <directory> <file>: PlaylistTrack::ReadId3Tag on a fresh track record (directory ends with a separator, e.g. packs/base/music/)
// and logs whether a tag was found, the lengths of artist/title and the sum of their bytes.

#include "mp3_scan.h"

#include "game/game/game_accessors.h"
#include "platform/mp3_length_scan.h"
#include "game/ui/sound_types.h"
#include "core/log.h"

#include <cstdint>
#include <cstring>

namespace obl::test {

bool ExecuteMp3ScanCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "id3-read" && args.size() == 2) {
        audio::PlaylistTrack track{};
        track.directory = args[0].c_str();
        track.file = args[1].c_str();
        track.ReadId3Tag();
        auto sum = [](const char* text) {
            unsigned total = 0;
            for (; text && *text; ++text) total += static_cast<unsigned char>(*text);
            return total;
        };
        OBL_LOG("autotest: id3-read %s tag=%d artist-len=%d title-len=%d artist-sum=%u title-sum=%u", args[1].c_str(),
                track.artist && track.title, track.artist ? static_cast<int>(std::strlen(track.artist)) : -1,
                track.title ? static_cast<int>(std::strlen(track.title)) : -1, sum(track.artist), sum(track.title));
        if (track.artist) delete[] track.artist;
        if (track.title) delete[] track.title;
        return true;
    }
    if (name != "mp3-scan" || args.size() != 2) return false;
    mp3::Mp3Info info{};
    platform::ScanMp3Length(info, args[0].c_str(), args[1].c_str());
    OBL_LOG("autotest: mp3-scan %s frames=%d ms=%d bitrate=%d rate=%d junk=%d bytes=%d valid=%d", args[1].c_str(), info.frames,
            info.lengthMs, info.bitrate, info.sampleRate, info.junkBytes, info.frameBytes, info.valid);
    return true;
}
} // namespace obl::test
