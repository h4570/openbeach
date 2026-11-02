#include "core/char16.h"
#include "gui_manager_dialogs.h"
#include "gui_sort.h"
#include "game/ui/gui_containers_types.h"
#include "game/game/game_accessors.h"
#include "gui_containers.h"
#include "game/objects/guest.h"
#include "game/ui/gui_sort.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_lists_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstring>

namespace obl::gui {
namespace {
using namespace obl::gui_sort;
using Comparator = ui::GuiSortComparator;
test::CallCounter g_counter("ui.gui-sort"), g_standard("GuiSortStandard"), g_unordered("GuiSortUnordered");
test::CallCounter g_realGuests("GuiSortRealGuests"), g_realBuildings("GuiSortRealBuildings"), g_realStaff("GuiSortRealStaff");
test::CallCounter g_tables("GuiSortTables");

struct GameEnv {
    float GuestValue(const void* object, GuestField field) const
    {
        const auto& row = *static_cast<const object::Guest*>(object);
        switch (field) {
        case GuestField::Happiness: return row.m_happiness;
        case GuestField::Cash: return row.m_cash;
        case GuestField::Sunburn: return object::GuestSunburnSeverity(row);
        case GuestField::Headache: return row.m_desires[4];
        case GuestField::Drunk: return row.m_drunk;
        case GuestField::Bladder: return row.m_desires[14];
        case GuestField::FoodIllness: return row.m_foodIll;
        case GuestField::Price: return row.m_opinion[3];
        case GuestField::Safety: return row.m_opinion[6];
        case GuestField::Crowding: return row.m_opinion[1];
        case GuestField::Beauty: return row.m_opinion[0];
        case GuestField::Cleanliness: return row.m_opinion[5];
        case GuestField::Totty: return row.m_opinion[2];
        case GuestField::Variety: return row.m_opinion[4];
        }
        return 0;
    }
    float BuildingFloat(const void* object, BuildingField field) const
    {
        const auto& row = *static_cast<const object::Building*>(object);
        switch (field) {
        case BuildingField::Income: return row.m_incomeToday;
        case BuildingField::Cost: return row.m_costToday;
        case BuildingField::Profit: return row.m_profitToday;
        default: return row.m_averageProfit;
        }
    }
    int32_t BuildingSigned(const void* object, BuildingField field) const
    {
        const auto& row = *static_cast<const object::Building*>(object);
        switch (field) {
        case BuildingField::RepairState: return static_cast<int32_t>(row.m_RepairState);
        case BuildingField::Power: return row.m_definition->m_Power;
        default: return row.m_NumVisitsToday;
        }
    }
    uint32_t BuildingUnsigned(const void* object, BuildingField field) const
    {
        const auto& row = *static_cast<const object::Building*>(object);
        return static_cast<uint32_t>(field == BuildingField::Age ? row.m_BuildingAge : row.m_NumFaults);
    }
    void Text(const void* object, const char* key, Char16* out) const
    {
        static_cast<object::Base*>(const_cast<void*>(object))->GetVariableText(key, out);
    }
};

template<class Less> void RunSort(test::CallCounter& real, void** begin, void** end, Less less, bool unordered)
{
    real.Count();
    g_counter.Count();
    if (begin == end) return;
    if (unordered) {
        StableSortUnordered(begin, end, less);
        g_unordered.Count();
    } else {
        std::stable_sort(begin, end, less);
        g_standard.Count();
    }
}

void DriveTables(int phase)
{
    auto* gui = game::GetGuiManager();
    if (!gui) {
        OBL_LOG("autotest: FAILED GuiSortTables requires gameplay");
        return;
    }
    gui->m_context[1] = 1;
    gui->m_context[2] = 0x13;
    gui->m_context[10] = 6;
    if (phase == 1) {
        for (const char* name : {"GuestPanel", "ObjectBrowser", "StaffBrowser"}) {
            const int result = gui->ShowDialog(name, nullptr, false);
            g_tables.Record((result & 0xff) != 0, name);
        }
        return;
    }
    for (auto* node = static_cast<ui::GuiManager*>(gui)->m_definitions; node; node = node->m_next) {
        if (std::strcmp(node->m_dialog->m_name, "ObjectBrowser") == 0) {
            container::DialogSelectTab(node->m_dialog, 1);
            break;
        }
    }
    static_cast<ui::GuiManager*>(gui)->RefreshAllDialogs();
}
} // namespace

namespace gui_table_sort {
void Guests(void** begin, void** end, int key, bool reverse)
{
    const auto env = GameEnv{};
    const bool unordered = begin != end && GuestSortNeedsUnordered(env, key, begin, end);
    RunSort(g_realGuests, begin, end, [&](auto lhs, auto rhs) { return GuestLess(env, key, reverse, lhs, rhs); }, unordered);
}
void Buildings(void** begin, void** end, int key, bool reverse)
{
    const auto env = GameEnv{};
    const bool unordered = begin != end && BuildingSortNeedsUnordered(env, key, begin, end);
    RunSort(g_realBuildings, begin, end, [&](auto lhs, auto rhs) { return BuildingLess(env, key, reverse, lhs, rhs); }, unordered);
}
void StaffByTitle(void** begin, void** end)
{
    RunSort(g_realStaff, begin, end, [&](auto lhs, auto rhs) { return StaffTitleLess(GameEnv{}, lhs, rhs); }, false);
}
} // namespace gui_table_sort

bool RegisterGuiSortDrivers()
{
    g_tables.SetDriver(DriveTables);
    return true;
}
} // namespace obl::gui
