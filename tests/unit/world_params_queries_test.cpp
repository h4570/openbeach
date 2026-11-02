#include "unit_test.h"
#include "game/world/world_params_queries.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace obl;
namespace wpq = obl::world_params_queries;

namespace {

template<class Def> void Fill(Def* table, int count, const char* prefix, int idOffset = 0)
{
    for (int i = 0; i < count; ++i) {
        table[i].m_id = i + idOffset;
        table[i].m_key = std::string(prefix) + std::to_string(i);
    }
}

template<class Def> void Fill(std::vector<Def>& table, int count, const char* prefix, int idOffset = 0)
{
    table.resize(count);
    for (int i = 0; i < count; ++i) {
        table[i].m_id = i + idOffset;
        table[i].m_key = std::string(prefix) + std::to_string(i);
    }
}

std::unique_ptr<tables::WorldParams> MakeParams()
{
    auto p = std::make_unique<tables::WorldParams>();
    p->m_numCommodityFamilies = 3;
    Fill(p->m_commodityFamilies, 3, "FAM");
    p->m_numCommodities = 4;
    Fill(p->m_commodities, 4, "COM");
    p->m_numBuildings = 5;
    p->m_buildings.resize(5);
    Fill(p->m_buildings.data(), 5, "BLD");
    p->m_numEvents = 2;
    Fill(p->m_events, 2, "EVT");
    p->m_numBeers = 2;
    Fill(p->m_beers, 2, "BEER");
    p->m_numFines = 2;
    Fill(p->m_fines, 2, "FINE");
    p->m_numStaff = 3;
    Fill(p->m_staff, 3, "STF");
    p->m_numGuests = 2;
    Fill(p->m_guests, 2, "GST");
    p->m_numVehicles = 2;
    Fill(p->m_vehicles, 2, "VEH");
    p->m_numTables = 2;
    Fill(p->m_tables, 2, "TBL");
    p->m_numScenery = 3;
    return p;
}

} // namespace

OBL_TEST_CASE("world_params_queries.find_each_table_by_key")
{
    auto p = MakeParams();
    OBL_CHECK_EQ(wpq::FindCommodityFamilyId(*p, "FAM2"), 2);
    OBL_CHECK_EQ(wpq::FindCommodityId(*p, "COM3"), 3);
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, "BLD4"), 4);
    OBL_CHECK_EQ(wpq::FindEventId(*p, "EVT1"), 1);
    OBL_CHECK_EQ(wpq::FindBeerId(*p, "BEER1"), 1);
    OBL_CHECK_EQ(wpq::FindFineId(*p, "FINE0"), 0);
    OBL_CHECK_EQ(wpq::FindStaffId(*p, "STF2"), 2);
    OBL_CHECK_EQ(wpq::FindGuestId(*p, "GST1"), 1);
    OBL_CHECK_EQ(wpq::FindVehicleId(*p, "VEH1"), 1);
    OBL_CHECK_EQ(wpq::FindTableId(*p, "TBL1"), 1);
}

OBL_TEST_CASE("world_params_queries.find_returns_the_stored_id_not_the_index")
{
    auto p = MakeParams();
    p->m_buildings[2].m_id = 77;
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, "BLD2"), 77);
}

OBL_TEST_CASE("world_params_queries.find_ignores_case_and_is_exact")
{
    auto p = MakeParams();
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, "bld1"), wpq::FindBuildingId(*p, "BLD1"));
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, "BLD"), wpq::kNotFound); // prefix of a key
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, "BLD10"), wpq::kNotFound); // key plus suffix
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, ""), wpq::kNotFound);
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, "COM1"), wpq::kNotFound); // a key of another table
}

OBL_TEST_CASE("world_params_queries.find_searches_only_the_counted_records")
{
    auto p = MakeParams();
    p->m_buildings.resize(8);
    Fill(p->m_buildings.data(), 8, "BLD"); // records 5..7 exist in memory but are not counted
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, "BLD4"), 4);
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, "BLD5"), wpq::kNotFound);
    p->m_numBuildings = 0;
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, "BLD0"), wpq::kNotFound);
}

