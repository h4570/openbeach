#include "unit_test.h"
#include "game/game/settings_ini.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace obl::settings;

struct FakeIni {
    std::map<std::string, std::string> values;
    std::vector<std::pair<std::string, std::string>> writes;
    std::string Read(const char* section, const char* key, int)
    {
        const auto found = values.find(std::string(section) + "/" + key);
        return found == values.end() ? kMissing : found->second;
    }
    void Write(const char* section, const char* key, const std::string& value)
    {
        writes.emplace_back(std::string(section) + "/" + key, value);
        values[std::string(section) + "/" + key] = value;
    }
};
} // namespace

OBL_TEST_CASE("settings_ini.missing_keys_keep_values")
{
    FakeIni ini;
    SoundValues sound;
    sound.musicVol = 0.5f;
    sound.provider = "kept";
    sound.playListMode = 1;
    sound.tracks = {{"a.mp3", "music\\", 0, -1}};
    LoadSound(ini, sound);
    OBL_CHECK_EQ(sound.musicVol, 0.5f);
    OBL_CHECK_EQ(sound.provider, std::string("kept"));
    OBL_CHECK_EQ(int(sound.playListMode), 1);
    OBL_CHECK_EQ(sound.tracks.size(), size_t(1));
    OBL_CHECK(!sound.providerRead && !sound.tracksRead && !sound.playListPresent);
    VideoValues video{800, 600, 1, 1, 1, 1};
    LoadVideo(ini, video);
    OBL_CHECK_EQ(video.resWidth, 800);
    OBL_CHECK_EQ(int(video.keyline), 1);
}

OBL_TEST_CASE("settings_ini.literal_default_counts_as_missing")
{
    FakeIni ini;
    ini.values["Video/ResWidth"] = "default";
    ini.values["Video/ResHeight"] = "1024";
    VideoValues video{640, 480, 0, 0, 0, 0};
    LoadVideo(ini, video);
    OBL_CHECK_EQ(video.resWidth, 640);
    OBL_CHECK_EQ(video.resHeight, 1024);
}

OBL_TEST_CASE("settings_ini.numbers_and_flags")
{
    FakeIni ini;
    ini.values["Sound/MusicVol"] = "0.25";
    ini.values["Sound/EffectsVol"] = "junk";
    ini.values["Sound/PlayListMode"] = "7";
    ini.values["Video/NudgeScroll"] = "0";
    ini.values["Video/SeaEffects"] = "5";
    ini.values["Video/LightEffects"] = "x";
    ini.values["Video/Keyline"] = "-1";
    SoundValues sound;
    sound.effectsVol = 3;
    LoadSound(ini, sound);
    OBL_CHECK_EQ(sound.musicVol, 0.25f);
    OBL_CHECK_EQ(sound.effectsVol, 0.0f); // atof of text that is not a number
    OBL_CHECK_EQ(int(sound.playListMode), 1);
    VideoValues video{0, 0, 1, 0, 1, 0};
    LoadVideo(ini, video);
    OBL_CHECK_EQ(int(video.nudgeScroll), 0);
    OBL_CHECK_EQ(int(video.seaEffects), 1);
    OBL_CHECK_EQ(int(video.lightEffects), 0);
    OBL_CHECK_EQ(int(video.keyline), 1);
}

OBL_TEST_CASE("settings_ini.playlist_entries")
{
    FakeIni ini;
    ini.values["Sound/3DProvider"] = "Miles Fast 2D Positional Audio";
    ini.values["Sound/PlayListCount"] = "3";
    ini.values["Sound/PlayListTrack0"] = "one.mp3";
    ini.values["Sound/PlayListDirectory0"] = "music\\";
    ini.values["Sound/PlayListAttribute0"] = "1";
    ini.values["Sound/PlayListTime0"] = "12345";
    ini.values["Sound/PlayListTrack1"] = "two.mp3";
    ini.values["Sound/PlayListDirectory1"] = "d:\\x\\";
    // Entry 2 is absent: the "default" text becomes its file and directory name.
    SoundValues sound;
    sound.tracks = {{"old.mp3", "music\\", 1, 5}};
    LoadSound(ini, sound);
    OBL_CHECK(sound.providerRead && sound.tracksRead && sound.playListPresent);
    OBL_CHECK_EQ(sound.provider, std::string("Miles Fast 2D Positional Audio"));
    OBL_CHECK_EQ(sound.tracks.size(), size_t(3));
    OBL_CHECK_EQ(sound.tracks[0].file, std::string("one.mp3"));
    OBL_CHECK_EQ(sound.tracks[0].night, 1);
    OBL_CHECK_EQ(sound.tracks[0].lengthMs, 12345);
    OBL_CHECK_EQ(sound.tracks[1].lengthMs, -1); // no PlayListTime key
    OBL_CHECK_EQ(sound.tracks[1].directory, std::string("d:\\x\\"));
    OBL_CHECK_EQ(sound.tracks[2].file, std::string("default"));
    OBL_CHECK_EQ(sound.tracks[2].directory, std::string("default"));
    OBL_CHECK_EQ(sound.tracks[2].night, 0);

    ini.values["Sound/PlayListCount"] = "-2";
    LoadSound(ini, sound);
    OBL_CHECK(sound.tracksRead && sound.tracks.empty()); // an empty list still replaces the old one
}

