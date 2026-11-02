#pragma once

#include "game/tables/world_params_leaves.h"
#include <vector>

// WorldParams: the definitions parsed from the converted catalog and one guest set (docs/specs/world-params.md). A native object owned by the world (`World::m_worldParams`, made with `new` by
// world::world_params::Construct); ids are indices in file order. Field names follow the file keywords (BUILD_<X>_<KEYWORD> ->
// m_<Keyword>).
namespace obl::tables {

// Index into GuestDef::m_Opinion* (and the matching Guest opinion fields).
enum GuestOpinion : int { Beauty, Crowding, Totty, Price, Variety, Cleanliness, Safety };

struct WorldParams {
    // The table capacities of the 2002 data files: the catalog and guest-set loaders refuse more records.
    static constexpr int kMaxCommodityFamilies = 24, kMaxCommodities = 41, kMaxOpeningHours = 5, kMaxBuildings = 59, kMaxDesires = 14,
                         kMaxStaff = 21, kMaxGuests = 20, kMaxVehicles = 6, kMaxNames = 0x370, kMaxEvents = 20, kMaxBeers = 5,
                         kMaxFines = 2, kMaxTables = 19, kMaxScenery = 94, kDesireSlots = 26;

    int m_numCommodityFamilies{};
    int m_numCommodities{};
    int m_numOpeningHours{};
    int m_numBuildings{};
    int m_numDesires{};
    int m_numStaff{};
    int m_numGuests{};
    int m_numVehicles{};
    int m_numEvents{};
    int m_numBeers{};
    int m_numFines{};
    int m_numTables{};
    int m_numScenery{};
    char m_names[kMaxNames][0x40]{}; // MALENAME/FEMALENAME/SURNAME text keys (female from index 0x8c, surnames from 0x118)
    int m_nameCounts[3]{};
    std::vector<OpeningHoursDef> m_openingHours;
    std::vector<TableDef> m_tables;
    std::vector<StaffDef> m_staff;
    std::vector<GuestDef> m_guests;
    std::vector<EventDef> m_events;
    std::vector<BeerDef> m_beers;
    std::vector<FineDef> m_fines;
    std::vector<CommodityFamilyDef> m_commodityFamilies;
    std::vector<CommodityDef> m_commodities;
    std::vector<DesireDef> m_desires;
    std::vector<DesireSlotDef> m_desireSlots;
    std::vector<VehicleDef> m_vehicles;
    std::vector<SceneryDef> m_scenery;
    std::vector<BuildingDef> m_buildings;
};

} // namespace obl::tables