OBL_TEST_CASE("world_params_queries.find_returns_the_first_of_duplicate_keys")
{
    auto p = MakeParams();
    p->m_staff[2].m_key = "STF0";
    p->m_staff[2].m_id = 9;
    OBL_CHECK_EQ(wpq::FindStaffId(*p, "STF0"), 0);
}

OBL_TEST_CASE("world_params_queries.find_empty_key_matches_an_empty_record_key")
{
    auto p = MakeParams();
    p->m_buildings[3].m_key[0] = 0;
    OBL_CHECK_EQ(wpq::FindBuildingId(*p, ""), 3);
}

OBL_TEST_CASE("world_params_queries.accessors_match_the_field_offsets")
{
    auto p = MakeParams();
    p->m_vehicles.resize(2);
    p->m_scenery.resize(3);
    OBL_CHECK_EQ(wpq::GetBuildingDef(*p, 3), &p->m_buildings[3]);
    OBL_CHECK_EQ(wpq::GetBuildingKey(*p, 3), p->m_buildings[3].m_key.c_str());
    OBL_CHECK(wpq::GetCommodityName(*p, 2) == p->m_commodities[2].m_key.c_str());
    OBL_CHECK_EQ(wpq::GetEventDefinition(*p, 1), &p->m_events[1]);
    OBL_CHECK_EQ(wpq::GetBeerDefinition(*p, 1), &p->m_beers[1]);
    OBL_CHECK_EQ(wpq::GetStaffDefinition(*p, 2), &p->m_staff[2]);
    OBL_CHECK_EQ(wpq::GetVehicleDefinition(*p, 1), &p->m_vehicles[1]);
    OBL_CHECK_EQ(wpq::GetTableDefinition(*p, 1), &p->m_tables[1]);
    OBL_CHECK_EQ(wpq::GetSceneryDef(*p, 2), &p->m_scenery[2]);
}

OBL_TEST_CASE("world_params_queries.accessors_read_the_record_fields")
{
    auto p = MakeParams();
    p->m_commodities[2].m_family = 7;
    OBL_CHECK_EQ(wpq::GetCommodityFamilyOf(*p, 2), 7);
    OBL_CHECK_EQ(std::string(wpq::GetCommodityName(*p, 3)), std::string("COM3")); // the key, not m_name
    OBL_CHECK_EQ(std::string(wpq::GetBuildingKey(*p, 4)), std::string("BLD4"));
    OBL_CHECK_EQ(wpq::GetBuildingDef(*p, 1)->m_id, 1);
}

OBL_TEST_CASE("world_params_queries.accessors_do_not_bounds_check")
{
    auto p = MakeParams();
    // Past the counted records but inside the table, and a negative id: plain address arithmetic.
    OBL_CHECK_EQ(wpq::GetBuildingDef(*p, 5), p->m_buildings.data() + 5);
    OBL_CHECK_EQ(reinterpret_cast<uintptr_t>(wpq::GetBuildingDef(*p, 1)) - reinterpret_cast<uintptr_t>(wpq::GetBuildingDef(*p, -1)),
            static_cast<uintptr_t>(2 * sizeof(tables::BuildingDef)));
}

OBL_TEST_CASE("world_params_queries.scenery_id_and_the_four_io_leftover_accessors")
{
    auto p = MakeParams();
    Fill(p->m_scenery, 3, "SCN", 10);
    OBL_CHECK_EQ(wpq::FindSceneryId(*p, "SCN2"), 12);
    OBL_CHECK_EQ(wpq::FindSceneryId(*p, "SCN3"), wpq::kNotFound);
    p->m_commodities[2].m_name = "Beer";
    OBL_CHECK_EQ(std::strcmp(wpq::GetCommodityDisplayName(*p, 2), "Beer"), 0);
    OBL_CHECK_EQ(wpq::GetFineRecord(*p, 1), &p->m_fines[1]);
    OBL_CHECK_EQ(wpq::GetGuestDefinition(*p, 1), &p->m_guests[1]);
    OBL_CHECK_EQ(wpq::GetGuestDefinition(*p, 5), p->m_guests.data() + 5);
}
