#pragma once

#include "game/ui/gui_sort.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <functional>
#include <vector>

namespace obl::save_slots {
struct Slot {
    int32_t m_number; // N of the file name
    uint32_t m_time; // save time (32-bit time_t)
    int32_t m_scenario;
    int32_t m_day;
    float m_hour;
    float m_cash;
    int32_t m_guests;
    char m_title[64]; // text-table key; the scanner reads but does not keep the sandbox flag
    bool m_oblSave; // the slot's file is save<N>.obl.save, else the old save<N>.dat (save-switch.md)
};

template<class It> void SortTime(It first, It last)
{
    if (first == last) return;
    // Complement preserves unsigned descending order, including timestamps above INT_MAX.
    gui_sort::ObjectIdSort(first, last, [](auto slot) { return ~uint32_t(slot->m_time); });
}
template<class It> void SortNumber(It first, It last)
{
    if (first == last) return;
    gui_sort::ObjectIdSort(first, last, [](auto slot) { return uint32_t(slot->m_number); });
}
template<class It> int FirstFree(It first, It last)
{
    SortNumber(first, last);
    int number = 0;
    for (auto pos = first; pos != last; ++pos)
        if ((*pos)->m_number == number) ++number;
    SortTime(first, last);
    return number;
}

// The routine floors to a 64-bit value, then zero-extends its low word for the subtraction.
inline int DisplayMinute(float hour)
{
    const auto whole = uint32_t(int64_t(std::floor(double(hour))));
    const double minutes = (double(hour) - double(whole)) * 60.0;
    return minutes >= 45 ? 45 : minutes >= 30 ? 30 : minutes >= 15 ? 15 : 0;
}

constexpr const char* kSaveName = "BeachLife Save";
constexpr float kSaveVersion = 1.6f;

// Slot scans accept the save class and version, including an unordered (NaN) version comparison.
inline bool AcceptHeader(const char* name, float version)
{
    return !std::strcmp(name, kSaveName) && !(std::fabs(double(version) - double(kSaveVersion)) > 1.1920928955078125e-7);
}
// `save<N>.dat` / `save<N>.obl.save`: atoi past the four-letter prefix (a non-number reads as 0).
inline int SlotNumber(const char* fileName)
{
    return std::atoi(fileName + 4);
}
inline std::string SlotPath(int number)
{
    char path[128];
    std::snprintf(path, sizeof(path), "savegames\\save%d.dat", number);
    return path;
}
// A sandbox save stores the title key, not the text; the key is 1-based.
inline std::string SandboxTitleKey(int scenario)
{
    char title[128];
    std::snprintf(title, sizeof(title), "MAINSS_SANDBOX%d_TITLE", scenario + 1);
    return title;
}
// Row 0 of the save list is "new save"; rows 1..count are existing slots. An empty selection reads as row 0.
inline int SelectedRow(const int* selBegin, const int* selEnd)
{
    return selBegin == selEnd ? 0 : *selBegin;
}
inline bool ValidRow(int row, size_t slotCount)
{
    return row >= 0 && static_cast<size_t>(row) <= slotCount;
}
} // namespace obl::save_slots

namespace obl::game {
struct Game;
}

namespace obl::ui {
bool RegisterSaveSlotDrivers();
// Callbacks bracket world construction and restoration of the loaded objects.
struct PendingLoad {
    int scenario = 0;
    bool sandbox = false;
    std::function<void()> beforeLoad, afterLoad;
};
void QueueSaveLoad(PendingLoad load);
bool LoadPendingFor(const game::Game& game);

namespace save_panel {
void ScanSaveSlots();
// ui.save-game-write: the save panel writes the selected slot of the current Game.
void WriteSave();
void ClearSaveSlots();
void FillLoadList();
void FillSaveList();
bool IsExistingSlotSelected();
int SelectedImageId();
void RequestLoadSelected();
void DeleteSelectedSave();
void DeleteSelectedLoad();
void UpdateLoadInfoBox(game::Game* game);
void LoadPendingSave(game::Game* game);
} // namespace save_panel
} // namespace obl::ui
