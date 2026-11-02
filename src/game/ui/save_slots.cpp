#include "game/game/game.h"
#include "core/text16.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/renderer_api.h"
#include "game/world/world_lifecycle.h"
#include "game/game/game_api.h"
#include "game/ui/save_slots.h"
#include "game/game/world_test_state.h"
#include "game/ui/gui_tooltip.h"
#include "game/ui/gui_manager_buttons.h"
#include "game/ui/gui_lists.h"
#include "game/ui/text_table.h"
#include "game/game/game_accessors.h"
#include "game/game/local_time.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_controls_types.h"
#include "core/log.h"
#include "core/os.h"
#include "game/ui/save_slot_header.h"
#include "game/game/save_load.h"
#include "core/call_counter.h"
#include "game/game/diagnostics.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <tuple>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <vector>

namespace obl::ui {
namespace {
using SaveSlot = save_slots::Slot;
// The drivers (`call-drive`) are attached to these; the fixtures count into ui.save-slots-fixture.
test::CallCounter g_scan("ui.save-game-scan-slots"), g_write("ui.save-game-write");
test::CallCounter g_checks("ui.save-slots-fixture");

template<class Sort> auto Reorder(game::Game* game, Sort sort)
{
    std::vector<SaveSlot*> order;
    order.reserve(game->m_saveSlots.size());
    for (auto& slot : game->m_saveSlots) order.push_back(slot.release());
    const auto result = sort(order.begin(), order.end());
    for (size_t i = 0; i < order.size(); ++i) game->m_saveSlots[i].reset(order[i]);
    return result;
}
void Clear(game::Game* game)
{
    game->m_saveSlots.clear();
    game->m_loadListFilled = 0;
}
void Scan(game::Game* game)
{
    Clear(game);
    for (const os::DirectoryEntry& entry : os::ListDirectory("savegames")) {
        if (!os::MatchesPattern(entry.name, "save", ".obl.save")) continue;
        std::ifstream in(os::ResolvePath("savegames/" + entry.name), std::ios::binary);
        auto slot = std::make_unique<SaveSlot>();
        if (!in || !save_slots::ReadOblHeader(in, *slot)) continue;
        slot->m_number = save_slots::SlotNumber(entry.name.c_str());
        slot->m_oblSave = true;
        game->m_saveSlots.push_back(std::move(slot));
    }
    Reorder(game, [](auto first, auto last) {
        save_slots::SortTime(first, last);
        return 0;
    });
}
ui::GuiListBox* SaveList(game::Game* game)
{
    return static_cast<ui::GuiListBox*>(game->m_guiManager.get()->FindListBox("SaveListBox"));
}
int Number(game::Game* game, int row)
{
    if (row != 0) return game->m_saveSlots[row - 1]->m_number;
    return Reorder(game, [](auto first, auto last) { return save_slots::FirstFree(first, last); });
}
void Write(game::Game* game, const uint32_t* timestamp = nullptr)
{
    auto* list = SaveList(game);
    if (!list) return;
    list->m_selCursor = 0;
    const int row = save_slots::SelectedRow(list->m_selBegin, list->m_selEnd);
    if (!save_slots::ValidRow(row, game->m_saveSlots.size())) return;
    const int number = Number(game, row);
    std::string error;
    if (!game::WriteOblSave(
                os::ResolvePath(save_slots::OblSlotPath(number)), timestamp ? *timestamp : uint32_t(std::time(nullptr)), error)) {
        OBL_LOG("save slot %d: %s", number, error.c_str());
        return;
    }
    std::error_code removed;
    std::filesystem::remove(os::ResolvePath(save_slots::SlotPath(number)), removed);
}
std::vector<SaveSlot> Snapshot(game::Game* game)
{
    std::vector<SaveSlot> slots;
    for (const auto& value : game->m_saveSlots) {
        SaveSlot slot{};
        // Bytes after the title's terminator are not compared.
        std::memcpy(&slot, value.get(), offsetof(SaveSlot, m_title));
        std::snprintf(slot.m_title, sizeof(slot.m_title), "%s", value->m_title);
        slots.push_back(slot);
    }
    return slots;
}
using List = ui::GuiListBox;
List* FindList(game::Game* game, const char* name)
{
    return static_cast<List*>(game->m_guiManager.get()->FindListBox(name));
}
void ClearRows(List* list)
{
    gui::listbox::ClearRows(list);
}
void ClearSelection(List* list)
{
    gui::listbox::ClearSelection(list);
}
void Select(List* list, int row)
{
    gui::listbox::SelectRow(list, uint32_t(row), 1);
}
const Char16* Text(const char* key)
{
    return text_table::Lookup(key);
}
void AddText(List* list, int row, int x, int width, const Char16* text, bool newRow)
{
    gui::listbox::AddText(list, row, x, width, 15, 2, 0, 0, text, uint32_t(newRow));
}
void TimeText(Char16* out, const char* key, uint32_t stamp)
{
    const std::tm time = platform::LocalTime(int32_t(stamp));
    text16::Format(out, 128, Text(key), time.tm_hour, time.tm_min, time.tm_mday, time.tm_mon + 1, time.tm_year + 1900);
}
void FillSlots(game::Game* game, List* list, int row, bool clear)
{
    if (clear) ClearRows(list);
    list->m_multiSelect = 0;
    game->m_selectedSaveSlot = nullptr;
    Char16 text[128];
    for (const auto& slot : game->m_saveSlots) {
        AddText(list, row, 0, 300, Text(slot->m_title), true);
        text16::Format(text, std::size(text), Text("LOADSAVE_INFO_CONTENTS"), slot->m_day + 1, double(slot->m_cash));
        AddText(list, row, 279, 106, text, false);
        TimeText(text, "LOADSAVE_TIME_CONTENTS", slot->m_time);
        AddText(list, row++, 390, 115, text, false);
    }
    if (!game->m_saveSlots.empty()) Select(list, 0);
}
void FillLoad(game::Game* game)
{
    FillSlots(game, FindList(game, "LoadListBox"), 0, true);
    game->m_loadListFilled = 1;
}
void FillSave(game::Game* game)
{
    auto* list = SaveList(game);
    ClearRows(list);
    AddText(list, 0, 0, 260, Text("LOADSAVE_SAVESLOT_EMPTY"), true);
    Select(list, 0);
    FillSlots(game, list, 1, false);
}
bool Existing(game::Game* game)
{
    auto* list = SaveList(game);
    list->m_selCursor = 0;
    return save_slots::SelectedRow(list->m_selBegin, list->m_selEnd) != 0;
}
int ImageId(game::Game* game)
{
    return game->m_selectedSaveSlot ? game->m_selectedSaveSlot->m_scenario : -1;
}
void UpdateInfo(game::Game* game)
{
    game->m_selectedSaveSlot = nullptr;
    if (!game->m_loadListFilled) return;
    auto* info = FindList(game, "LoadInfoBox");
    auto* list = FindList(game, "LoadListBox");
    info->m_enabled = 0;
    ClearRows(info);
    if (list->m_selBegin == list->m_selEnd) return;
    list->m_selCursor = 0;
    const SaveSlot* slot = game->m_saveSlots[*list->m_selBegin].get();
    game->m_selectedSaveSlot = slot;
    AddText(info, 0, 0, 350, Text(slot->m_title), true);
    Char16 text[128];
    const int hour = int(std::floor(slot->m_hour));
    const int minute = save_slots::DisplayMinute(slot->m_hour);
    text16::Format(text, std::size(text), Text("LOADSAVE_DAYTIME_CONTENTS"), slot->m_day + 1, hour, minute);
    AddText(info, 1, 0, 350, text, true);
    text16::Format(text, std::size(text), Text("LOADSAVE_CASH_CONTENTS"), double(slot->m_cash));
    AddText(info, 2, 0, 350, text, true);
    TimeText(text, "LOADSAVE_INFO_TIME_CONTENTS", slot->m_time);
    AddText(info, 3, 0, 350, text, true);
    text16::Format(text, std::size(text), Text("LOADSAVE_GUESTS_CONTENTS"), slot->m_guests);
    AddText(info, 4, 0, 350, text, true);
}
void DeleteSelected(game::Game* game, bool load)
{
    auto* list = FindList(game, load ? "LoadListBox" : "SaveListBox");
    list->m_selCursor = 0;
    const int row = *list->m_selBegin;
    if (load) {
        ClearRows(list);
        ClearSelection(list);
    }
    std::remove(os::ResolvePath(save_slots::SlotPath(*game->m_saveSlots[row - (load ? 0 : 1)])).c_str());
    Clear(game);
    Scan(game);
}
void RequestLoad(game::Game* game)
{
    auto* list = FindList(game, "LoadListBox");
    list->m_selCursor = 0;
    game->m_loadSlot = *list->m_selBegin;
    game->m_nextState = game->m_states[1].get();
}
void DeleteSave(game::Game* game)
{
    DeleteSelected(game, false);
}
void DeleteLoad(game::Game* game)
{
    DeleteSelected(game, true);
}

// The obl.save load queued by QueueSaveLoad; LoadPending loads it instead of a slot file.
std::optional<PendingLoad> g_pendingLoad;
void LoadWorld(game::Game* game, int scenario, bool sandbox);
void LoadPending(game::Game* game)
{
    if (g_pendingLoad) {
        const PendingLoad load = std::move(*g_pendingLoad);
        g_pendingLoad.reset();
        Clear(game);
        game->m_loadSlot = -1;
        if (load.beforeLoad) load.beforeLoad();
        LoadWorld(game, load.scenario, load.sandbox);
        if (load.afterLoad) load.afterLoad();
        return;
    }
    const SaveSlot& slot = *game->m_saveSlots[game->m_loadSlot];
    // The document through QueueOblSave, then the queued load above.
    const std::string path = save_slots::SlotPath(slot);
    std::string error;
    const bool queued = game::QueueOblSave(os::ResolvePath(path), error);
    Clear(game);
    game->m_loadSlot = -1;
    if (!queued) {
        OBL_LOG("load %s: %s", path.c_str(), error.c_str());
        return;
    }
    LoadPending(game);
}
void LoadWorld(game::Game* game, int scenario, bool sandbox)
{
    game->m_currentState->Exit();
    game->m_sandbox = sandbox;
    game::game_api::LoadScenarioProgress();
    auto& worldTest = static_cast<game::WorldTestState&>(*game->m_states[static_cast<int>(game::GameStateId::WorldTest)]);
    game->m_currentState = game->m_nextState = &worldTest;
    game->m_lastUnlockedScenario = scenario;
    worldTest.m_loadingSave = 1;
    worldTest.Enter();
    obl::ui::renderer::SetGuiBarHeight(game::GetRenderer(), 0);
    world::LoadFromSave(*game::GetWorld());
    worldTest.FinishLoad();
    auto* gui = game::GetGuiManager();
    gui::UpdateGuiBarHeight(gui);
    gui->UpdateFerryButtonGraphic();
}

test::CallCounter g_checkClearSaveSlots("ui.save-slots-clear-save-slots");
test::CallCounter g_checkDeleteSelectedSave("ui.save-slots-delete-selected-save");
test::CallCounter g_checkDeleteSelectedLoad("ui.save-slots-delete-selected-load");
test::CallCounter g_checkFillLoadList("ui.save-slots-fill-load-list");
test::CallCounter g_checkFillSaveList("ui.save-slots-fill-save-list");
test::CallCounter g_checkUpdateLoadInfoBox("ui.save-slots-update-load-info-box");
test::CallCounter g_checkRequestLoadSelectedSave("ui.save-slots-request-load-selected-save");
test::CallCounter g_checkLoadPendingSave("ui.save-slots-load-pending-save");
test::CallCounter g_fill("ui.game-fill-save-slot-list");
test::CallCounter g_existing("ui.game-is-existing-save-slot-selected"), g_image("ui.game-get-selected-save-image-id");

// Frees a fixture copy's own records and their storage (GameCopy).
void Release(game::Game* game)
{
    Clear(game);
    std::vector<std::unique_ptr<SaveSlot>>().swap(game->m_saveSlots);
}
struct FixtureDirectory {
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path path = previous / "tmp" / "save-slots-fixture";
    FixtureDirectory()
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path / "savegames");
        std::filesystem::current_path(path);
    }
    ~FixtureDirectory()
    {
        std::filesystem::current_path(previous);
        std::filesystem::remove_all(path);
    }
};
// The real save folder for a fixture that writes obl.save files (the writer reads the pack relative to the game directory, so
// FixtureDirectory's working directory would hide it): the files that were not there at the start are removed at the end.
struct NewSavesRemoved {
    std::vector<std::string> before = Names();
    static std::vector<std::string> Names()
    {
        std::vector<std::string> names;
        for (const os::DirectoryEntry& entry : os::ListDirectory("savegames")) names.push_back(entry.name);
        return names;
    }
    ~NewSavesRemoved()
    {
        for (const std::string& name : Names())
            if (std::find(before.begin(), before.end(), name) == before.end()) std::remove(os::ResolvePath("savegames/" + name).c_str());
    }
};
// An old `.dat` save header in the record format of docs/architecture/data-formats/converter/save-conversion.md: a type tag, then the value (strings and the stream
// signature as a length and bytes).
void Header(const char* path, int number)
{
    std::ofstream out(os::ResolvePath(path), std::ios::binary);
    auto record = [&](uint8_t tag, const void* bytes, size_t size) {
        out.put(static_cast<char>(tag));
        out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    };
    auto text = [&](uint8_t tag, const char* value) {
        const auto size = static_cast<int32_t>(std::strlen(value));
        record(tag, &size, 4);
        out.write(value, size);
    };
    auto integer = [&](uint32_t value) { record(4, &value, 4); };
    auto real = [&](float value) { record(3, &value, 4); };
    text(0, "Binary Serial Stream");
    text(0, "BeachLife Save");
    real(1.6f);
    integer(uint32_t(number % 3 == 0 ? 0xffffffff : number % 3));
    integer(2);
    integer(uint32_t(number + 10));
    real(12.5f);
    real(4321.25f);
    integer(7);
    text(1, "TEST_TITLE");
    const uint8_t sandbox = 1;
    record(2, &sandbox, 1);
}
// An obl.save with Header's values and no sections: the slot list reads the header alone. `format` names another document for
// the rejected fixtures; `truncated` ends inside the header.
void OblHeader(const std::string& path, int number, const char* format = "obl.save", bool truncated = false)
{
    std::ofstream out(os::ResolvePath(path), std::ios::binary);
    out << R"({"format": ")" << format << R"(", "version": 2, "header": {"saved_at": )" << (number % 3 == 0 ? 0xffffffffu : number % 3)
        << R"(, "scenario": "sandbox3", "sandbox": true, "title": "TEST_TITLE", "title_text": "Test", "day": )" << number + 10;
    if (truncated) return;
    out << R"(, "day_time": 12.5, "cash": 4321.25, "guests": 7, "packs": [{"id": "p"}], "converted_from": null}, "world": {}})";
}
// A fixture's copy of the live Game: its bytes only and never destroyed, so the states and scenario names stay owned by the live
// Game alone. The save-slot list is the copy's own (empty at the start, freed by Release).
struct GameCopy {
    GameCopy()
    {
        std::memcpy(static_cast<void*>(&game), obl::game::GetGame(), sizeof(obl::game::Game));
        std::construct_at(&game.m_saveSlots);
    }
    ~GameCopy() {}
    union {
        obl::game::Game game;
    };
};
void DriveScan(int count)
{
    FixtureDirectory directory;
    GameCopy copy;
    obl::game::Game& game = copy.game;
    constexpr int sizes[] = {0, 1, 6, 16, 17, 33, 279};
    for (int pass = 0; pass < count; ++pass) {
        for (const auto& file : std::filesystem::directory_iterator(os::ResolvePath("savegames"))) std::filesystem::remove(file.path());
        const int size = sizes[pass % std::size(sizes)];
        for (int i = 0; i < size; ++i) OblHeader(save_slots::OblSlotPath(i * 2), i);
        OblHeader("savegames/save-other.obl.save", 1, "other");
        OblHeader("savegames/save-cut.obl.save", 1, "obl.save", true);
        Header("savegames/save1.dat", 1); // a valid old save: not listed since step 4b
        if (size) OblHeader("savegames/save000.obl.save", 0); // duplicate slot number
        game.m_loadListFilled = 1;
        Scan(&game);
        const bool oblRead = std::all_of(game.m_saveSlots.begin(), game.m_saveSlots.end(), [](const auto& slot) {
            return slot->m_oblSave && slot->m_scenario == 2 && slot->m_day == slot->m_number / 2 + 10 && slot->m_hour == 12.5f &&
                    slot->m_cash == 4321.25f && slot->m_guests == 7 && !std::strcmp(slot->m_title, "TEST_TITLE");
        });
        g_checks.Record(game.m_saveSlots.size() == size_t(size + (size != 0)) && !game.m_loadListFilled && oblRead,
                "fixture size=" + std::to_string(size));
    }
    Release(&game);
}
void DriveWrite(int count)
{
    NewSavesRemoved saves;
    GameCopy copy;
    obl::game::Game& game = copy.game;
    auto* list = SaveList(&game);
    if (!list) {
        OBL_LOG("autotest: FAILED SaveSlots requires SaveListBox");
        return;
    }
    auto* const first = list->m_selBegin;
    auto* const last = list->m_selEnd;
    auto* const cap = list->m_selCap;
    const int cursor = list->m_selCursor;
    int row{};
    list->m_selBegin = &row;
    list->m_selEnd = &row + 1;
    list->m_selCap = &row + 1;
    for (int pass = 0; pass < count; ++pass) {
        game.m_sandbox = uint8_t(pass % 2);
        // New slot, overwrite first row, overwrite last row, empty selection (operation regression).
        Scan(&game);
        row = pass % 4 == 1 ? 1 : pass % 4 == 2 ? int(game.m_saveSlots.size()) : 0;
        const bool empty = pass % 4 == 3;
        int expectedNumber{};
        if (row)
            expectedNumber = game.m_saveSlots[row - 1]->m_number;
        else {
            std::vector<SaveSlot*> numbering;
            for (const auto& slot : game.m_saveSlots) numbering.push_back(slot.get());
            expectedNumber = save_slots::FirstFree(numbering.begin(), numbering.end());
        }
        list->m_selBegin = empty ? nullptr : &row;
        list->m_selEnd = empty ? nullptr : &row + 1;
        // WriteOblSave's header reads the live Game, so the copy's sandbox flag is mirrored there for the write.
        const uint8_t liveSandbox = obl::game::GetGame()->m_sandbox;
        obl::game::GetGame()->m_sandbox = game.m_sandbox;
        Write(&game);
        obl::game::GetGame()->m_sandbox = liveSandbox;
        list->m_selBegin = &row;
        list->m_selEnd = &row + 1;
        Scan(&game);
        SaveSlot* written{};
        for (const auto& slot : game.m_saveSlots)
            if (slot->m_number == expectedNumber) written = slot.get();
        const auto* world = obl::game::GetWorld();
        char title[128];
        std::snprintf(title, sizeof(title), "MAINSS_SANDBOX%d_TITLE", game.m_lastUnlockedScenario + 1);
        g_checks.Record(written && written->m_scenario == game.m_lastUnlockedScenario && written->m_day == world->m_gameDay &&
                        written->m_hour == world->m_gameDayTime && written->m_cash == world->m_player->m_cash &&
                        written->m_guests == world->m_numGuests &&
                        !std::strcmp(written->m_title, game.m_sandbox ? title : world->m_scenarioTitle.c_str()),
                "written slot=" + std::to_string(expectedNumber) + (empty ? " empty selection" : " selected") +
                        (written ? " title=" + std::string(written->m_title) : std::string(" missing")));
    }
    list->m_selBegin = first;
    list->m_selEnd = last;
    list->m_selCap = cap;
    list->m_selCursor = cursor;
    Release(&game);
}
test::CallCounter g_listChecks("ui.save-slots-lists-checks"), g_loadChecks("ui.save-slots-load-checks");
struct ListSnapshot {
    uint32_t rows{};
    int scrollTop{}, cursor{};
    uint8_t multi{}, enabled{};
    std::vector<int> selection;
    using Cell = std::tuple<int, int, int, int, int, Text16>;
    std::vector<Cell> cells;
    bool operator==(const ListSnapshot&) const = default;
};
ListSnapshot SnapshotList(List* list)
{
    ListSnapshot out{list->m_rowCount, list->m_scrollTop, list->m_selCursor, list->m_multiSelect, list->m_enabled};
    if (list->m_selBegin != list->m_selEnd) out.selection.assign(list->m_selBegin, list->m_selEnd);
    for (auto** it = list->m_cellsBegin; it != list->m_cellsEnd; ++it) {
        const auto* cell = *it;
        const auto* widget = static_cast<ui::GuiText*>(cell->m_widget);
        const auto* text = widget->m_text ? widget->m_text : widget->m_ownedText;
        out.cells.emplace_back(cell->m_row, widget->m_baseX0, widget->m_baseY0, widget->m_baseX1, widget->m_baseY1, text ? text : u"");
    }
    return out;
}
void ResetList(List* list)
{
    ClearRows(list);
    ClearSelection(list);
    list->m_selCursor = 0;
    list->m_scrollTop = 0;
    list->m_multiSelect = 1;
    list->m_enabled = 1;
}
void Record(test::CallCounter& check, bool equal, const char* what)
{
    check.Record(equal, what);
    g_listChecks.Record(equal, what);
}
void DriveLists(int count)
{
    GameCopy copy;
    obl::game::Game& game = copy.game;
    auto* save = SaveList(&game);
    auto* load = FindList(&game, "LoadListBox");
    auto* info = FindList(&game, "LoadInfoBox");
    constexpr int sizes[] = {0, 1, 17, 33, 279};
    for (int pass = 0; pass < count; ++pass) {
        Clear(&game);
        const int size = sizes[pass % std::size(sizes)];
        for (int i = 0; i < size; ++i) {
            auto slot = std::make_unique<SaveSlot>();
            slot->m_number = i * 2;
            slot->m_time = 1700000000u + uint32_t(i * 61);
            slot->m_scenario = i % 12;
            slot->m_day = i;
            slot->m_hour = float(i % 24) + float(i % 4) * 0.25f;
            slot->m_cash = 12345.25f - float(i * 500);
            slot->m_guests = i * 7;
            std::snprintf(slot->m_title, sizeof(slot->m_title), "MAINSS_SANDBOX%d_TITLE", i % 12 + 1);
            game.m_saveSlots.push_back(std::move(slot));
        }
        auto compare = [&](test::CallCounter& check, List* list, auto operation, const char* what) {
            ResetList(list);
            operation();
            const auto actual = SnapshotList(list);
            const bool isInfo = list == info, isSave = list == save;
            const auto rows = uint32_t(isInfo ? (game.m_selectedSaveSlot ? 5 : 0) : size + (isSave ? 1 : 0));
            const auto cells = size_t(isInfo ? rows : size * 3 + (isSave ? 1 : 0));
            Record(check, actual.rows == rows && actual.cells.size() == cells && (isInfo || !actual.multi) && (!isInfo || !actual.enabled),
                    what);
        };
        compare(g_fill, load, [&] { FillSlots(&game, load, 0, true); }, "slot rows");
        compare(g_checkFillLoadList, load, [&] { FillLoad(&game); }, "load rows");
        Record(g_checkFillLoadList, game.m_loadListFilled == 1 && !game.m_selectedSaveSlot, "load flag");
        compare(g_checkFillSaveList, save, [&] { FillSave(&game); }, "save rows");
        for (int row : {0, size}) {
            Select(save, row);
            save->m_selCursor = 17;
            const bool expected = row != 0;
            save->m_selCursor = 17;
            Record(g_existing, Existing(&game) == expected && save->m_selCursor == 0, "existing row");
        }
        for (int row : {-1, 0, size - 1}) {
            if (row >= size) continue;
            ResetList(load);
            FillLoad(&game);
            ClearSelection(load);
            if (row >= 0) Select(load, row);
            compare(g_checkUpdateLoadInfoBox, info, [&] { UpdateInfo(&game); }, "info rows");
            Record(g_image, ImageId(&game) == (row < 0 ? -1 : row % 12), "image scenario");
            if (row >= 0) {
                const auto* next = game.m_nextState;
                const int pending = row;
                const auto* expectedNext = game.m_states[1].get();
                game.m_loadSlot = -1;
                game.m_nextState = const_cast<obl::game::GameState*>(next);
                RequestLoad(&game);
                Record(g_checkRequestLoadSelectedSave, pending == game.m_loadSlot && game.m_nextState == expectedNext,
                        "pending index and state");
            }
        }
        game.m_loadListFilled = 0;
        static const SaveSlot selected{};
        game.m_selectedSaveSlot = &selected;
        UpdateInfo(&game);
        Record(g_checkUpdateLoadInfoBox, !game.m_selectedSaveSlot, "unfilled info reset");
        Clear(&game);
        const bool empty = game.m_saveSlots.empty() && !game.m_loadListFilled;
        game.m_loadListFilled = 1;
        Clear(&game);
        Record(g_checkClearSaveSlots, empty && game.m_saveSlots.empty() && !game.m_loadListFilled, "clear records");
    }
    ResetList(save);
    ResetList(load);
    ResetList(info);
    Release(&game);
}
void DriveDeletes(int count)
{
    FixtureDirectory directory;
    GameCopy copy;
    obl::game::Game& game = copy.game;
    for (int pass = 0; pass < count; ++pass) {
        int selectedRow{};
        const bool load = pass % 2 != 0;
        auto* list = FindList(&game, load ? "LoadListBox" : "SaveListBox");
        auto& check = load ? g_checkDeleteSelectedLoad : g_checkDeleteSelectedSave;
        auto* const savedBegin = list->m_selBegin;
        auto* const savedEnd = list->m_selEnd;
        auto* const savedCap = list->m_selCap;
        const int countSlots = pass % 3 == 0 ? 1 : pass % 3 == 1 ? 17 : 279;
        auto prepare = [&] {
            for (const auto& file : std::filesystem::directory_iterator(os::ResolvePath("savegames"))) std::filesystem::remove(file.path());
            for (int i = 0; i < countSlots; ++i) OblHeader(save_slots::OblSlotPath(i * 2), i);
            Scan(&game);
            selectedRow = countSlots - 1 + (load ? 0 : 1);
            // No date formatting here: the scanner fixtures include unsigned timestamps.
            ClearSelection(list);
            list->m_selBegin = &selectedRow;
            list->m_selEnd = list->m_selCap = &selectedRow + 1;
            list->m_selCursor = 99;
        };
        const bool expectedEmpty = load;
        prepare();
        const int deletedNumber = game.m_saveSlots[countSlots - 1]->m_number;
        const std::string deletedPath = save_slots::SlotPath(*game.m_saveSlots[countSlots - 1]);
        DeleteSelected(&game, load);
        const auto actual = Snapshot(&game);
        Record(check,
                !std::filesystem::exists(os::ResolvePath(deletedPath)) &&
                        std::none_of(actual.begin(), actual.end(), [&](const SaveSlot& slot) { return slot.m_number == deletedNumber; }) &&
                        actual.size() == size_t(countSlots - 1) && !game.m_loadListFilled &&
                        (list->m_selBegin == list->m_selEnd) == expectedEmpty && list->m_selCursor == 0,
                "delete file and rescan");
        list->m_selBegin = savedBegin;
        list->m_selEnd = savedEnd;
        list->m_selCap = savedCap;
    }
    ClearSelection(SaveList(&game));
    ClearSelection(FindList(&game, "LoadListBox"));
    Release(&game);
}
void DriveLoad(int count)
{
    for (int pass = 0; pass < count; ++pass) {
        auto* game = obl::game::GetGame();
        constexpr int number = 2140000000;
        const auto path = save_slots::OblSlotPath(number);
        if (save_slots::SlotFileExists(number)) {
            g_loadChecks.Record(false, "reserved fixture path exists");
            return;
        }
        GameCopy fixture;
        obl::game::Game& copy = fixture.game;
        auto* slot = copy.m_saveSlots.emplace_back(std::make_unique<SaveSlot>()).get();
        slot->m_number = number;
        auto* list = SaveList(&copy);
        // Selection uses a real allocator-backed list buffer because entering the state destroys GUI widgets.
        slot->m_time = 1700000000;
        std::strcpy(slot->m_title, "MAINSS_SANDBOX1_TITLE");
        FillSave(&copy);
        Select(list, 1);
        const auto* world = obl::game::GetWorld();
        const int day = world->m_gameDay, guests = world->m_numGuests;
        const float hour = world->m_gameDayTime, cash = world->m_player->m_cash;
        const int scenario = game->m_lastUnlockedScenario;
        const uint8_t sandbox = game->m_sandbox;
        Write(&copy);
        Release(&copy);
        Scan(game);
        int row{};
        for (const auto& value : game->m_saveSlots) {
            if (value->m_number == number) break;
            ++row;
        }
        game->m_loadSlot = row;
        LoadPending(game);
        world = obl::game::GetWorld();
        const bool equal = game->m_loadSlot == -1 && game->m_saveSlots.empty() && !game->m_loadListFilled &&
                game->m_currentState == game->m_states[0].get() && game->m_nextState == game->m_states[0].get() &&
                game->m_lastUnlockedScenario == scenario && game->m_sandbox == sandbox && world->m_gameDay == day &&
                world->m_gameDayTime == hour && world->m_numGuests == guests && world->m_player->m_cash == cash;
        g_loadChecks.Record(equal, "world and state after full save load");
        g_checkLoadPendingSave.Record(equal, "world and state after full save load");
        std::remove(os::ResolvePath(path).c_str());
    }
}

} // namespace

