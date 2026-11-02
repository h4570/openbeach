// The row-data procedures of GuiTable (src/game/ui/gui_table_collect.h): the refresh timer, the per-kind filters, the sorting hand-over, the
// published counts and the staff selection id, on plain memory with a recording Env.

#include "object_storage.h"
#include "unit_test.h"
#include "leak_ok.h"
#include "importer/src/readers/building_behavior_keys.h"

#include "game/ui/gui_table_collect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace obl::gui_tables;
using obl::object::Building;
using obl::object::Guest;
using obl::tables::BuildingDef;
using obl::tables::GuestDef;

namespace {

std::string Num(int64_t v)
{
    return std::to_string(v);
}

struct CollectEnv {
    std::vector<std::string> log;
    obl::object::Manager manager;
    obl::ui::GuiManager gui{};
    std::vector<obl::object::Base*> staff;
    std::vector<bool> open; // The building object is open routine by object id
    void* provider = nullptr;

    obl::object::Manager* Objects() { return &manager; }
    obl::ui::GuiManager& Gui() { return gui; }
    void* VariableProvider()
    {
        log.push_back("provider");
        return provider;
    }
    bool IsOpen(void* building)
    {
        log.push_back("open?");
        return open[static_cast<obl::object::Base*>(building)->m_id];
    }
    std::vector<obl::object::Base*> StaffList()
    {
        log.push_back("staff");
        return staff;
    }
    void SortGuests(void** b, void** e, int key, bool reverse)
    {
        log.push_back("sort-guests:" + Num(e - b) + "," + Num(key) + "," + Num(reverse));
    }
    void SortBuildings(void** b, void** e, int key, bool reverse)
    {
        log.push_back("sort-buildings:" + Num(e - b) + "," + Num(key) + "," + Num(reverse));
    }
    void SortStaffByTitle(void** b, void** e) { log.push_back("sort-title:" + Num(e - b)); }
    void SortStaffById(void** b, void** e)
    {
        log.push_back("sort-id:" + Num(e - b));
        obl::gui_sort::ObjectIdSort(b, e, [](void* p) { return static_cast<obl::object::Base*>(p)->m_id; });
    }
    void Sync(Table& table) { log.push_back("sync:" + Num(table.m_count)); }
    void SetContext(int32_t index, int32_t value) { log.push_back("context:" + Num(index) + "," + Num(value)); }
    void* AllocSmall(size_t bytes) { return LeakOk(std::calloc(1, bytes < 16 ? 16 : bytes)); }
    // The GUI records (`new T()` in the game) come from Alloc with the record's size.
    template<class T> T* NewRecord()
    {
        void* memory = Alloc(sizeof(T));
        return memory ? new (memory) T() : nullptr;
    }
    void* Alloc(size_t bytes) { return LeakOk(std::calloc(1, bytes)); }
    void FreeSmall(void*, uint32_t) {}
    void Free(void*) {}
};

std::string Join(const std::vector<std::string>& v)
{
    std::string out;
    for (const auto& s : v) out += s + " ";
    return out;
}

std::vector<uint32_t> Ids(const Table& t)
{
    std::vector<uint32_t> ids;
    for (void** p = t.m_rows.m_begin; p != t.m_rows.m_end; ++p) ids.push_back(static_cast<obl::object::Base*>(*p)->m_id);
    return ids;
}

// A table whose next `dt` of 0.5 s reaches the period.
Table DueTable()
{
    Table table{};
    table.m_refreshTimer = 3.5f;
    table.m_sortParam = 1;
    table.m_selected = -1;
    return table;
}
constexpr float kDue = 0.5f;

struct World {
    CollectEnv env;
    std::vector<std::unique_ptr<uint8_t[]>> memory;
    std::vector<std::unique_ptr<GuestDef>> guestDefs;
    std::vector<std::unique_ptr<BuildingDef>> buildingDefs;