OBL_TEST_CASE("settings_ini.save_order_and_formats")
{
    FakeIni ini;
    SoundValues sound;
    sound.musicVol = 0.5f;
    sound.effectsVol = 1.0f;
    sound.provider = "Prov";
    sound.playListMode = 1;
    sound.tracks = {{"a.mp3", "music\\", 1, 200}, {"b.mp3", "x\\", 0, -1}};
    SaveSound(ini, sound);
    const std::vector<std::pair<std::string, std::string>> expected{{"Sound/MusicVol", "0.500000"}, {"Sound/EffectsVol", "1.000000"},
            {"Sound/3DProvider", "Prov"}, {"Sound/PlayListMode", "1"}, {"Sound/PlayListCount", "2"}, {"Sound/PlayListTrack0", "a.mp3"},
            {"Sound/PlayListDirectory0", "music\\"}, {"Sound/PlayListAttribute0", "1"}, {"Sound/PlayListTime0", "200"},
            {"Sound/PlayListTrack1", "b.mp3"}, {"Sound/PlayListDirectory1", "x\\"}, {"Sound/PlayListAttribute1", "0"},
            {"Sound/PlayListTime1", "-1"}};
    OBL_CHECK(ini.writes == expected);

    FakeIni video;
    SaveVideo(video, VideoValues{1280, 720, 1, 0, 1, 0});
    const std::vector<std::pair<std::string, std::string>> expectedVideo{{"Video/ResWidth", "1280"}, {"Video/ResHeight", "720"},
            {"Video/NudgeScroll", "1"}, {"Video/SeaEffects", "0"}, {"Video/LightEffects", "1"}, {"Video/Keyline", "0"}};
    OBL_CHECK(video.writes == expectedVideo);
}

OBL_TEST_CASE("settings_ini.save_then_load_round_trip")
{
    FakeIni ini;
    SoundValues sound;
    sound.musicVol = 0.75f;
    sound.effectsVol = 0.125f;
    sound.provider = "Prov";
    sound.tracks = {{"a.mp3", "music\\", 1, 200}, {"b.mp3", "x\\", 0, -1}};
    SaveSound(ini, sound);
    SoundValues loaded;
    LoadSound(ini, loaded);
    OBL_CHECK_EQ(loaded.musicVol, 0.75f);
    OBL_CHECK_EQ(loaded.effectsVol, 0.125f);
    OBL_CHECK_EQ(loaded.provider, sound.provider);
    OBL_CHECK_EQ(loaded.tracks.size(), size_t(2));
    OBL_CHECK_EQ(loaded.tracks[1].lengthMs, -1);
    OBL_CHECK_EQ(loaded.tracks[0].night, 1);
}

namespace {
struct RecordingSections {
    std::vector<std::string>* calls;
    void LoadSound() { calls->push_back("loadSound"); }
    void LoadDialogs() { calls->push_back("loadDialogs"); }
    void LoadVideo() { calls->push_back("loadVideo"); }
    void Touch() { calls->push_back("touch"); }
    void SaveSound() { calls->push_back("saveSound"); }
    void SaveDialogs() { calls->push_back("saveDialogs"); }
    void SaveVideo() { calls->push_back("saveVideo"); }
};
} // namespace

OBL_TEST_CASE("settings_ini.main_load_needs_matching_version")
{
    for (const char* version : {static_cast<const char*>(nullptr), "2", "default", ""}) {
        FakeIni ini;
        if (version) ini.values["Beach Life Options/Version"] = version;
        ini.values["Language/Language"] = "3";
        std::vector<std::string> calls;
        RecordingSections sections{&calls};
        MainValues values{1, 1};
        OBL_CHECK(!LoadMain(ini, values, sections));
        OBL_CHECK(calls.empty());
        OBL_CHECK_EQ(values.language, 1);
    }
}

