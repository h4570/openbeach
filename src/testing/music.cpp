#include "game/ui/gui_action_entries.h"
#include "game/ui/sound_api.h"
#include "game/ui/music_player.h"
#include "music.h"

#include "core/config.h"
#include "game/ui/gui_lists.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/music_stream.h"
#include "game/game/settings_store.h"
#include "game/ui/sound_types.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_controls_types.h"
#include "core/log.h"
#include "game/ui/media_pack.h"
#include "test_util.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace obl::test {
namespace {
using ActionCallback = void (*)(void*);
using PlayCallback = char (*)(void*, const char*);
using VolumeCallback = void (*)(void*, float);
double g_pausePosition = 0;

struct Track {
    const char* file;
    const char* directory;
    void* artist = nullptr;
    void* title = nullptr;
    int night = 0;
    int repeat = 0;
    int length = 0;
};
Track g_tracks[] = {{"day1.mp3", media_pack::kTrackDirectory}, {"day2.mp3", media_pack::kTrackDirectory}};
Track* g_entries[] = {&g_tracks[0], &g_tracks[1]};
// The day/night fixture: a day and a night track, played by the Playlist code behind a MusicPlayer.
Track g_dayNightTracks[] = {
        {"day1.mp3", media_pack::kTrackDirectory, nullptr, nullptr, 0}, {"night1.mp3", media_pack::kTrackDirectory, nullptr, nullptr, 1}};
Track* g_dayNightEntries[] = {&g_dayNightTracks[0], &g_dayNightTracks[1]};
audio::MusicPlayer g_player{};
audio::Playlist g_playlist{};

audio::Sound* Sound()
{
    return audio::CurrentSound();
}

void Report()
{
    auto s = audio::GetMusicStats();
    audio::Sound* sound = Sound();
    OBL_LOG("autotest: music open=%d active=%d paused=%d seconds=%.6f duration=%.6f queued=%d "
            "volume=%d stored=%.6f gain=%.6f eof=%d failed=%d decoded=%llu energy=%.9f pause-delta=%.6f index=%d",
            s.open, sound ? sound->IsStreamActive() : 0, sound ? sound->IsStreamStopped() : 0, s.seconds, s.duration, s.queued, s.volume,
            sound ? sound->musicVolume : 0, s.gain, s.eof, s.failed, s.decoded, s.energy, std::abs(s.seconds - g_pausePosition),
            g_playlist.current);
}
} // namespace

bool ReadMusicProbe(const std::string& name, double& out)
{
    if (name == "music-index") {
        out = g_playlist.current;
        return true;
    }
    if (!name.starts_with("music-")) return false;
    if (name == "music-panel-open") {
        auto* gui = game::GetGuiManager();
        out = gui && gui->IsDialogOpen("MP3Panel");
        return true;
    }
    auto s = audio::GetMusicStats();
    if (name == "music-active")
        out = s.active;
    else if (name == "music-paused")
        out = s.paused;
    else if (name == "music-seconds")
        out = s.seconds;
    else if (name == "music-energy")
        out = s.energy;
    else if (name == "music-eof")
        out = s.eof;
    else
        return false;
    return true;
}