    template<class T> T* Make(uint32_t id, const char* name = "")
    {
        memory.emplace_back(new uint8_t[sizeof(T)]());
        auto* object = &obl::unit::StampVtable<T>(memory.back().get());
        object->m_id = id;
        std::snprintf(object->m_Name, sizeof object->m_Name, "%s", name);
        return object;
    }
    Guest* AddGuest(uint32_t id, int gender, bool dying = false)
    {
        auto* guest = Make<Guest>(id);
        guestDefs.emplace_back(new GuestDef());
        guestDefs.back()->m_Gender = gender;
        guest->m_definition = guestDefs.back().get();
        guest->m_waitingForDeath = dying;
        env.Objects()->AddGuest(guest, id);
        return guest;
    }
    Building* AddBuilding(uint32_t id, const char* name, int commodities = 1, int power = 0, bool open = true, bool dying = false)
    {
        auto* building = Make<Building>(id, name);
        buildingDefs.emplace_back(new BuildingDef());
        buildingDefs.back()->m_NumCommodities = commodities;
        buildingDefs.back()->m_Power = power;
        buildingDefs.back()->m_Behavior = obl::BehaviorFromKey(name);
        building->m_definition = buildingDefs.back().get();
        building->m_waitingForDeath = dying;
        env.open.resize(std::max<size_t>(env.open.size(), id + 1));
        env.open[id] = open;
        env.Objects()->AddBuilding(building, id);
        return building;
    }
    obl::object::Staff* AddStaff(uint32_t id, int type, bool sacked = false, bool dying = false)
    {
        auto* staff = Make<obl::object::Staff>(id);
        staff->m_type = static_cast<obl::object::ObjectType>(type);
        staff->m_gotTheSack = sacked;
        staff->m_waitingForDeath = dying;
        env.staff.push_back(staff);
        return staff;
    }
};

} // namespace

OBL_TEST_CASE("the timer gathers dt and rebuilds only from the period, dropping the surplus")
{
    World w;
    w.AddGuest(1, 0);
    Table table = DueTable();
    CollectGuests(table, 0.25f, w.env);
    OBL_CHECK(table.m_refreshTimer == 3.75f);
    OBL_CHECK(w.env.log.empty());
    CollectGuests(table, 7.0f, w.env);
    OBL_CHECK(table.m_refreshTimer == 0.0f);
    OBL_CHECK(Ids(table) == std::vector<uint32_t>{1});
    table.m_refreshTimer = std::nanf("");
    w.env.log.clear();
    CollectGuests(table, 1.0f, w.env);
    OBL_CHECK(w.env.log.empty());
    OBL_CHECK(std::isnan(table.m_refreshTimer));
}

OBL_TEST_CASE("guests: removal flag, gender filters, sort and the two published values")
{
    World w;
    w.AddGuest(10, 0);
    w.AddGuest(11, 1);
    w.AddGuest(12, 0, true);
    w.AddGuest(13, 1);
    Table table = DueTable();
    w.env.gui.m_context[1] = 0;
    CollectGuests(table, kDue, w.env);
    OBL_CHECK(Ids(table) == (std::vector<uint32_t>{10, 11, 13}));
    OBL_CHECK(table.m_count == 3);
    OBL_CHECK(Join(w.env.log) == "sync:3 context:6,3 ");
    w.env.log.clear();

    table.m_refreshTimer = 3.5f;
    w.env.gui.m_context[1] = 3;
    CollectGuests(table, kDue, w.env);
    OBL_CHECK(Ids(table) == (std::vector<uint32_t>{10}));
    OBL_CHECK(Join(w.env.log) == "sort-guests:1,3,1 sync:1 context:6,1 ");
    w.env.log.clear();

    table.m_refreshTimer = 3.5f;
    table.m_sortParam = 0;
    w.env.gui.m_context[1] = 4;
    CollectGuests(table, kDue, w.env);
    OBL_CHECK(Ids(table) == (std::vector<uint32_t>{11, 13}));
    OBL_CHECK(Join(w.env.log) == "sort-guests:2,4,0 sync:2 context:6,2 ");
    w.env.log.clear();

    table.m_refreshTimer = 3.5f;
    w.env.gui.m_context[1] = 9; // any other value: everyone, still sorted
    CollectGuests(table, kDue, w.env);
    OBL_CHECK(Ids(table) == (std::vector<uint32_t>{10, 11, 13}));
    OBL_CHECK(Join(w.env.log) == "sort-guests:3,9,0 sync:3 context:6,3 ");
}

