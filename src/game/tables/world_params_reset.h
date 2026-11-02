#pragma once

#include "game/tables/world_params_tables.h"

#include <cstring>

namespace obl::world_params_process {

using tables::WorldParams;

// Clear the counts, the name table and the record tables. The game always clears beers too; Keep preserves the existing beer
// records (their count is still cleared) for callers explicitly requesting it.
enum class Beers { Zero, Keep };
inline void Reset(WorldParams& p, Beers beers)
{
    for (int* count :
            {&p.m_numCommodityFamilies, &p.m_numCommodities, &p.m_numOpeningHours, &p.m_numBuildings, &p.m_numDesires, &p.m_numStaff,
                    &p.m_numGuests, &p.m_numVehicles, &p.m_numEvents, &p.m_numBeers, &p.m_numFines, &p.m_numTables, &p.m_numScenery})
        *count = 0;
    std::memset(p.m_names, 0, sizeof(p.m_names));
    std::memset(p.m_nameCounts, 0, sizeof(p.m_nameCounts));
    p.m_commodityFamilies.clear();
    p.m_commodities.clear();
    p.m_desires.clear();
    p.m_desireSlots.clear();
    p.m_vehicles.clear();
    p.m_scenery.clear();
    p.m_buildings.clear();
    p.m_tables.clear();
    p.m_staff.clear();
    p.m_guests.clear();
    p.m_openingHours.clear();
    p.m_events.clear();
    p.m_fines.clear();
    if (beers == Beers::Zero) p.m_beers.clear();
}

} // namespace obl::world_params_process
