#include "game/ui/media_pack.h"

#include "core/pack_path.h"

#include <cstring>
#include <fstream>

namespace obl::media_pack {

std::string IntroPath(const std::filesystem::path& pack)
{
    return pack_path::Resolve(pack, "video/intro.mpg").generic_string();
}

std::string TrackDirectory(const std::filesystem::path& pack, const char* directory, const char* file)
{
    if (std::strcmp(directory, kTrackDirectory) != 0) return directory;
    // generic_string keeps the `/` form of the logical name, so an unshadowed track gives the same text as before the layers.
    const auto resolved = pack_path::Resolve(pack, std::string("music/") + file);
    return resolved.parent_path().generic_string() + "/";
}

std::string TrackPath(const std::filesystem::path& pack, const char* directory, const char* file)
{
    return TrackDirectory(pack, directory, file) + file;
}

bool CheckReadable(const std::string& path, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (in) return true;
    error = path + ": cannot open (Import the game data again)";
    return false;
}

} // namespace obl::media_pack