void DriveMusicPanel(int count)
{
    auto* gui = game::GetGuiManager();
    auto* player = gui ? static_cast<audio::MusicPlayer*>(gui->m_musicPlayer) : nullptr;
    if (!game::GetWorld() || !player) {
        OBL_LOG("autotest: FAILED music-panel requires gameplay and a MusicPlayer");
        return;
    }
    const bool wasVisible = player->panelVisible != 0;
    if (!wasVisible) gui::gui_action_entries::MP3List(0);
    auto* box = player->list;
    const bool dialogOpen = gui->IsDialogOpen("MP3Panel");
    if (!player->panelVisible || !box || !dialogOpen) {
        OBL_LOG("autotest: FAILED music-panel did not open player-visible=%d list=%p dialog-open=%d", player->panelVisible, box,
                dialogOpen);
        return;
    }
    auto* savedPlaylist = player->playlist;
    const int savedManual = player->manual;
    const int savedTop = box->m_scrollTop;
    const int savedCursor = box->m_selCursor;
    std::vector<int> savedSelection;
    for (auto* p = box->m_selBegin; p != box->m_selEnd; ++p) savedSelection.push_back(*p);

    constexpr int kTracks = 24;
    std::array<audio::PlaylistTrack, kTracks> tracks{};
    std::array<audio::PlaylistTrack*, kTracks> entries{};
    std::array<std::string, kTracks> files;
    char artist[] = "Panel artist";
    char title[] = "Panel title";
    char empty[] = "";
    char longTitle[] = "A long title for checking the list font ellipsis and clipping at the end of the title column";
    constexpr int kLengths[] = {0, -1, 999, 1000, 59999, 60000, 61000, 3600001, -61000};
    for (int row = 0; row < kTracks; ++row) {
        files[row] = "fixture-" + std::to_string(row) + ".mp3";
        tracks[row].file = files[row].c_str();
        tracks[row].directory = "music\\";
        tracks[row].artist = row % 6 == 0 || row % 6 == 4 ? nullptr : row % 6 == 3 ? empty : artist;
        tracks[row].title = row % 6 == 0 || row % 6 == 2 ? nullptr : row % 6 == 3 ? longTitle : row % 6 == 5 ? empty : title;
        tracks[row].night = row % 3; // Any nonzero flag uses the moon.
        tracks[row].lengthMs = kLengths[row % std::size(kLengths)];
        entries[row] = &tracks[row];
    }
    audio::Playlist fixture{};
    player->playlist = &fixture;
    int failures = 0, checkedCells = 0, scrollCases = 0;
    constexpr int kCounts[] = {kTracks, 0, 1, 4};
    for (int i = 0; i < count; ++i) {
        const int failuresBefore = failures;
        const int rows = kCounts[i % std::size(kCounts)];
        fixture.tracks.assign(entries.begin(), entries.begin() + rows);
        fixture.current = i % 3 == 0 ? -1 : i % 3 == 1 ? 0 : rows - 1;
        player->manual = (i / 4) % 2;
        const int repeatRow = (i / 8) % 3 == 0 ? -1 : (i / 8) % 3 == 1 ? 0 : rows - 1;
        for (int row = 0; row < kTracks; ++row) tracks[row].repeat = row == repeatRow;
        // Seed the input with real rows, a multi-selection, and a nonzero scroll position.
        player->RebuildList(1);
        gui::listbox::SelectRow(box, 0, 1);
        if (rows > 1) gui::listbox::SelectRow(box, rows - 1, 0);
        if (rows > box->m_visibleRows) {
            gui::listbox::SetScrollTop(box, 2);
            if (box->m_scrollTop != 2) ++failures;
            ++scrollCases;
        }
        const char clear = (i / 16) % 2 ? 7 : 0;
        const int selectionBefore = box->m_selBegin ? static_cast<int>(box->m_selEnd - box->m_selBegin) : 0;
        box->m_selCursor = 1;
        player->RebuildList(clear);
        const int selectionAfter = box->m_selBegin ? static_cast<int>(box->m_selEnd - box->m_selBegin) : 0;
        if (box->m_rowCount != static_cast<uint32_t>(rows) || box->m_scrollTop != 0 || selectionAfter != (clear ? 0 : selectionBefore) ||
                box->m_selCursor != 1 || (box->m_scroll != nullptr) != (rows > box->m_visibleRows))
            ++failures;
        int expectedCells = 0;
        for (int row = 0; row < rows; ++row)
            expectedCells += 1 + (tracks[row].repeat != 0) + (player->manual == 0) + (tracks[row].lengthMs != 0);
        const int cells = box->m_cellsBegin ? static_cast<int>(box->m_cellsEnd - box->m_cellsBegin) : 0;
        if (cells != expectedCells) ++failures;
        checkedCells += cells;
        // Rebuild must synchronize every settings field even for an empty playlist.
        const auto& settings = game::settings_store::Current();
        if (settings.m_playList.size() != static_cast<size_t>(rows)) ++failures;
        for (int row = 0; row < rows && row < static_cast<int>(settings.m_playList.size()); ++row) {
            const auto& saved = settings.m_playList[row];
            if (saved.file != tracks[row].file || saved.directory != tracks[row].directory || saved.night != tracks[row].night ||
                    saved.lengthMs != tracks[row].lengthMs)
                ++failures;
        }
        if (failures != failuresBefore)
            OBL_LOG("autotest: FAILED music-panel case=%d rows=%d manual=%d repeat=%d clear=%d cells=%d expected-cells=%d "
                    "selection=%d expected-selection=%d scroll=%d bar=%d",
                    i, rows, player->manual, repeatRow, clear, cells, expectedCells, selectionAfter, clear ? 0 : selectionBefore,
                    box->m_scrollTop, box->m_scroll != nullptr);
    }
    player->playlist = savedPlaylist;
    player->manual = savedManual;
    player->RebuildList(1);
    for (int row : savedSelection) gui::listbox::SelectRow(box, row, 0);
    box->m_selCursor = savedCursor;
    gui::listbox::SetScrollTop(box, savedTop);
    if (!wasVisible) gui::gui_action_entries::MP3List(0);
    OBL_LOG("autotest: %s music-panel cases=%d cells=%d scroll-cases=%d failures=%d", failures ? "FAILED" : "PASS", count, checkedCells,
            scrollCases, failures);
}

