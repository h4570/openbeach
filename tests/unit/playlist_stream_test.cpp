// Playlist stream decisions (src/game/ui/playlist_stream.h): Playlist::Update/PlayCurrent/StopStream/TogglePause/Advance/Previous with a
// recording Sound host.

#include "unit_test.h"

#include "game/ui/playlist_stream.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace obl::playlist;

namespace {

struct Track {
    const char* file = "";
    const char* directory = "";
    int night = 0;
    uint8_t repeat = 0;
};

struct List {
    int current = 0;
    int repeatMode = 0;
    uint8_t playing = 0;
    int modeFilter = 2;
    uint8_t failed = 0;
};

struct Host {
    bool active = false, stopped = false, playResult = true;
    std::vector<std::string> calls;
    bool Active()
    {
        calls.push_back("active");
        return active;
    }
    bool Stopped()
    {
        calls.push_back("stopped");
        return stopped;
    }
    void Resume() { calls.push_back("resume"); }
    void Pause() { calls.push_back("pause"); }
    void Close() { calls.push_back("close"); }
    char Play(const char* path)
    {
        calls.push_back(std::string("play:") + path);
        return playResult ? 1 : 0;
    }
};

struct Tracks {
    Track storage[3] = {{"a.mp3", "dir\\", 0, 0}, {"b.mp3", "dir\\", 1, 0}, {"c.mp3", "x\\", 0, 0}};
    Track* items[3] = {&storage[0], &storage[1], &storage[2]};
};

} // namespace

OBL_TEST_CASE("playlist_stream.play_current_opens_directory_plus_file_and_sets_flags")
{
    Tracks t;
    List list;
    list.current = 2;
    Host host;
    OBL_CHECK_EQ(PlayCurrent(list, t.items, 3, 0, host), 1);
    OBL_REQUIRE(host.calls.size() == 1);
    OBL_CHECK_EQ(host.calls[0], std::string("play:x\\c.mp3"));
    OBL_CHECK_EQ(list.playing, 1);
    OBL_CHECK_EQ(list.failed, 0);

    host.playResult = false;
    OBL_CHECK_EQ(PlayCurrent(list, t.items, 3, 0, host), 0);
    OBL_CHECK_EQ(list.playing, 0);
    OBL_CHECK_EQ(list.failed, 1);
}

OBL_TEST_CASE("playlist_stream.play_current_resumes_paused_stream_only_with_resume")
{
    Tracks t;
    List list;
    Host host;
    host.stopped = true;
    OBL_CHECK_EQ(PlayCurrent(list, t.items, 3, 1, host), 1);
    OBL_REQUIRE(host.calls.size() == 2);
    OBL_CHECK_EQ(host.calls[1], std::string("resume"));
    OBL_CHECK_EQ(list.playing, 0); // resume does not touch the flags

    host.calls.clear();
    host.stopped = false;
    PlayCurrent(list, t.items, 3, 1, host);
    OBL_REQUIRE(host.calls.size() == 2);
    OBL_CHECK_EQ(host.calls[1], std::string("play:dir\\a.mp3"));

    host.calls.clear();
    OBL_CHECK_EQ(PlayCurrent(list, t.items, 0, 1, host), 0); // empty playlist: no Sound call at all
    OBL_CHECK(host.calls.empty());
}

OBL_TEST_CASE("playlist_stream.update_stops_or_advances_by_repeat_mode")
{
    Tracks t;
    List list;
    Host host;
    list.playing = 1;
    list.repeatMode = 0;
    OBL_CHECK_EQ(Update(list, t.items, 3, host), 0);
    OBL_CHECK_EQ(list.playing, 0);

    list.playing = 1;
    list.repeatMode = 1;
    host.calls.clear();
    OBL_CHECK_EQ(Update(list, t.items, 3, host), 1);
    OBL_CHECK_EQ(list.current, 1);
    OBL_CHECK_EQ(host.calls.back(), std::string("play:dir\\b.mp3"));

    list.repeatMode = 2; // any other mode leaves a finished stream alone
    list.playing = 1;
    OBL_CHECK_EQ(Update(list, t.items, 3, host), 0);
    OBL_CHECK_EQ(list.playing, 1);

    host.active = true; // still playing: nothing happens
    list.repeatMode = 0;
    OBL_CHECK_EQ(Update(list, t.items, 3, host), 0);
    OBL_CHECK_EQ(list.playing, 1);

    list.playing = 0; // not playing: Sound is not even asked
    host.calls.clear();
    OBL_CHECK_EQ(Update(list, t.items, 3, host), 0);
    OBL_CHECK(host.calls.empty());
}

OBL_TEST_CASE("playlist_stream.step_closes_stream_when_filter_has_no_track")
{
    Tracks t;
    List list;
    Host host;
    list.modeFilter = 3; // no track has night == 3
    list.playing = 1;
    Step(list, t.items, 3, -1, host);
    OBL_REQUIRE(host.calls.size() == 1);
    OBL_CHECK_EQ(host.calls[0], std::string("close"));
    OBL_CHECK_EQ(list.playing, 0);

    list.modeFilter = 2;
    list.current = 0;
    host.calls.clear();
    Step(list, t.items, 3, -1, host); // wraps to the last track and plays it
    OBL_CHECK_EQ(list.current, 2);
    OBL_CHECK_EQ(host.calls.back(), std::string("play:x\\c.mp3"));
}

OBL_TEST_CASE("playlist_stream.stop_and_toggle")
{
    List list;
    Host host;
    list.playing = 1;
    Stop(list, host);
    OBL_CHECK_EQ(list.playing, 0);
    OBL_CHECK_EQ(host.calls.back(), std::string("close"));

    TogglePause(host);
    OBL_CHECK_EQ(host.calls.back(), std::string("pause"));
    host.stopped = true;
    TogglePause(host);
    OBL_CHECK_EQ(host.calls.back(), std::string("resume"));
}