namespace save_panel {
void ScanSaveSlots()
{
    Scan(obl::game::GetGame());
}
void WriteSave()
{
    Write(obl::game::GetGame());
}
void ClearSaveSlots()
{
    Clear(obl::game::GetGame());
}
void FillLoadList()
{
    FillLoad(obl::game::GetGame());
}
void FillSaveList()
{
    FillSave(obl::game::GetGame());
}
bool IsExistingSlotSelected()
{
    return Existing(obl::game::GetGame());
}
int SelectedImageId()
{
    return ImageId(obl::game::GetGame());
}
void RequestLoadSelected()
{
    RequestLoad(obl::game::GetGame());
}
void DeleteSelectedSave()
{
    DeleteSave(obl::game::GetGame());
}
void DeleteSelectedLoad()
{
    DeleteLoad(obl::game::GetGame());
}
void UpdateLoadInfoBox(obl::game::Game* game)
{
    UpdateInfo(game);
}
void LoadPendingSave(obl::game::Game* game)
{
    LoadPending(game);
}
} // namespace save_panel

void QueueSaveLoad(PendingLoad load)
{
    g_pendingLoad = std::move(load);
}
bool LoadPendingFor(const obl::game::Game& game)
{
    return game.m_loadSlot != -1 || g_pendingLoad.has_value();
}

bool RegisterSaveSlotDrivers()
{
    static test::CallCounter listChecks("ui.save-slots-drive-lists"), deleteChecks("ui.save-slots-drive-deletes");
    listChecks.SetDriver(DriveLists);
    deleteChecks.SetDriver(DriveDeletes);
    g_loadChecks.SetDriver(DriveLoad);
    g_scan.SetDriver(DriveScan);
    g_write.SetDriver(DriveWrite);
    return true;
}

} // namespace obl::ui
