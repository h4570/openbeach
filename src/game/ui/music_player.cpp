#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_manager_buttons.h"
#include "game/ui/playlist_services.h"
#include "game/ui/music_player.h"
#include "game/game/settings_store.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/gui_lists.h"
#include "game/ui/sound_api.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_controls_types.h"
#include "playlist_logic.h"
#include "playlist_nav.h"
#include "core/call_counter.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

// MusicPlayer::SetNight(night). The Playlist and list-box helpers it calls are the functions of playlist_*.h and
// gui_lists.h.
namespace obl::audio {
namespace {

using ButtonWalk = void (ui::GuiManager::*)(const char*);
using audio::MusicPlayer;
using audio::Playlist;
using ui::GuiListBox;

// `call-drive ui.music-panel-drive` drives a live panel through the native entry (testing/music.cpp registers it).
test::CallCounter s_rebuild("ui.music-panel-drive");
test::CallCounter g_rebuildCounter("ui.music-panel-rebuild");
// Call counters (`call-report ui.music-player`).
test::CallCounter g_counter("ui.music-player");
// List actions and the panel visibility handler (`call-report ui.music-player-actions`).
test::CallCounter g_actionsCounter("ui.music-player-actions");

void RebuildRows(MusicPlayer* player, char clearSelection)
{
    GuiListBox* box = player->list;
    gui::listbox::ClearRows(box);
    if (clearSelection) gui::listbox::ClearSelection(box);
    Playlist* list = player->playlist;
    const int count = list->GetCount();
    const int current = list->current;
    uint32_t color = 0;
    for (int row = 0; row < count; ++row)
        if (list->IsRepeat(row)) color = 0x808080;
    auto textCell = [&](int row, int offset, int width, uint32_t cellColor, const char* text, uint32_t newRow) {
        gui::listbox::AddTextA(box, row, offset, width, 0xf, 2, row == current, cellColor, text, newRow);
    };
    auto imageCell = [&](int row, int offset, const char* graphic) { gui::listbox::AddImage(box, row, offset, graphic); };
    for (int row = 0; row < count; ++row) {
        const char* title = list->GetDisplayTitle(row);
        const char* artist = list->GetArtist(row);
        // Keep the full label until AddTextA truncates it to the actual font width.
        std::string label = artist ? std::string(artist) + " - " + title : title;
        const bool repeat = list->IsRepeat(row) != 0;
        textCell(row, 0, 0xf0, repeat ? 0 : color, label.c_str(), 1);
        if (repeat) imageCell(row, 0xf0, "UI3_mp3_icon_repeat");
        if (player->manual == 0) {
            const int night = list->GetNightFlag(row);
            imageCell(row, 0xff, night == 0 ? "UI3_mp3_icon_sun" : "UI3_mp3_icon_moon");
        }
        const int length = list->GetLengthMs(row);
        if (length != 0) {
            char time[32];
            const int seconds = length / 1000;
            std::snprintf(time, sizeof(time), "%d:%02d", seconds / 60, seconds % 60);
            // Even the repeating row's duration keeps the global grey color.
            textCell(row, 0x11d, 0x23, color, time, 0);
        }
    }
    platform::SyncPlaylistToSettings(player);
}

// The window actions read the selection through the list box: the game advances `m_selCursor` over it (it ends on the last entry),
// which later walks of the same list see, so that side effect is kept.
std::vector<int> TakeSelection(GuiListBox* box)
{
    std::vector<int> rows;
    box->m_selCursor = 0;
    const int count = box->m_selBegin ? static_cast<int>(box->m_selEnd - box->m_selBegin) : 0;
    for (int i = 0; i < count; ++i) {
        box->m_selCursor = i;
        rows.push_back(box->m_selBegin[i]);
    }
    return rows;
}

int SelectionCount(const GuiListBox* box)
{
    return box->m_selBegin ? static_cast<int>(box->m_selEnd - box->m_selBegin) : 0;
}

void DeleteSelectedTracks(MusicPlayer* player)
{
    Playlist* list = player->playlist;
    const int selected = SelectionCount(player->list);
    if (selected == 0) return;
    const std::vector<int> rows = TakeSelection(player->list);
    for (int row : rows) list->MarkForDelete(row);
    const auto plan = playlist::PlanDelete(rows.data(), selected, list->current);
    list->RemoveMarked();
    if (plan.currentDeleted && list->IsStreamActive()) {
        list->SetCurrentIndex(plan.lastRow);
        list->PlayCurrent(0);
    } else if (plan.index != list->current) {
        list->SetCurrentIndex(plan.index);
    }
    player->RebuildList(1);
    gui::listbox::SelectRow(player->list, plan.reselect, 1);
    gui::listbox::EnsureRowVisible(player->list, plan.reselect);
}

// Fewer than two selected rows shuffle the whole playlist; otherwise only the tracks on the selected rows trade places among those rows.
void ShuffleSelectedTracks(MusicPlayer* player)
{
    Playlist* list = player->playlist;
    const int selected = SelectionCount(player->list);
    if (selected < 2) {
        list->Shuffle();
    } else {
        const std::vector<int> rows = TakeSelection(player->list);
        audio::PlaylistTrack** items = list->tracks.data();
        std::vector<audio::PlaylistTrack*> picked;
        for (int row : rows) picked.push_back(items[row]);
        playlist::Shuffle(picked.data(), selected, [] { return game::Rand(); });
        for (int i = 0; i < selected; ++i) items[rows[i]] = picked[i];
    }
    player->RebuildList(0);
}

// Repeat is a single-track flag: toggling the first selected row either clears it or moves it there and starts that track (unless it is
// already the playing one). An empty selection reads past its storage in the game; the callers only offer the action with a selection.
void ToggleRepeatOfSelection(MusicPlayer* player)
{
    Playlist* list = player->playlist;
    const int top = player->list->m_scrollTop;
    player->list->m_selCursor = 0;
    const int row = *player->list->m_selBegin;
    const char repeat = list->IsRepeat(row) ? 0 : 1;
    list->SetRepeat(row, repeat);
    player->RebuildList(0);
    gui::listbox::SetScrollTop(player->list, top);
    if (!repeat) return;
    if (list->current == row && list->IsStreamActive()) return;
    list->SetCurrentIndex(row);
    list->PlayCurrent(0);
}

// Showing the window binds the list box, enables multi-select and puts the playlist filter in step with the day/night switch (command 9
// turns it off: `manual` 1 greys the sun/moon buttons and plays every track); hiding it saves the settings.
void ApplyPanelVisibility(MusicPlayer* player, bool visible)
{
    if (!visible) {
        game::settings_store::Save(game::settings_store::Current());
        player->panelVisible = visible;
        return;
    }
    ui::GuiManager* gui = game::GetGuiManager();
    player->list = static_cast<GuiListBox*>(game::GetGuiManager()->FindListBox("PlayList"));
    if (!player->list) {
        player->panelVisible = visible;
        return;
    }
    player->list->m_multiSelect = 1;
    auto buttons = [&](ButtonWalk walk) {
        for (const char* action : {"MP3_Sun", "MP3_Moon"}) (gui->*walk)(action);
    };
    if (player->manual == 0) {
        buttons(&ui::GuiManager::ResetActionButtons);
        player->playlist->modeFilter = player->night == 0 ? 0 : 1;
    } else if (player->manual == 1) {
        buttons(&ui::GuiManager::DisableActionButtons);
        player->playlist->modeFilter = 2;
    }
    player->RebuildList(1);
    gui::listbox::SelectRow(player->list, player->playlist->current, 1);
    gui::listbox::EnsureRowVisible(player->list, player->playlist->current);
    player->panelVisible = visible;
}

int FirstSelectedRow(GuiListBox* box)
{
    box->m_selCursor = 0;
    return box->m_selBegin ? *box->m_selBegin : 0; // the game reads the first entry unchecked (empty selection: no storage)
}

void ResetButtons(const char* const* names, int count, ButtonWalk walk)
{
    ui::GuiManager* gui = game::GetGuiManager();
    for (int i = 0; i < count; ++i) (gui->*walk)(names[i]);
}

// Commands 0/1 set the time-of-day flag of every selected row, then redraw keeping the scroll position (nothing selected: no-op).
void SetSelectedNight(MusicPlayer* player, int night)
{
    GuiListBox* box = player->list;
    const int top = box->m_scrollTop;
    const int selected = SelectionCount(box);
    if (selected == 0) return;
    box->m_selCursor = 0;
    player->playlist->SetNightFlag(box->m_selBegin[0], night);
    for (int i = 1; i < selected; ++i) {
        int row;
        if (night == 0) {
            row = box->m_selBegin[++box->m_selCursor];
        } else {
            row = gui::listbox::NextSelectedRow(box);
        }
        player->playlist->SetNightFlag(row, night);
    }
    player->RebuildList(0);
    gui::listbox::SetScrollTop(player->list, top);
}

// Commands 5/6: the first selected track trades places with its neighbour and stays selected, the list keeps its scroll position.
void MoveSelected(MusicPlayer* player, bool up)
{
    GuiListBox* box = player->list;
    const int row = FirstSelectedRow(box);
    const int top = box->m_scrollTop;
    int target;
    if (up) {
        player->playlist->MoveUp(row);
        player->RebuildList(1);
        target = row - 1 < 0 ? 0 : row - 1;
    } else {
        player->playlist->MoveDown(row);
        target = row + 1;
        const int last = player->playlist->GetCount() - 1;
        if (last < target) target = last;
        player->RebuildList(1);
    }
    gui::listbox::SelectRow(player->list, target, 1);
    gui::listbox::SetScrollTop(player->list, top);
    gui::listbox::EnsureRowVisible(player->list, target);
}

// After a track change an open window redraws and brings the first selected row into view; `row` is read before the redraw.
void RefreshOpenPanel(MusicPlayer* player)
{
    if (!player->panelVisible) return;
    GuiListBox* box = player->list;
    const int top = box->m_scrollTop;
    const int row = FirstSelectedRow(box);
    player->RebuildList(0);
    gui::listbox::SetScrollTop(player->list, top);
    gui::listbox::EnsureRowVisible(player->list, row);
}

// Command 8 / 9: the day/night switch on (filter follows the time of day, sun/moon buttons live) or off (every track, buttons greyed);
// the saved setting mirrors it.
void SetSwitch(MusicPlayer* player, bool automatic)
{
    static const char* const kButtons[] = {"MP3_Sun", "MP3_Moon"};
    player->manual = automatic ? 0 : 1;
    ResetButtons(kButtons, 2, automatic ? &ui::GuiManager::ResetActionButtons : &ui::GuiManager::DisableActionButtons);
    player->playlist->modeFilter = automatic ? (player->night == 0 ? 0 : 1) : 2;
    player->RebuildList(0);
    game::settings_store::Current().m_playlistMode = automatic ? 1 : 0;
}

// Command 0xf: back to the default playlist (game::settings_store::ResetPlaylist resets the saved list).
void ResetPlaylist(MusicPlayer* player)
{
    static const char* const kButtons[] = {
            "MP3_MoveUp", "MP3_MoveDown", "MP3_Repeat", "MP3_Sun", "MP3_Moon", "MP3_Delete", "MP3_Shuffle", "MP3_AmpPlay"};
    Playlist* list = player->playlist;
    list->StopStream();
    list->Clear();
    game::settings_store::ResetPlaylist(game::settings_store::Current());
    platform::ScanMusicFolder(player, false);
    player->RebuildList(1);
    gui::listbox::SelectRow(player->list, 0, 1);
    if (list->GetCount() != 0) ResetButtons(kButtons, 8, &ui::GuiManager::ResetActionButtons);
}

// MusicPlayer::Command(command); commands outside 0..15 do nothing.
void RunCommand(MusicPlayer* player, int command)
{
    Playlist* list = player->playlist;
    switch (command) {
    case 0:
    case 1: SetSelectedNight(player, command); break;
    case 2: ToggleRepeatOfSelection(player); break;
    case 3: ShuffleSelectedTracks(player); break;
    case 4: DeleteSelectedTracks(player); break;
    case 5: MoveSelected(player, true); break;
    case 6: MoveSelected(player, false); break;
    case 7:
        if (!player->panelVisible) {
            list->PlayCurrent(1);
            break;
        }
        if (SelectionCount(player->list) == 0) break;
        list->SetCurrentIndex(FirstSelectedRow(player->list));
        list->PlayCurrent(0);
        RefreshOpenPanel(player);
        break;
    case 8: SetSwitch(player, true); break;
    case 9: SetSwitch(player, false); break;
    case 10:
        list->AdvanceTrack();
        RefreshOpenPanel(player);
        break;
    case 11:
        list->PreviousTrack();
        RefreshOpenPanel(player);
        break;
    case 12: list->StopStream(); break;
    case 13: list->TogglePause(); break;
    case 14: player->rescan = 1; break;
    case 15: ResetPlaylist(player); break;
    default: break;
    }
}

// Greys or enables the track-list buttons (`reset` false greys). MoveUp/MoveDown/Repeat only make sense for a single selected track.
void SetButtons(std::initializer_list<const char*> names, bool enable)
{
    ResetButtons(names.begin(), static_cast<int>(names.size()),
            enable ? &ui::GuiManager::ResetActionButtons : &ui::GuiManager::DisableActionButtons);
}

constexpr std::initializer_list<const char*> kMoveButtons = {"MP3_MoveUp", "MP3_MoveDown", "MP3_Repeat"};
constexpr std::initializer_list<const char*> kAllButtons = {
        "MP3_MoveUp", "MP3_MoveDown", "MP3_Repeat", "MP3_Sun", "MP3_Moon", "MP3_Delete", "MP3_Shuffle", "MP3_AmpPlay"};

// MusicPlayer::EnableTrackButtons: every track button live once the list has tracks.
void EnableButtons(MusicPlayer* player)
{
    if (player->playlist->GetCount() != 0) SetButtons(kAllButtons, true);
}

// MusicPlayer::Update, called by GuiManager::Update every frame. A pending refresh (command 14) rescans the music folder behind the
// "MP3Refreshing" dialog inside this one GUI update (no simulation tick can run meanwhile, so the game's world pause was vacuous);
// then the playlist ticks, and an open window keeps its buttons in step with the selection size, reacts to a quick click on a row (play
// it) and redraws when the track advanced.
void UpdatePlayer(MusicPlayer* player)
{
    void* gui = game::GetGuiManager();
    Playlist* list = player->playlist;
    if (player->rescan != 0) {
        list->StopStream();
        list->SetCurrentIndex(0);
        list->Clear();
        platform::SyncPlaylistToSettings(player);
        game::settings_store::ResetPlaylist(game::settings_store::Current());
        platform::ScanMusicFolder(player, true);
        player->RebuildList(1);
        gui::listbox::SelectRow(player->list, 0, 1);
        EnableButtons(player);
        player->rescan = 0;
        game::GetGuiManager()->CloseDialog("MP3Refreshing");
    }
    const char advanced = list->Update();
    if (!player->panelVisible) return;
    GuiListBox* box = player->list;
    const int selected = SelectionCount(box);
    if (selected != player->selectedCount) {
        if (selected < 2) {
            if (player->selectedCount > 1) SetButtons(kMoveButtons, true);
        } else if (player->selectedCount < 2) {
            SetButtons(kMoveButtons, false);
        }
        player->selectedCount = selected;
    }
    if (list->GetCount() == 0) SetButtons(kAllButtons, false);
    if (box->m_repeat) RunCommand(player, 7);
    if (advanced) {
        box = player->list;
        const int top = box->m_scrollTop;
        const int row = FirstSelectedRow(box);
        player->RebuildList(0);
        gui::listbox::SetScrollTop(player->list, top);
        gui::listbox::EnsureRowVisible(player->list, row);
    }
}

// Playlist::RestartTimeOfDay: starts the first track of the current time of day from the top (nothing playable:
// the index goes back to 0 and nothing starts).

// The caller pushes a bool promoted to int; the function reads the low byte.

// A switch to `night` re-filters the playlist; when the stream plays a track of the other time of day and the list has tracks of both, the
// current track is closed and the next track of the new time of day starts (the track that was playing is remembered in `lastIndex`).
void ApplyNight(MusicPlayer* player, int night)
{
    player->night = night;
    Playlist* list = player->playlist;
    if (list->GetCount() == 0) return;
    if (list->IsRepeat(list->current) || player->manual != 0) return;
    list->modeFilter = night == 0 ? 0 : 1;
    if (player->forced != 1) return;
    if (!list->IsStreamActive() || !list->HasOtherMode() || !list->HasMode()) return;
    list->StopStream();
    const int playing = list->current;
    if (player->lastIndex != -1) list->SetCurrentIndex(player->lastIndex);
    player->lastIndex = playing;
    list->AdvanceTrack();
    list->PlayCurrent(0);
    if (!player->panelVisible) return;
    // The open window redraws its rows and keeps the scroll position and the first selected row in view. The game reads the first
    // selection entry unchecked (an empty selection has no storage and crashes it); 0 stands in here.
    GuiListBox* box = player->list;
    const int scrollTop = box->m_scrollTop;
    box->m_selCursor = 0;
    const int row = box->m_selBegin ? *box->m_selBegin : 0;
    player->RebuildList(0);
    gui::listbox::SetScrollTop(player->list, scrollTop);
    gui::listbox::EnsureRowVisible(player->list, row);
}

} // namespace
} // namespace obl::audio