OBL_TEST_CASE("buildings: every panel filter")
{
    World w;
    w.AddBuilding(0, "A", 0, -1, true);
    w.AddBuilding(1, "B", 2, 0, false);
    w.AddBuilding(2, "C", 1, 5, true, true); // being removed: never listed
    w.AddBuilding(0 + 3, "D", 0, 7, true);
    Table table = DueTable();
    struct Case {
        int filter;
        std::vector<uint32_t> ids;
        const char* log;
    };
    const Case cases[] = {
            {0, {0, 1, 3}, "sync:3 context:6,3 "},
            {0x13, {1}, "sort-buildings:1,19,1 sync:1 context:6,1 "},
            {0x14, {0, 1, 3}, "sort-buildings:3,20,1 sync:3 context:6,3 "},
            {0x15, {1}, "sort-buildings:1,21,1 sync:1 context:6,1 "},
            {0x16, {1}, "sort-buildings:1,22,1 sync:1 context:6,1 "},
            {0x17, {1, 3}, "sort-buildings:2,23,1 sync:2 context:6,2 "},
            {0x18, {0, 3}, "open? open? open? sort-buildings:2,24,1 sync:2 context:6,2 "},
            {0x19, {1}, "open? open? open? sort-buildings:1,25,1 sync:1 context:6,1 "},
            {-5, {0, 1, 3}, "sort-buildings:3,-5,1 sync:3 context:6,3 "},
    };
    for (const Case& c : cases) {
        table.m_refreshTimer = 3.5f;
        w.env.log.clear();
        w.env.gui.m_context[2] = c.filter;
        CollectBuildings(table, kDue, w.env);
        OBL_CHECK(Ids(table) == c.ids);
        OBL_CHECK(table.m_count == c.ids.size());
        OBL_CHECK(Join(w.env.log) == c.log);
    }
}

OBL_TEST_CASE("power generators match the exact name and keep the manager's order; closed-power buildings follow the flag")
{
    World w;
    w.AddBuilding(0, "POWERGEN");
    w.AddBuilding(1, "POWERGENX");
    w.AddBuilding(2, "POWERGEN", 1, 0, true, true);
    w.AddBuilding(3, "powergen");
    w.AddBuilding(4, "POWERGEN");
    static_cast<Building*>(w.env.Objects()->m_buildings[1])->m_closedForPower = 1;
    static_cast<Building*>(w.env.Objects()->m_buildings[2])->m_closedForPower = 1;
    static_cast<Building*>(w.env.Objects()->m_buildings[3])->m_closedForPower = 1;
    Table table = DueTable();
    CollectPowerGenerators(table, kDue, w.env);
    OBL_CHECK(Ids(table) == (std::vector<uint32_t>{0, 4}));
    OBL_CHECK(table.m_count == 2);
    OBL_CHECK(Join(w.env.log) == "sync:2 ");
    w.env.log.clear();
    table.m_refreshTimer = 3.5f;
    CollectClosedPower(table, kDue, w.env);
    OBL_CHECK(Ids(table) == (std::vector<uint32_t>{1, 3}));
    OBL_CHECK(Join(w.env.log) == "sync:2 ");
}

