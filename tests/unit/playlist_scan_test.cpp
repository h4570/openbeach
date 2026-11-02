// Folder scan of the music playlist (src/game/ui/playlist_scan.h): the walk read from the playlist scan music folder routine.

#include "unit_test.h"

#include "game/ui/playlist_scan.h"

#include <map>
#include <string>
#include <vector>

using namespace obl::playlist;

namespace {

struct Added {
    std::string directory, file;
    int night, lengthMs;
    bool operator==(const Added& other) const
    {
        return directory == other.directory && file == other.file && night == other.night && lengthMs == other.lengthMs;
    }
};

struct FakeEnv {
    std::map<std::string, std::vector<std::string>> listings; // key: directory + "*" + extension
    std::vector<Added> list;

    std::vector<std::string> List(const std::string& directory, const std::string& extension)
    {
        auto found = listings.find(directory + "*" + extension);
        return found == listings.end() ? std::vector<std::string>{} : found->second;
    }
    bool Contains(const std::string& directory, const std::string& file)
    {
        for (const Added& track : list)
            if (track.directory == directory && track.file == file) return true;
        return false;
    }
    void Add(const std::string& directory, const std::string& file, int night, int lengthMs)
    {
        list.push_back({directory, file, night, lengthMs});
    }
};

} // namespace

OBL_TEST_CASE("playlist_scan.saved_tracks_only_when_present")
{
    FakeEnv env;
    const std::vector<SavedTrack> saved = {{"a.mp3", "data\\soundtrack\\", 0, 1000}, {"b.mp3", "music\\", 1, -1}};
    ScanFolder(env, false, saved, false);
    OBL_CHECK(env.list.empty());
    ScanFolder(env, true, saved, false);
    OBL_REQUIRE(env.list.size() == 2);
    OBL_CHECK(env.list[0] == (Added{"data\\soundtrack\\", "a.mp3", 0, 1000}));
    OBL_CHECK(env.list[1] == (Added{"music\\", "b.mp3", 1, -1}));
}

OBL_TEST_CASE("playlist_scan.folder_adds_new_files_once_and_skips_dots")
{
    FakeEnv env;
    env.listings["music\\*.mp3"] = {".", "..", "x.mp3", "y.mp3", "x.mp3"};
    ScanFolder(env, false, {{"x.mp3", "music\\", 1, 5}}, true); // saved list is ignored without the flag
    OBL_CHECK(env.list.empty() == false);
    OBL_REQUIRE(env.list.size() == 2);
    OBL_CHECK(env.list[0] == (Added{"music\\", "x.mp3", 0, -1}));
    OBL_CHECK(env.list[1] == (Added{"music\\", "y.mp3", 0, -1}));
}

OBL_TEST_CASE("playlist_scan.saved_track_blocks_the_folder_duplicate")
{
    FakeEnv env;
    env.listings["music\\*.mp3"] = {"x.mp3"};
    ScanFolder(env, true, {{"x.mp3", "music\\", 1, 5}}, true);
    OBL_REQUIRE(env.list.size() == 1);
    OBL_CHECK(env.list[0].night == 1);
}

OBL_TEST_CASE("playlist_scan.shortcuts_are_not_followed")
{
    FakeEnv env;
    env.listings["music\\*.mp3"] = {"x.mp3"};
    env.listings["music\\*.lnk"] = {"a.lnk"};
    ScanFolder(env, false, {}, true);
    OBL_REQUIRE(env.list.size() == 1);
    OBL_CHECK(env.list[0] == (Added{"music\\", "x.mp3", 0, -1}));
}