// Signatures of the last `music-actions record` run (state after every action), compared by `music-actions compare`.
std::vector<std::string> g_actionSignatures;

// The MP3 panel actions (delete, shuffle, repeat, visibility) on a fixture playlist of game-heap tracks behind the live open panel. The
// signature holds what the actions change: track order, current index, repeat flags, selection, cursor, scroll, rows, saved settings, filter.
void RunMusicActions(bool compare)
{
    auto* gui = game::GetGuiManager();
    auto* player = gui ? static_cast<audio::MusicPlayer*>(gui->m_musicPlayer) : nullptr;
    if (!game::GetWorld() || !player) {
        OBL_LOG("autotest: FAILED music-actions requires gameplay and a MusicPlayer");
        return;
    }
    const bool wasVisible = player->panelVisible != 0;
    if (!wasVisible) gui::gui_action_entries::MP3List(0);
    auto* box = player->list;
    if (!player->panelVisible || !box) {
        OBL_LOG("autotest: FAILED music-actions: the panel did not open");
        return;
    }
    auto* savedPlaylist = player->playlist;
    const int savedManual = player->manual, savedNight = player->night, savedFilter = savedPlaylist->modeFilter;
    const int savedTop = box->m_scrollTop, savedCursor = box->m_selCursor, savedSelected = player->selectedCount;
    const uint8_t savedMode = game::settings_store::Current().m_playlistMode;
    std::vector<int> savedSelection;
    for (auto* p = box->m_selBegin; p != box->m_selEnd; ++p) savedSelection.push_back(*p);

    auto heapString = [](const std::string& text) {
        char* copy = new char[text.size() + 1];
        std::memcpy(copy, text.c_str(), text.size() + 1);
        return copy;
    };
    audio::Sound sound{};
    audio::Playlist fixture{};
    fixture.sound = &sound;
    // The actions free only what they delete from the list, so releasing the tracks left in it is exact.
    auto release = [&] {
        for (auto* t : fixture.tracks) {
            for (const char* text : {t->file, t->directory}) delete[] text;
            delete t;
        }
        fixture.tracks.clear();
    };
    auto fill = [&](int rows) {
        release();
        for (int row = 0; row < rows; ++row) {
            auto* t = new audio::PlaylistTrack{};
            t->file = heapString("fixture-" + std::to_string(row) + ".mp3");
            t->directory = heapString("music\\");
            t->night = row % 2;
            t->lengthMs = 1000 * (row + 1);
            fixture.tracks.push_back(t);
        }
    };
    player->playlist = &fixture;
    std::vector<std::string> signatures;
    auto signature = [&](const char* what) {
        std::string s = std::string(what) + ":";
        auto num = [&](int v) { s += std::to_string(v) + ","; };
        for (const auto* t : fixture.tracks) {
            s += std::string(t->file) + (t->repeat ? "R" : "") + ";";
        }
        num(fixture.current);
        num(fixture.modeFilter);
        num(player->rescan);
        num(player->manual);
        num(game::settings_store::Current().m_playlistMode);
        num(player->panelVisible);
        num(player->list != nullptr);
        num(player->selectedCount);
        num(box->m_multiSelect);
        num(box->m_selCursor);
        num(box->m_scrollTop);
        num(static_cast<int>(box->m_rowCount));
        s += "sel:";
        for (auto* p = box->m_selBegin; p != box->m_selEnd; ++p) num(*p);
        const auto& settings = game::settings_store::Current();
        s += "saved:";
        for (const auto& saved : settings.m_playList) s += saved.file + ";";
        signatures.push_back(s);
    };
    auto select = [&](std::initializer_list<int> rows) {
        bool first = true;
        for (int row : rows) {
            gui::listbox::SelectRow(box, row, first);
            first = false;
        }
    };
    auto setup = [&](int rows, int current, std::initializer_list<int> rows2, int repeatRow = -1) {
        fill(rows);
        fixture.current = current;
        if (repeatRow >= 0) fixture.tracks[repeatRow]->repeat = 1;
        player->RebuildList(1);
        box->m_selCursor = 0;
        select(rows2);
        if (rows > box->m_visibleRows) gui::listbox::SetScrollTop(box, 3);
    };
    player->manual = 0;
    player->selectedCount = 0;
    // Delete: single row below / above / on the current track, several rows (running index compare), all rows, no selection.
    const struct {
        int rows, current;
        std::vector<int> sel;
    } deletes[] = {{10, 5, {2}}, {10, 5, {7}}, {10, 5, {5}}, {10, 5, {1, 4, 6}}, {10, 5, {1, 2, 3}}, {10, 0, {0}}, {10, 9, {9}},
            {10, 4, {0, 4, 9}}, {3, 1, {0, 1, 2}}, {1, 0, {0}}, {10, 5, {}}, {24, 20, {2, 3, 22}}};
    for (const auto& c : deletes) {
        setup(c.rows, c.current, {});
        for (size_t i = 0; i < c.sel.size(); ++i) gui::listbox::SelectRow(box, c.sel[i], i == 0);
        player->DeleteSelected();
        signature("delete");
    }
    // Shuffle: no selection and one row shuffle everything; two or more rows only trade places among themselves.
    for (auto sel : std::vector<std::vector<int>>{{}, {3}, {2, 5}, {0, 1, 2, 3, 4}, {1, 4, 7, 9}, {9, 4, 1}}) {
        setup(12, 6, {});
        for (size_t i = 0; i < sel.size(); ++i) gui::listbox::SelectRow(box, sel[i], i == 0);
        player->ShuffleSelected();
        signature("shuffle");
    }
    // Repeat: set on a row, clear it, move it from another row, the current track's own row (stream is off in the fixture).
    setup(12, 2, {4});
    player->ToggleRepeatSelected();
    signature("repeat-set");
    player->ToggleRepeatSelected();
    signature("repeat-clear");
    setup(12, 2, {2});
    player->ToggleRepeatSelected();
    signature("repeat-current");
    setup(24, 0, {8, 9}, 3);
    player->ToggleRepeatSelected();
    signature("repeat-move");
    // Visibility: show with the day/night switch on (day, night) and off, then hide (saves the settings: the playlist goes back first).
    for (int manual : {0, 0, 1}) {
        setup(8, 3, {});
        player->manual = manual;
        player->night = static_cast<int>(signatures.size() % 2);
        fixture.modeFilter = 2;
        box->m_multiSelect = 0;
        player->OnPanelVisibilityChanged(1);
        signature("visible");
    }
    // MusicPlayer::Command: night flags, move up/down (edges), switch on/off, track changes with the window open, stop/pause/refresh. 15 (reset to
    // the default playlist) rescans the disk and rewrites the settings, so it is not driven. The fixture Sound is off, so nothing streams.
    const struct {
        int cmd, rows, current;
        std::vector<int> sel;
    } commands[] = {{0, 8, 2, {1, 4, 5}}, {1, 8, 2, {0, 2, 6}}, {0, 8, 2, {}}, {1, 8, 2, {3}}, {5, 8, 3, {4}}, {5, 8, 3, {0}},
            {6, 8, 3, {4}}, {6, 8, 3, {7}}, {5, 24, 3, {10}}, {6, 24, 3, {10}}, {7, 8, 3, {5}}, {7, 8, 3, {}}, {10, 8, 3, {2}},
            {11, 8, 3, {2}}, {11, 8, 0, {}}, {12, 8, 3, {2}}, {13, 8, 3, {2}}, {14, 8, 3, {2}}, {8, 8, 3, {}}, {9, 8, 3, {}},
            {99, 8, 3, {2}}};
    for (const auto& c : commands) {
        setup(c.rows, c.current, {});
        for (size_t i = 0; i < c.sel.size(); ++i) gui::listbox::SelectRow(box, c.sel[i], i == 0);
        player->night = c.cmd & 1;
        fixture.modeFilter = c.cmd == 11 ? 2 : (c.cmd & 1);
        player->Command(c.cmd);
        signature(("cmd" + std::to_string(c.cmd)).c_str());
    }
    player->rescan = 0;
    // Playlist::RestartTimeOfDay: each filter over mixed tracks, a filter no track has (all tracks day, filter night), an empty list.
    const struct {
        int filter, rows;
        bool allDay;
    } restarts[] = {{0, 5, false}, {1, 5, false}, {2, 5, false}, {1, 5, true}, {0, 0, false}};
    for (const auto& r : restarts) {
        setup(r.rows, 0, {});
        if (r.allDay)
            for (auto* t : fixture.tracks) t->night = 0;
        fixture.modeFilter = r.filter;
        fixture.current = 2;
        fixture.RestartTimeOfDay();
        signature("restart");
    }
    // MusicPlayer::Update: the selection-size switch of the arrow buttons (0 -> 1 -> 3 -> 3 -> 1 -> 0 rows selected), an empty playlist, a quick row click
    // (m_bRepeat -> command 7), and a pending refresh (rescan of the music folder behind the open window). The fixture Sound is off, so the
    // playlist tick never reports an advanced track and the redraw branch is not reached.
    auto update = [&] { player->Update(); };
    setup(8, 3, {});
    player->selectedCount = 0;
    player->rescan = 0;
    for (std::vector<int> sel : {std::vector<int>{}, {1}, {1, 2, 3}, {1, 2, 3}, {4}, {}}) {
        for (size_t i = 0; i < sel.size(); ++i) gui::listbox::SelectRow(box, sel[i], i == 0);
        if (sel.empty()) box->m_selEnd = box->m_selBegin;
        update();
        signature("update-select");
    }
    setup(0, 0, {});
    player->selectedCount = 2;
    update();
    signature("update-empty");
    setup(8, 3, {5});
    box->m_repeat = 1;
    update();
    box->m_repeat = 0;
    signature("update-click");
    setup(8, 3, {2});
    player->rescan = 1;
    update();
    signature("update-rescan");
    player->EnableTrackButtons();
    signature("enable-buttons");
    player->playlist = savedPlaylist;
    player->manual = savedManual;
    player->night = savedNight;
    player->selectedCount = 0;
    game::settings_store::Current().m_playlistMode = savedMode;
    savedPlaylist->modeFilter = savedFilter;
    player->RebuildList(1);
    player->OnPanelVisibilityChanged(0);
    signature("hidden");
    player->OnPanelVisibilityChanged(1);
    savedPlaylist->modeFilter = savedFilter;
    for (int row : savedSelection) gui::listbox::SelectRow(box, row, 0);
    box->m_selCursor = savedCursor;
    player->selectedCount = savedSelected;
    gui::listbox::SetScrollTop(box, savedTop);
    if (!wasVisible) gui::gui_action_entries::MP3List(0);
    release();
    int failures = 0;
    if (compare) {
        if (signatures.size() != g_actionSignatures.size()) ++failures;
        for (size_t i = 0; i < signatures.size() && i < g_actionSignatures.size(); ++i)
            if (signatures[i] != g_actionSignatures[i]) {
                ++failures;
                OBL_LOG("autotest: FAILED music-actions case %zu\n  recorded %s\n  now      %s", i, g_actionSignatures[i].c_str(),
                        signatures[i].c_str());
            }
    } else {
        g_actionSignatures = signatures;
    }
    OBL_LOG("autotest: %s music-actions %s cases=%zu failures=%d", failures ? "FAILED" : "PASS", compare ? "compare" : "record",
            signatures.size(), failures);
}

