// The id and definition lookups of the world parameters: src/game/world/world_params_queries.h answers every call. The lookups and
// accessors are the world_params:: facades (world_params.h); the miss path (FatalError exits the process) is covered by unit
// tests only.

#include "game/world/world_params.h"
#include "game/game/diagnostics.h"

#include "game/game/game_accessors.h"
#include "core/call_counter.h"
#include "game/world/world_params_queries.h"

#include <algorithm>

namespace obl::world {
namespace {

using tables::WorldParams;
namespace queries = world_params_queries;

// One counter per Id lookup (`call-report world.params-get-<subject>-id`).
test::CallCounter g_commodityFamilyIdCalls("world.params-get-commodity-family-id");
test::CallCounter g_commodityIdCalls("world.params-get-commodity-id");
test::CallCounter g_buildingIdCalls("world.params-get-building-id");
test::CallCounter g_eventIdCalls("world.params-get-event-id");
test::CallCounter g_beerIdCalls("world.params-get-beer-id");
test::CallCounter g_fineIdCalls("world.params-get-fine-id");
test::CallCounter g_staffIdCalls("world.params-get-staff-id");
test::CallCounter g_guestIdCalls("world.params-get-guest-id");
test::CallCounter g_vehicleIdCalls("world.params-get-vehicle-id");
test::CallCounter g_tableIdCalls("world.params-get-table-id");
test::CallCounter g_sceneryIdCalls("world.params-get-scenery-id");

// Id lookups: a record's key -> its id; an unknown key reports `missing`.
struct IdSpec {
    test::CallCounter* counter;
    int (*find)(const WorldParams&, const char*);
    const char* missing;
    int (*count)(const WorldParams&);
    const char* (*keyAt)(const WorldParams&, int);
};
#define OBL_WP_TABLE(field, num)                                                                                                           \
    [](const WorldParams& p) { return p.num; }, [](const WorldParams& p, int i) { return queries::KeyText(p.field[i].m_key); }
constexpr IdSpec kCommodityFamilyIdSpec{&g_commodityFamilyIdCalls, &queries::FindCommodityFamilyId,
        "Unrecognized commodity family \"%s\" in the gameplay data", OBL_WP_TABLE(m_commodityFamilies, m_numCommodityFamilies)};
constexpr IdSpec kCommodityIdSpec{&g_commodityIdCalls, &queries::FindCommodityId, "Unrecognized commodity \"%s\" in the gameplay data",
        OBL_WP_TABLE(m_commodities, m_numCommodities)};
constexpr IdSpec kBuildingIdSpec{&g_buildingIdCalls, &queries::FindBuildingId, "Unrecognized building type \"%s\" in the gameplay data",
        OBL_WP_TABLE(m_buildings, m_numBuildings)};
constexpr IdSpec kEventIdSpec{
        &g_eventIdCalls, &queries::FindEventId, "Unrecognized event \"%s\" in the gameplay data", OBL_WP_TABLE(m_events, m_numEvents)};
constexpr IdSpec kBeerIdSpec{
        &g_beerIdCalls, &queries::FindBeerId, "Unrecognized beer \"%s\" in the gameplay data", OBL_WP_TABLE(m_beers, m_numBeers)};
constexpr IdSpec kFineIdSpec{
        &g_fineIdCalls, &queries::FindFineId, "Unrecognized fine \"%s\" in the gameplay data", OBL_WP_TABLE(m_fines, m_numFines)};
constexpr IdSpec kStaffIdSpec{
        &g_staffIdCalls, &queries::FindStaffId, "Unrecognized staff type \"%s\" in the gameplay data", OBL_WP_TABLE(m_staff, m_numStaff)};
constexpr IdSpec kGuestIdSpec{
        &g_guestIdCalls, &queries::FindGuestId, "Unrecognized guest type \"%s\" in the gameplay data", OBL_WP_TABLE(m_guests, m_numGuests)};
constexpr IdSpec kVehicleIdSpec{&g_vehicleIdCalls, &queries::FindVehicleId, "Unrecognized vehicle type \"%s\" in the gameplay data",
        OBL_WP_TABLE(m_vehicles, m_numVehicles)};
constexpr IdSpec kTableIdSpec{
        &g_tableIdCalls, &queries::FindTableId, "Unrecognized table type \"%s\" in the gameplay data", OBL_WP_TABLE(m_tables, m_numTables)};
// The one lookup that does not abort on a miss: it returns 0.
constexpr IdSpec kSceneryIdSpec{&g_sceneryIdCalls, &queries::FindSceneryId, nullptr, OBL_WP_TABLE(m_scenery, m_numScenery)};
#undef OBL_WP_TABLE

constexpr const IdSpec* kIdSpecs[] = {&kCommodityFamilyIdSpec, &kCommodityIdSpec, &kBuildingIdSpec, &kEventIdSpec, &kBeerIdSpec,
        &kFineIdSpec, &kStaffIdSpec, &kGuestIdSpec, &kVehicleIdSpec, &kTableIdSpec, &kSceneryIdSpec};

int LookUpId(const IdSpec& spec, WorldParams* params, const char* key)
{
    spec.counter->Count();
    const int id = spec.find(*params, key);
    if (id != queries::kNotFound) return id;
    if (!spec.missing) return 0;
    // FatalError exits the process; the return of 0 or the table count after it is never reached.
    game::FatalError(spec.missing, key);
    return 0;
}

// One counter per accessor (`call-report world.params-get-<subject>`).
test::CallCounter g_commodityNameCalls("world.params-get-commodity-name");
test::CallCounter g_commodityFamilyOfCalls("world.params-get-commodity-family-of");
test::CallCounter g_commodityDisplayNameCalls("world.params-get-commodity-display-name");
test::CallCounter g_fineRecordCalls("world.params-get-fine-record");
test::CallCounter g_guestDefinitionCalls("world.params-get-guest-definition");
test::CallCounter g_eventDefinitionCalls("world.params-get-event-definition");
test::CallCounter g_beerDefinitionCalls("world.params-get-beer-definition");
test::CallCounter g_buildingDefCalls("world.params-get-building-def");
test::CallCounter g_buildingKeyCalls("world.params-get-building-key");
test::CallCounter g_staffDefinitionCalls("world.params-get-staff-definition");
test::CallCounter g_vehicleDefinitionCalls("world.params-get-vehicle-definition");
test::CallCounter g_tableDefinitionCalls("world.params-get-table-definition");
test::CallCounter g_sceneryDefCalls("world.params-get-scenery-def");

// The records are the loaded tables, which are handed out as mutable pointers; the queries take const.
template<class T> T* Mutable(const T* record)
{
    return const_cast<T*>(record);
}

// `call-drive` walks each accessor over its table plus a few ids on each side of it: the address arithmetic needs no valid
// record. The accessors that read the record (the std::string keys, the family field) walk only the table's own ids, since
// a record past the end of its vector is not there to read.
struct DefDrive {
    int (*count)(const WorldParams&);
    void (*call)(WorldParams*, int);
    bool readsRecord = false;
};
constexpr DefDrive kDefDrives[] = {
        {[](const WorldParams& p) { return p.m_numCommodities; }, [](WorldParams* p, int id) { world_params::GetCommodityName(p, id); },
                true},
        {[](const WorldParams& p) { return p.m_numCommodities; }, [](WorldParams* p, int id) { world_params::GetCommodityFamilyOf(p, id); },
                true},
        {[](const WorldParams& p) { return p.m_numCommodities; },
                [](WorldParams* p, int id) { world_params::GetCommodityDisplayName(p, id); }},
        {[](const WorldParams& p) { return p.m_numFines; }, [](WorldParams* p, int id) { world_params::GetFineRecord(p, id); }},
        {[](const WorldParams& p) { return p.m_numGuests; }, [](WorldParams* p, int id) { world_params::GetGuestDefinition(p, id); }},
        {[](const WorldParams& p) { return p.m_numEvents; }, [](WorldParams* p, int id) { world_params::GetEventDefinition(p, id); }},
        {[](const WorldParams& p) { return p.m_numBeers; }, [](WorldParams* p, int id) { world_params::GetBeerDefinition(p, id); }},
        {[](const WorldParams& p) { return p.m_numBuildings; }, [](WorldParams* p, int id) { world_params::GetBuildingDef(p, id); }},
        {[](const WorldParams& p) { return p.m_numBuildings; }, [](WorldParams* p, int id) { world_params::GetBuildingKey(p, id); }, true},
        {[](const WorldParams& p) { return p.m_numStaff; }, [](WorldParams* p, int id) { world_params::GetStaffDefinition(p, id); }},
        {[](const WorldParams& p) { return p.m_numVehicles; }, [](WorldParams* p, int id) { world_params::GetVehicleDefinition(p, id); }},
        {[](const WorldParams& p) { return p.m_numTables; }, [](WorldParams* p, int id) { world_params::GetTableDefinition(p, id); }},
        {[](const WorldParams& p) { return p.m_numScenery; }, [](WorldParams* p, int id) { world_params::GetSceneryDef(p, id); }},
};

// `call-drive <name>`: every key of every loaded table, and every accessor over the table plus a few ids on each side of it.
void Drive(int count)
{
    WorldParams* params = game::GetWorldParams();
    if (!params) return;
    for (int round = 0; round < std::max(count, 1); ++round) {
        for (const IdSpec* spec : kIdSpecs)
            for (int i = 0, n = spec->count(*params); i < n; ++i) LookUpId(*spec, params, spec->keyAt(*params, i));
        for (const DefDrive& drive : kDefDrives)
            for (int id = drive.readsRecord ? 0 : -1, n = drive.count(*params); id < (drive.readsRecord ? n : n + 2); ++id)
                drive.call(params, id);
    }
}

} // namespace

namespace world_params {

int GetCommodityFamilyId(WorldParams* params, const char* key)
{
    return LookUpId(kCommodityFamilyIdSpec, params, key);
}
int GetCommodityId(WorldParams* params, const char* key)
{
    return LookUpId(kCommodityIdSpec, params, key);
}
int GetBuildingId(WorldParams* params, const char* key)
{
    return LookUpId(kBuildingIdSpec, params, key);
}
int GetEventId(WorldParams* params, const char* key)
{
    return LookUpId(kEventIdSpec, params, key);
}
int GetBeerId(WorldParams* params, const char* key)
{
    return LookUpId(kBeerIdSpec, params, key);
}
int GetFineId(WorldParams* params, const char* key)
{
    return LookUpId(kFineIdSpec, params, key);
}
int GetStaffId(WorldParams* params, const char* key)
{
    return LookUpId(kStaffIdSpec, params, key);
}
int GetGuestId(WorldParams* params, const char* key)
{
    return LookUpId(kGuestIdSpec, params, key);
}
int GetVehicleId(WorldParams* params, const char* key)
{
    return LookUpId(kVehicleIdSpec, params, key);
}
int GetTableId(WorldParams* params, const char* key)
{
    return LookUpId(kTableIdSpec, params, key);
}
int GetSceneryId(WorldParams* params, const char* key)
{
    return LookUpId(kSceneryIdSpec, params, key);
}

const char* GetCommodityName(WorldParams* params, int id)
{
    g_commodityNameCalls.Count();
    return queries::GetCommodityName(*params, id);
}
int GetCommodityFamilyOf(WorldParams* params, int id)
{
    g_commodityFamilyOfCalls.Count();
    return queries::GetCommodityFamilyOf(*params, id);
}
const char* GetCommodityDisplayName(WorldParams* params, int id)
{
    g_commodityDisplayNameCalls.Count();
    return queries::GetCommodityDisplayName(*params, id);
}
tables::FineDef* GetFineRecord(WorldParams* params, int id)
{
    g_fineRecordCalls.Count();
    return Mutable(queries::GetFineRecord(*params, id));
}
tables::GuestDef* GetGuestDefinition(WorldParams* params, int id)
{
    g_guestDefinitionCalls.Count();
    return Mutable(queries::GetGuestDefinition(*params, id));
}
tables::EventDef* GetEventDefinition(WorldParams* params, int id)
{
    g_eventDefinitionCalls.Count();
    return Mutable(queries::GetEventDefinition(*params, id));
}
tables::BeerDef* GetBeerDefinition(WorldParams* params, int id)
{
    g_beerDefinitionCalls.Count();
    return Mutable(queries::GetBeerDefinition(*params, id));
}
tables::BuildingDef* GetBuildingDef(WorldParams* params, int id)
{
    g_buildingDefCalls.Count();
    return Mutable(queries::GetBuildingDef(*params, id));
}
const char* GetBuildingKey(WorldParams* params, int id)
{
    g_buildingKeyCalls.Count();
    return queries::GetBuildingKey(*params, id);
}
tables::StaffDef* GetStaffDefinition(WorldParams* params, int id)
{
    g_staffDefinitionCalls.Count();
    return Mutable(queries::GetStaffDefinition(*params, id));
}
tables::VehicleDef* GetVehicleDefinition(WorldParams* params, int id)
{
    g_vehicleDefinitionCalls.Count();
    return Mutable(queries::GetVehicleDefinition(*params, id));
}
tables::TableDef* GetTableDefinition(WorldParams* params, int id)
{
    g_tableDefinitionCalls.Count();
    return Mutable(queries::GetTableDefinition(*params, id));
}
tables::SceneryDef* GetSceneryDef(WorldParams* params, int index)
{
    g_sceneryDefCalls.Count();
    return Mutable(queries::GetSceneryDef(*params, index));
}

} // namespace world_params

bool RegisterWorldParamsDriver()
{
    g_buildingIdCalls.SetDriver(&Drive); // The building-id driver exercises all id and definition queries.
    return true;
}

} // namespace obl::world