OBL_TEST_CASE("settings_ini.main_load_order_and_values")
{
    FakeIni ini;
    ini.values["Beach Life Options/Version"] = "1";
    ini.values["Language/Language"] = "5";
    ini.values["Mouse/SystemCursor"] = "7";
    std::vector<std::string> calls;
    RecordingSections sections{&calls};
    MainValues values;
    OBL_CHECK(LoadMain(ini, values, sections));
    OBL_CHECK_EQ(values.language, 5);
    OBL_CHECK_EQ(int(values.systemCursor), 1);
    OBL_CHECK((calls == std::vector<std::string>{"loadSound", "loadDialogs", "loadVideo"}));
    ini.values["Mouse/SystemCursor"] = "0";
    ini.values["Language/Language"] = "default";
    OBL_CHECK(LoadMain(ini, values, sections));
    OBL_CHECK_EQ(values.language, 5);
    OBL_CHECK_EQ(int(values.systemCursor), 0);
}

OBL_TEST_CASE("settings_ini.main_save_order")
{
    FakeIni ini;
    std::vector<std::string> calls;
    RecordingSections sections{&calls};
    SaveMain(ini, MainValues{4, 1}, sections);
    OBL_CHECK((calls == std::vector<std::string>{"touch", "saveSound", "saveDialogs", "saveVideo"}));
    const std::vector<std::pair<std::string, std::string>> expected = {
            {"Beach Life Options/Version", "1"}, {"Language/Language", "4"}, {"Mouse/SystemCursor", "1"}};
    OBL_CHECK(ini.writes == expected);
}

OBL_TEST_CASE("settings_ini.dialogs_save_order_and_load")
{
    FakeIni ini;
    SaveDialogs(ini, {{10, 20, 3}, {-4, 5, 0}});
    const std::vector<std::pair<std::string, std::string>> expected = {{"Dialog/DialogCount", "2"}, {"Dialog/DialogXPos3", "10"},
            {"Dialog/DialogYPos3", "20"}, {"Dialog/DialogXPos0", "-4"}, {"Dialog/DialogYPos0", "5"}};
    OBL_CHECK(ini.writes == expected);
    std::vector<DialogPosition> loaded;
    LoadDialogs(ini, loaded);
    OBL_CHECK_EQ(int(loaded.size()), 2);
    // Keys are scanned in ascending order, so the list order follows the key, not the save order.
    OBL_CHECK_EQ(loaded[0].key, 0);
    OBL_CHECK_EQ(loaded[0].x, -4);
    OBL_CHECK_EQ(loaded[1].key, 3);
    OBL_CHECK_EQ(loaded[1].y, 20);
}

OBL_TEST_CASE("settings_ini.dialog_screen_rebase")
{
    FakeIni ini;
    int width = 0, height = 0;
    OBL_CHECK(!LoadDialogScreen(ini, width, height));
    SaveDialogScreen(ini, 1600, 900);
    OBL_CHECK(ini.writes.back() == std::make_pair(std::string("Dialog/DialogScreen"), std::string("1600x900")));
    ini.values["Dialog/DialogScreen"] = "1600x900";
    OBL_CHECK(LoadDialogScreen(ini, width, height));
    OBL_CHECK_EQ(width, 1600);
    OBL_CHECK_EQ(height, 900);
    ini.values["Dialog/DialogScreen"] = "junk";
    OBL_CHECK(!LoadDialogScreen(ini, width, height));
    std::vector<DialogPosition> entries = {{500, 300, 1}};
    RebaseDialogs(entries, 1600, 900, 1024, 768);
    OBL_CHECK_EQ(entries[0].x, 500 - 288);
    OBL_CHECK_EQ(entries[0].y, 300 - 66);
}

OBL_TEST_CASE("settings_ini.dialogs_missing_keys")
{
    FakeIni ini;
    std::vector<DialogPosition> loaded;
    LoadDialogs(ini, loaded);
    OBL_CHECK(loaded.empty());
    ini.values["Dialog/DialogCount"] = "3";
    ini.values["Dialog/DialogXPos1"] = "7"; // y missing: atoi("default") = 0
    ini.values["Dialog/DialogXPos2"] = "default";
    LoadDialogs(ini, loaded);
    OBL_CHECK_EQ(int(loaded.size()), 1);
    OBL_CHECK_EQ(loaded[0].key, 1);
    OBL_CHECK_EQ(loaded[0].x, 7);
    OBL_CHECK_EQ(loaded[0].y, 0);
    ini.values["Dialog/DialogCount"] = "-1";
    loaded.clear();
    LoadDialogs(ini, loaded);
    OBL_CHECK(loaded.empty());
}