namespace obl::audio {
void MusicPlayer::RebuildList(char clearSelection)
{
    g_rebuildCounter.Count();
    RebuildRows(this, clearSelection);
}

MusicPlayer* MusicPlayer::Construct()
{
    playlist = nullptr;
    list = nullptr;
    selectedCount = 0;
    panelVisible = 0;
    forced = 1;
    rescan = 0;
    lastIndex = -1;
    auto* tracks = new Playlist;
    playlist = tracks->Construct(audio::CurrentSound());
    platform::ScanMusicFolder(this, false);
    return this;
}

void MusicPlayer::Destruct()
{
    if (!playlist) return;
    playlist->Destruct();
    delete playlist;
}

void MusicPlayer::SetNight(int newNight)
{
    g_counter.Count();
    ApplyNight(this, newNight);
}

void MusicPlayer::DeleteSelected()
{
    g_actionsCounter.Count();
    DeleteSelectedTracks(this);
}

void MusicPlayer::ShuffleSelected()
{
    g_actionsCounter.Count();
    ShuffleSelectedTracks(this);
}

void MusicPlayer::ToggleRepeatSelected()
{
    g_actionsCounter.Count();
    ToggleRepeatOfSelection(this);
}

void MusicPlayer::OnPanelVisibilityChanged(int visible)
{
    g_actionsCounter.Count();
    ApplyPanelVisibility(this, static_cast<uint8_t>(visible) != 0);
}

void MusicPlayer::Command(int command)
{
    g_actionsCounter.Count();
    RunCommand(this, command);
}

void MusicPlayer::Update()
{
    g_actionsCounter.Count();
    UpdatePlayer(this);
}

void MusicPlayer::EnableTrackButtons()
{
    g_actionsCounter.Count();
    EnableButtons(this);
}

// Nothing playable for the filter: the index goes back to 0 and nothing starts.
void Playlist::RestartTimeOfDay()
{
    g_actionsCounter.Count();
    current = 0;
    const int count = GetCount();
    if (count == 0) return;
    if (modeFilter != 2) {
        const PlaylistTrack* const* items = tracks.data();
        const int found = playlist::FirstOfMode(items, count, modeFilter);
        if (found < 0) return;
        current = found;
    }
    PlayCurrent(0);
}
} // namespace obl::audio

namespace obl::audio {
MusicPlayer* GetMusicPlayer()
{
    ui::GuiManager* gui = game::GetGuiManager();
    return gui ? static_cast<MusicPlayer*>(gui->m_musicPlayer) : nullptr;
}
} // namespace obl::audio