OBL_TEST_CASE("commodities: the open building is the only row and the count comes from its definition")
{
    World w;
    Building* shop = w.AddBuilding(0, "SHOP", 6);
    w.env.provider = shop;
    Table table = DueTable();
    table.m_count = 99;
    CollectCommodities(table, kDue, w.env);
    OBL_CHECK(Ids(table) == (std::vector<uint32_t>{0}));
    OBL_CHECK(table.m_count == 6);
    OBL_CHECK(Join(w.env.log) == "provider sync:6 ");
    // a second pass reuses the storage
    void** storage = table.m_rows.m_begin;
    table.m_refreshTimer = 3.5f;
    CollectCommodities(table, kDue, w.env);
    OBL_CHECK(table.m_rows.m_begin == storage);
    OBL_CHECK(table.m_rows.m_end - table.m_rows.m_begin == 1);
}

OBL_TEST_CASE("staff: class filters, sacked and removed members, sort choice and the selected id")
{
    World w;
    w.AddStaff(30, 7);
    w.AddStaff(20, 11);
    w.AddStaff(25, 12);
    w.AddStaff(10, 7);
    w.AddStaff(15, 7, true);
    w.AddStaff(12, 7, false, true);
    w.AddStaff(40, 9);
    Table table = DueTable();
    struct Case {
        int filter;
        std::vector<uint32_t> ids;
        const char* log;
    };
    const Case cases[] = {
            {0, {20}, "staff sort-id:1 sync:1 "},
            {1, {}, "staff sync:0 "},
            {3, {40}, "staff sort-id:1 sync:1 "},
            {4, {10, 30}, "staff sort-id:2 sync:2 "},
            {5, {25}, "staff sort-id:1 sync:1 "},
            {6, {30, 20, 25, 10, 40}, "staff sort-title:5 sync:5 "}, // the title sort is the Env's: order stays as listed
            {7, {}, "staff sync:0 "},
            {-1, {}, "staff sync:0 "},
    };
    for (const Case& c : cases) {
        table.m_refreshTimer = 3.5f;
        table.m_selected = -1;
        w.env.log.clear();
        w.env.gui.m_context[10] = c.filter;
        CollectStaff(table, kDue, w.env);
        OBL_CHECK(Ids(table) == c.ids);
        OBL_CHECK(table.m_count == c.ids.size());
        OBL_CHECK(Join(w.env.log) == std::string(c.log) + "context:12,-1 ");
    }
}

OBL_TEST_CASE("staff: the selected row's id is published, from the first row for a shared-provider table")
{
    World w;
    w.AddStaff(30, 7);
    w.AddStaff(10, 7);
    w.AddStaff(20, 7);
    w.env.gui.m_context[10] = 4;
    Table table = DueTable();
    table.m_rowSource = 1;
    table.m_selected = 1;
    CollectStaff(table, kDue, w.env);
    OBL_CHECK(Ids(table) == (std::vector<uint32_t>{10, 20, 30})); // sorted by unsigned id
    OBL_CHECK(Join(w.env.log) == "staff sort-id:3 sync:3 context:12,20 ");
    w.env.log.clear();
    table.m_refreshTimer = 3.5f;
    table.m_rowSource = 0;
    table.m_selected = 2;
    CollectStaff(table, kDue, w.env);
    OBL_CHECK(Join(w.env.log) == "staff sort-id:3 sync:3 context:12,10 ");
}

OBL_TEST_CASE("the vector grows like STLport's across more rows than one block holds")
{
    World w;
    for (uint32_t i = 0; i < 40; ++i) w.AddGuest(i, static_cast<int>(i & 1));
    Table table = DueTable();
    CollectGuests(table, kDue, w.env);
    OBL_CHECK(table.m_count == 40);
    OBL_CHECK(table.m_rows.m_capacity - table.m_rows.m_begin == 64); // 1, 2, 4, ... doubling
    std::vector<uint32_t> expected(40);
    for (uint32_t i = 0; i < 40; ++i) expected[i] = i;
    OBL_CHECK(Ids(table) == expected);
}