bool ExecuteMusicCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (!name.starts_with("music-")) return false;
    if (name == "music-actions" && args.size() == 1) {
        RunMusicActions(args[0] == "compare");
        return true;
    }
    auto* sound = Sound();
    if (!sound) {
        OBL_LOG("autotest: FAILED music: Sound not constructed");
        return true;
    }
    if (name == "music-report") {
        Report();
    } else if (name == "music-play" && args.size() == 1) {
        int ok = sound->PlayStream(args[0].c_str());
        OBL_LOG("autotest: music-play success=%d", ok);
    } else if (name == "music-volume" && args.size() == 1) {
        sound->SetMusicVolume(static_cast<float>(std::atof(args[0].c_str())));
    } else if (name == "music-pause") {
        sound->PauseStream();
        g_pausePosition = audio::GetMusicStats().seconds;
    } else if (name == "music-resume") {
        sound->ResumeStream();
    } else if (name == "music-close") {
        sound->CloseStream();
    } else if (name == "music-mute") {
        sound->Mute();
    } else if (name == "music-unmute") {
        sound->Unmute();
    } else if (name == "music-seek-end") {
        bool ok = audio::SeekMusicForTest(std::max(0.0, audio::GetMusicStats().duration - 0.2));
        OBL_LOG("autotest: %s music-seek-end", ok ? "PASS" : "FAILED");
    } else if (name == "music-playlist") {
        sound->CloseStream();
        g_playlist = {};
        g_playlist.sound = sound;
        g_playlist.tracks.assign(
                reinterpret_cast<audio::PlaylistTrack**>(g_entries), reinterpret_cast<audio::PlaylistTrack**>(g_entries + 2));
        g_playlist.repeatMode = 1;
        g_playlist.modeFilter = 2;
        int ok = g_playlist.PlayCurrent(0);
        OBL_LOG("autotest: music-playlist success=%d", ok);
    } else if (name == "music-night-playlist") {
        sound->CloseStream();
        g_playlist = {};
        g_playlist.sound = sound;
        g_playlist.tracks.assign(reinterpret_cast<audio::PlaylistTrack**>(g_dayNightEntries),
                reinterpret_cast<audio::PlaylistTrack**>(g_dayNightEntries + 2));
        g_playlist.repeatMode = 1;
        g_playlist.modeFilter = 0;
        g_player = {};
        g_player.playlist = &g_playlist;
        g_player.forced = 1;
        g_player.lastIndex = -1;
        int ok = g_playlist.PlayCurrent(0);
        OBL_LOG("autotest: music-night-playlist success=%d", ok);
    } else if (name == "music-night" && args.size() >= 1) {
        // music-night <0|1> [manual forced]: MusicPlayer::SetNight on the fixture player.
        if (args.size() >= 3) {
            g_player.manual = std::atoi(args[1].c_str());
            g_player.forced = std::atoi(args[2].c_str());
        }
        g_player.SetNight(std::atoi(args[0].c_str()));
        OBL_LOG("autotest: music-night filter=%d index=%d last=%d", g_playlist.modeFilter, g_playlist.current, g_player.lastIndex);
    } else if (name == "music-playlist-tick") {
        g_playlist.Update();
    } else if (name == "music-disabled" && args.size() == 1) {
        sound->enabled = std::atoi(args[0].c_str()) ? 0 : 1;
    } else {
        return false;
    }
    return true;
}
} // namespace obl::test
