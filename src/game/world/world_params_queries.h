#pragma once

#include "game/tables/world_params_tables.h"
#include "core/ascii_case.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// The id and definition lookups (docs/specs/world-params.md): pure lookups over the
// fixed tables of the loaded definitions. The Id lookups are a linear, case-sensitive search of the first m_num*
// records by key; a miss is reported with FatalError (which exits the process), so the entry point keeps that
// part and these helpers only say "not found" (the scenery id lookup alone returns 0 on a miss, no FatalError). The accessors do no bounds check: they are a single address computation, so an id
// outside the table yields the wrapped address (negative ids included) and the caller dereferences it.
namespace obl::world_params_queries {

inline constexpr int kNotFound = -1;

inline const char* KeyText(const char* key)
{
    return key;
}
inline const char* KeyText(const std::string& key)
{
    return key.c_str();
}

// Record id of the first record whose key equals `key`, or kNotFound. `count` is compared unsigned, so a negative
// count reads as a huge one.
template<class Def> int FindIdByKey(const Def* records, int count, const char* key)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i)
        if (ascii::EqualsNoCase(KeyText(records[i].m_key), key)) return records[i].m_id;
    return kNotFound;
}

// Address of table record `index` without the bounds check of operator[] (`this + base + index * size`).
template<class Def> const Def* RecordAt(const Def* table, int index)
{
    return reinterpret_cast<const Def*>(reinterpret_cast<uintptr_t>(table) +
            static_cast<uintptr_t>(static_cast<ptrdiff_t>(index) * static_cast<ptrdiff_t>(sizeof(Def))));
}

template<class Def> int FindIdByKey(const std::vector<Def>& records, int count, const char* key)
{
    return FindIdByKey(records.data(), count, key);
}
template<class Def> const Def* RecordAt(const std::vector<Def>& table, int index)
{
    return RecordAt(table.data(), index);
}

using tables::WorldParams;

inline int FindCommodityFamilyId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_commodityFamilies, p.m_numCommodityFamilies, key);
}
inline int FindCommodityId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_commodities, p.m_numCommodities, key);
}
inline int FindBuildingId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_buildings, p.m_numBuildings, key);
}
inline int FindEventId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_events, p.m_numEvents, key);
}
inline int FindBeerId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_beers, p.m_numBeers, key);
}
inline int FindFineId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_fines, p.m_numFines, key);
}
inline int FindStaffId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_staff, p.m_numStaff, key);
}
inline int FindGuestId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_guests, p.m_numGuests, key);
}
inline int FindVehicleId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_vehicles, p.m_numVehicles, key);
}
inline int FindTableId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_tables, p.m_numTables, key);
}
inline int FindSceneryId(const WorldParams& p, const char* key)
{
    return FindIdByKey(p.m_scenery, p.m_numScenery, key);
}

// GetCommodityName returns the record key, not the display name.
inline const char* GetCommodityName(const WorldParams& p, int id)
{
    return RecordAt(p.m_commodities, id)->m_key.c_str();
}
inline int GetCommodityFamilyOf(const WorldParams& p, int id)
{
    return RecordAt(p.m_commodities, id)->m_family;
}
inline const tables::EventDef* GetEventDefinition(const WorldParams& p, int id)
{
    return RecordAt(p.m_events, id);
}
inline const tables::BeerDef* GetBeerDefinition(const WorldParams& p, int id)
{
    return RecordAt(p.m_beers, id);
}
inline const tables::BuildingDef* GetBuildingDef(const WorldParams& p, int id)
{
    return RecordAt(p.m_buildings, id);
}
inline const char* GetBuildingKey(const WorldParams& p, int id)
{
    return RecordAt(p.m_buildings, id)->m_key.c_str();
}
inline const tables::StaffDef* GetStaffDefinition(const WorldParams& p, int id)
{
    return RecordAt(p.m_staff, id);
}
inline const tables::VehicleDef* GetVehicleDefinition(const WorldParams& p, int id)
{
    return RecordAt(p.m_vehicles, id);
}
inline const tables::TableDef* GetTableDefinition(const WorldParams& p, int id)
{
    return RecordAt(p.m_tables, id);
}
// GetCommodityDisplayName returns CommodityDef::m_name ("Beer"), where GetCommodityName returns the key.
inline const char* GetCommodityDisplayName(const WorldParams& p, int id)
{
    return RecordAt(p.m_commodities, id)->m_name.c_str();
}
inline const tables::FineDef* GetFineRecord(const WorldParams& p, int id)
{
    return RecordAt(p.m_fines, id);
}
inline const tables::GuestDef* GetGuestDefinition(const WorldParams& p, int id)
{
    return RecordAt(p.m_guests, id);
}
inline const tables::SceneryDef* GetSceneryDef(const WorldParams& p, int index)
{
    return RecordAt(p.m_scenery, index);
}

} // namespace obl::world_params_queries
