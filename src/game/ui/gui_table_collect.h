#pragma once

#include "game/tables/building_behavior.h"
#include "game/ui/gui_sort.h"
#include "game/ui/gui_table_frame.h"
#include "game/ui/gui_manager_types.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// The six row-data procedures of `GuiTable`: table::Update{Guests, Buildings, PowerGenerators,
// BuildingsClosedPower, BuildingCommodities, Staff}, which `Refresh`/`Update` call through `CollectRows`. Each gathers the rows into
// the table's pointer vector no more often than every `kRefreshPeriod` seconds, publishes the count and lets the frame sync the scroll bar.
// Game access (the object manager, the panel filters, sorting, the vector allocator) goes through an `Env`:
//   Objects()                        g_ObjectManager             Gui()                      g_GuiManager
//   VariableProvider()               GuiManager::m_variableProvider
//   IsOpen(building)                 the building is open        StaffList()                the staff without service staff (a std::vector)
//   SortGuests / SortBuildings(begin, end, key, reverse)         SortStaffByTitle(begin, end)     SortStaffById(begin, end)
//   Sync(table)                      table::SyncScrollAndSelection (the shared `Env` of gui_table_frame.h supplies the allocator for PushWidget)
//   SetContext(index, value)         GuiManager::SetContext
namespace obl::gui_tables {

inline constexpr int32_t kGuestCountContext = 6; // GuiManager::SetContext index that receives the number of guest / building rows
inline constexpr int32_t kStaffClassCount = 6;
inline constexpr uint8_t kStaffClassTypes[kStaffClassCount] = {
        11, 10, 8, 9, 7, 12}; // staff filter 0..5 -> object type; filter 6 shows everyone
inline constexpr int32_t kStaffAllFilter = 6;

inline bool CollectDue(Table& table, float dt)
{
    const double sum = static_cast<double>(dt) + static_cast<double>(table.m_refreshTimer);
    table.m_refreshTimer = static_cast<float>(sum);
    if (!(sum >= static_cast<double>(kRefreshPeriod))) return false;
    table.m_refreshTimer = 0.0f;
    return true;
}

inline uint32_t RowCount(const Table& table)
{
    return static_cast<uint32_t>(table.m_rows.m_end - table.m_rows.m_begin);
}

inline const object::Base* AsObject(const void* object)
{
    return static_cast<const object::Base*>(object);
}

// table::UpdateGuests. Filter 3 keeps male guests (definition gender 0), 4 female (1), anything else everyone; any filter but 0 sorts.
template<class Env> void CollectGuests(Table& table, float dt, Env& env)
{
    if (!CollectDue(table, dt)) return;
    object::Manager& objects = *env.Objects();
    const int32_t filter = env.Gui().m_context[1];
    table.m_rows.m_end = table.m_rows.m_begin;
    for (uint32_t i = 0, count = static_cast<uint32_t>(objects.m_guests.size()); i < count; ++i) {
        object::Base* guest = objects.m_guests[i];
        if (guest->m_waitingForDeath) continue;
        if (filter == 3 || filter == 4) {
            const int32_t gender = static_cast<const object::Guest*>(guest)->m_definition->m_Gender;
            if (gender != (filter == 3 ? 0 : 1)) continue;
        }
        PushWidget(table.m_rows, guest, env);
    }
    table.m_count = RowCount(table);
    if (filter != 0) env.SortGuests(table.m_rows.m_begin, table.m_rows.m_end, filter, table.m_sortParam != 0);
    env.Sync(table);
    env.SetContext(kGuestCountContext, static_cast<int32_t>(table.m_count));
}

// table::UpdateBuildings. Filters 0x13, 0x15 and 0x16 keep buildings that sell commodities, 0x17 those with a non-negative POWER, 0x18 the open
// ones, 0x19 the closed ones, anything else (0x14 included) everyone; any filter but 0 sorts.
template<class Env> void CollectBuildings(Table& table, float dt, Env& env)
{
    if (!CollectDue(table, dt)) return;
    object::Manager& objects = *env.Objects();
    const int32_t filter = env.Gui().m_context[2];
    table.m_rows.m_end = table.m_rows.m_begin;
    for (uint32_t i = 0, count = static_cast<uint32_t>(objects.m_buildings.size()); i < count; ++i) {
        object::Base* building = objects.m_buildings[i];
        if (building->m_waitingForDeath) continue;
        const tables::BuildingDef* def = static_cast<const object::Building*>(building)->m_definition;
        bool keep = true;
        switch (filter) {
        case 0x13:
        case 0x15:
        case 0x16: keep = def->m_NumCommodities != 0; break;
        case 0x17: keep = def->m_Power >= 0; break;
        case 0x18: keep = env.IsOpen(building); break;
        case 0x19: keep = !env.IsOpen(building); break;
        default: break;
        }
        if (keep) PushWidget(table.m_rows, building, env);
    }
    table.m_count = RowCount(table);
    if (filter != 0) env.SortBuildings(table.m_rows.m_begin, table.m_rows.m_end, filter, table.m_sortParam != 0);
    env.Sync(table);
    env.SetContext(kGuestCountContext, static_cast<int32_t>(table.m_count));
}

// (every live building named exactly POWERGEN) and (every live building the power system closed): the manager's order, no sorting,
// no context slot.
template<class Env> void CollectPowerGenerators(Table& table, float dt, Env& env)
{
    if (!CollectDue(table, dt)) return;
    object::Manager& objects = *env.Objects();
    table.m_rows.m_end = table.m_rows.m_begin;
    for (uint32_t i = 0, count = static_cast<uint32_t>(objects.m_buildings.size()); i < count; ++i) {
        object::Base* building = objects.m_buildings[i];
        if (static_cast<const object::Building*>(building)->m_definition->m_Behavior == BuildingBehavior::PowerGenerator &&
                !building->m_waitingForDeath)
            PushWidget(table.m_rows, building, env);
    }
    table.m_count = RowCount(table);
    env.Sync(table);
}

template<class Env> void CollectClosedPower(Table& table, float dt, Env& env)
{
    if (!CollectDue(table, dt)) return;
    object::Manager& objects = *env.Objects();
    table.m_rows.m_end = table.m_rows.m_begin;
    for (uint32_t i = 0, count = static_cast<uint32_t>(objects.m_buildings.size()); i < count; ++i) {
        object::Base* building = objects.m_buildings[i];
        if (static_cast<const object::Building*>(building)->m_closedForPower && !building->m_waitingForDeath)
            PushWidget(table.m_rows, building, env);
    }
    table.m_count = RowCount(table);
    env.Sync(table);
}

// table::UpdateBuildingCommodities: the open window's building is the only element; one row per commodity it sells. The count is stored
// before the provider is pushed (the only procedure that does not count the vector).
template<class Env> void CollectCommodities(Table& table, float dt, Env& env)
{
    if (!CollectDue(table, dt)) return;
    table.m_rows.m_end = table.m_rows.m_begin;
    void* provider = env.VariableProvider();
    table.m_count = static_cast<uint32_t>(static_cast<const object::Building*>(provider)->m_definition->m_NumCommodities);
    PushWidget(table.m_rows, provider, env);
    env.Sync(table);
}

// table::UpdateStaff: the staff list without service staff, minus anyone sacked or being removed. Panel filter 6 sorts by the localized job
// title; every other filter (out-of-range values keep nobody) by unsigned object id. The selected row's id goes to context 0xc (-1: none).
template<class Env> void CollectStaff(Table& table, float dt, Env& env)
{
    if (!CollectDue(table, dt)) return;
    table.m_rows.m_end = table.m_rows.m_begin;
    const int32_t filter = env.Gui().m_context[10];
    for (object::Base* member : env.StaffList()) {
        if (static_cast<const object::Staff*>(member)->m_gotTheSack || member->m_waitingForDeath) continue;
        const bool keep = filter == kStaffAllFilter ||
                (filter >= 0 && filter < kStaffClassCount && static_cast<uint8_t>(member->m_type) == kStaffClassTypes[filter]);
        if (keep) PushWidget(table.m_rows, member, env);
    }
    if (filter == kStaffAllFilter)
        env.SortStaffByTitle(table.m_rows.m_begin, table.m_rows.m_end);
    else if (table.m_rows.m_begin != table.m_rows.m_end)
        env.SortStaffById(table.m_rows.m_begin, table.m_rows.m_end);
    table.m_count = RowCount(table);
    env.Sync(table);
    int32_t id = -1;
    if (table.m_selected != -1) {
        const void* row = table.m_rowSource == 0 ? table.m_rows.m_begin[0] : table.m_rows.m_begin[table.m_selected];
        id = static_cast<int32_t>(AsObject(row)->m_id);
    }
    env.SetContext(kSelectedContext, id);
}

} // namespace obl::gui_tables
