#include "unit_test.h"
#include "importer/src/readers/world_params_process.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
namespace wpp = obl::world_params_process;
namespace wpa = obl::world_params_add;
using tables::WorldParams;

// The world params process routine / Construct (zeroing) (docs/specs/world-params.md). The checks read the records' fields, so a wrong
// prefix order, cursor or table fails here without the game.

namespace {

std::vector<std::string> g_fatals;
std::vector<void*> g_heap;

void RecordFatal(const char* format, const char* argument)
{
    char text[256];
    std::snprintf(text, sizeof(text), format, argument ? argument : "");
    g_fatals.push_back(text);
}
void* Allocate(size_t bytes)
{
    void* memory = std::calloc(1, bytes ? bytes : 1);
    g_heap.push_back(memory);
    return memory;
}
const wpa::Env kEnv{&RecordFatal, &Allocate};

struct Fixture {
    std::unique_ptr<WorldParams> params = std::make_unique<WorldParams>();
    Fixture() { g_fatals.clear(); }
    ~Fixture()
    {
        for (void* memory : g_heap) std::free(memory);
        g_heap.clear();
    }
    WorldParams& operator*() { return *params; }
    WorldParams* operator->() { return params.get(); }

    // Parses `text` like the game does a file: a buffer of size + 1 bytes (the terminator the file reader appends).
    bool Parse(const std::string& text)
    {
        std::vector<char> buffer(text.begin(), text.end());
        buffer.push_back('.');
        return wpp::Process(*params, kEnv, buffer.data(), static_cast<uint32_t>(text.size()));
    }
};

OBL_TEST_CASE("world_params_process.leaves_grow_and_preserve_long_keys")
{
    Fixture f;
    std::string text;
    for (int i = 0; i < 25; ++i) text += "\"EVENT_E" + std::to_string(i) + "\"\n";
    for (int i = 0; i < 7; ++i) text += "\"BEER_B" + std::to_string(i) + "\"\n";
    for (int i = 0; i < 8; ++i) text += "\"HOURS_H" + std::to_string(i) + "\" \"8\" \"20\"\n";
    const std::string key(120, 'X');
    text += "\"FINE_" + key + "\"\n\"" + key + "_COST\" \"12.5\"\n";
    for (int i = 0; i < 4; ++i) text += "\"FINE_F" + std::to_string(i) + "\"\n";
    OBL_REQUIRE(f.Parse(text));
    OBL_CHECK_EQ(f->m_events.size(), size_t(25));
    OBL_CHECK_EQ(f->m_beers.size(), size_t(7));
    OBL_CHECK_EQ(f->m_openingHours.size(), size_t(8));
    OBL_CHECK_EQ(f->m_fines.size(), size_t(5));
    OBL_CHECK_EQ(world_params_queries::FindEventId(*f, "E24"), 24);
    OBL_CHECK_EQ(world_params_queries::FindBeerId(*f, "B6"), 6);
    OBL_CHECK_EQ(wpa::detail::FindOpeningHoursId(*f, "H7"), 7);
    OBL_CHECK_EQ(world_params_queries::FindFineId(*f, key.c_str()), 0);
    OBL_CHECK_EQ(f->m_fines[0].m_key, key);
    OBL_CHECK_EQ(f->m_fines[0].m_cost, 12.5f);
    OBL_CHECK(f->m_beers[0].m_Cocktail[0] == '\0');
    OBL_CHECK(g_fatals.empty());
}

// The families and commodities the building lines below refer to.
const char* const kBase = "// a comment may hold anything but quotes\n"
                          "\"COMFAM_ACCOMDTN\" \"Accommodation\"\n"
                          "\"COMFAM_SNACKS\"   \"Snacks\"\n"
                          "\"COM_BEDS\" \"Beds\" \"SNACKS\"\n"
                          "\"HOURS_DAY\" \"8\" \"20.5\"\n";

} // namespace

OBL_TEST_CASE("world_params_process.odd_quote_count_is_a_fatal_error_before_any_record")
{
    Fixture f;
    OBL_CHECK(!f.Parse("\"COMFAM_A\" \"Alpha\" \"COMFAM_B"));
    OBL_CHECK_EQ(g_fatals.size(), size_t(1));
    OBL_CHECK_EQ(g_fatals[0], std::string("The gameplay data has an unclosed quote"));
    OBL_CHECK_EQ(f->m_numCommodityFamilies, 0);
}

OBL_TEST_CASE("world_params_process.empty_input_changes_nothing")
{
    Fixture f;
    OBL_CHECK(f.Parse(""));
    OBL_CHECK(f.Parse("   // only a comment\n"));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_numCommodityFamilies, 0);
}

OBL_TEST_CASE("world_params_process.families_commodities_and_hours_have_expected_values")
{
    Fixture f;
    OBL_CHECK(f.Parse(kBase));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_numCommodityFamilies, 2);
    OBL_CHECK_EQ(f->m_commodityFamilies[0].m_key, std::string("ACCOMDTN"));
    OBL_CHECK_EQ(f->m_commodityFamilies[0].m_name, std::string("Accommodation"));
    OBL_CHECK_EQ(f->m_commodityFamilies[1].m_id, 1);
    OBL_CHECK_EQ(f->m_commodityFamilies[1].m_key, std::string("SNACKS"));
    OBL_CHECK_EQ(f->m_numCommodities, 1);
    OBL_CHECK_EQ(f->m_commodities[0].m_key, std::string("BEDS"));
    OBL_CHECK_EQ(f->m_commodities[0].m_name, std::string("Beds"));
    OBL_CHECK_EQ(f->m_commodities[0].m_family, 1); // the family id found by key
    OBL_CHECK_EQ(f->m_numOpeningHours, 1);
    OBL_CHECK_EQ(f->m_openingHours[0].m_key, std::string("DAY"));
    OBL_CHECK_EQ(f->m_openingHours[0].m_open, 8.0f);
    OBL_CHECK_EQ(f->m_openingHours[0].m_close, 20.5f);
}

OBL_TEST_CASE("world_params_process.commodity_with_an_unrecognized_family_is_fatal")
{
    Fixture f;
    OBL_CHECK(!f.Parse("\"COMFAM_A\" \"Alpha\"\n\"COM_X\" \"Ex\" \"NOPE\"\n\"COM_Y\" \"Why\" \"A\"\n"));
    OBL_CHECK_EQ(g_fatals.size(), size_t(1));
    OBL_CHECK_EQ(g_fatals[0], std::string("Unrecognized commodity family \"NOPE\" in the gameplay data"));
    OBL_CHECK_EQ(f->m_numCommodities, 0); // the line is not finished and the parse stops
}

OBL_TEST_CASE("world_params_process.building_lines_continue_the_record_whose_key_is_the_prefix")
{
    Fixture f;
    const std::string text = std::string(kBase) +
            "\"BUILD_HOTEL\"\n"
            "\"HOTEL_NAME\" \"Grand Hotel\"\n"
            "\"HOTEL_GRIDWIDTH\" \"4\"\n"
            "\"BUILD_BAR\"\n"
            "\"BAR_NAME\" \"Beach Bar\"\n"
            "\"HOTEL_GRIDHEIGHT\" \"3\"\n"
            "\"HOTEL_OPENINGHOURS\" \"DAY\"\n"
            "\"HOTEL_COMMODITY\" \"BEDS\" \"0.5\" \"0.25\" \"1\" \"125\" \"1\" \"85\"\n"
            "\"HOTEL_ATTRACTOR\" \"SNACKS\"\n";
    OBL_CHECK(f.Parse(text));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_numBuildings, 2);
    OBL_CHECK_EQ(f->m_buildings[0].m_id, 0);
    OBL_CHECK_EQ(std::string(f->m_buildings[0].m_key), std::string("HOTEL"));
    OBL_CHECK_EQ(std::string(f->m_buildings[0].m_Name), std::string("Grand Hotel"));
    OBL_CHECK_EQ(f->m_buildings[1].m_id, 1);
    OBL_CHECK_EQ(std::string(f->m_buildings[1].m_Name), std::string("Beach Bar"));
    OBL_CHECK(f->m_buildings[0].m_footprint == nullptr); // the footprint pointer starts null
    OBL_CHECK_EQ(f->m_buildings[0].m_GridWidth, 4);
    OBL_CHECK_EQ(f->m_buildings[0].m_GridHeight, 3);
    OBL_CHECK_EQ(f->m_buildings[0].m_OpeningHours, 0);
    OBL_CHECK_EQ(f->m_buildings[0].m_Attractor, 1);
    OBL_CHECK_EQ(f->m_buildings[0].m_Commodity[0], 0);
    OBL_CHECK_EQ(f->m_buildings[0].m_CommodityCost[0], 125.0f);
    OBL_CHECK_EQ(f->m_buildings[1].m_GridWidth, 0);
}

OBL_TEST_CASE("world_params_process.the_first_matching_table_wins_when_keys_clash")
{
    // BUILD_TOURIST and GUEST_TOURIST share a key: the building lookup runs before the guest lookup, so the line is the building's.
    Fixture f;
    OBL_CHECK(f.Parse("\"GUEST_TOURIST\"\n\"BUILD_TOURIST\"\n\"TOURIST_NAME\" \"Tourist\"\n"));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_numGuests, 1);
    OBL_CHECK_EQ(f->m_numBuildings, 1);
    // the building lookup comes before the guest lookup
    OBL_CHECK_EQ(f->m_buildings[0].m_Name[0], 'T');
    OBL_CHECK_EQ(f->m_guests[0].m_Name[0], '\0');
}

OBL_TEST_CASE("world_params_process.desire_families_are_listed_in_file_order")
{
    Fixture f;
    const std::string text = std::string(kBase) +
            "\"DESIRE_HUNGER\" \"Hunger\"\n"
            "\"HUNGER_COMFAM\" \"SNACKS\"\n"
            "\"HUNGER_COMFAM\" \"ACCOMDTN\"\n";
    OBL_CHECK(f.Parse(text));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_numDesires, 1);
    OBL_CHECK_EQ(f->m_desires[0].m_key, std::string("HUNGER"));
    OBL_CHECK_EQ(f->m_desires[0].m_name, std::string("Hunger"));
    OBL_CHECK_EQ(f->m_desires[0].m_numFamilies, 2);
    OBL_CHECK_EQ(f->m_desires[0].m_families[0], 1);
    OBL_CHECK_EQ(f->m_desires[0].m_families[1], 0);
}

OBL_TEST_CASE("world_params_process.desire_field_other_than_comfam_is_fatal")
{
    Fixture f;
    OBL_CHECK(!f.Parse(std::string(kBase) + "\"DESIRE_HUNGER\" \"Hunger\"\n\"HUNGER_NAME\" \"X\"\n"));
    OBL_CHECK_EQ(g_fatals.size(), size_t(1));
    OBL_CHECK_EQ(g_fatals[0], std::string("A desire pull needs a commodity family field, got \"NAME\""));
}

OBL_TEST_CASE("world_params_process.staff_takes_the_key_and_twelve_values")
{
    Fixture f;
    OBL_CHECK(f.Parse(
            "\"STAFF_BARMAN\" \"Barman\" \"M\" \"12\" \"bar.spr\" \"BARMAN\" \"MENU_B\" \"40\" \"1.5\" \"0.25\" \"0.5\" \"9\" \"18\"\n"
            "\"STAFF_CLEANER\" \"Cleaner\" \"F\" \"10\" \"c.spr\" \"CLEANER\" \"MENU_C\" \"30\" \"1\" \"0\" \"1\" \"6\" \"14\"\n"));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_numStaff, 2);
    OBL_CHECK_EQ(f->m_staff[0].m_key, std::string("BARMAN"));
    OBL_CHECK_EQ(f->m_staff[0].m_Name, std::string("Barman"));
    OBL_CHECK_EQ(f->m_staff[0].m_Wage, 12);
    OBL_CHECK_EQ(f->m_staff[0].m_EndHour, 18);
    OBL_CHECK_EQ(f->m_staff[1].m_key, std::string("CLEANER"));
    OBL_CHECK_EQ(f->m_staff[1].m_female, 1);
    OBL_CHECK_EQ(f->m_staff[1].m_StartHour, 6);
}

OBL_TEST_CASE("world_params_process.guest_keywords_route_to_the_scalar_or_the_range_writer")
{
    Fixture f;
    OBL_CHECK(f.Parse("\"GUEST_TOURIST\"\n"
                      "\"TOURIST_NAME\" \"Tourist\"\n"
                      "\"TOURIST_GENDER\" \"F\"\n"
                      "\"TOURIST_MONEY\" \"250\"\n"
                      "\"TOURIST_NAMESET\" \"whatever\"\n"
                      "\"TOURIST_MAX_ALCOHOL\" \"1\"\n"
                      "\"TOURIST_ALCOHOLPREF\" \"0.25\" \"0.75\"\n"
                      "\"TOURIST_CLEANLINESS_START\" \"3\" \"4\"\n"));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_numGuests, 1);
    const tables::GuestDef& tourist = f->m_guests[0];
    OBL_CHECK_EQ(tourist.m_key, std::string("TOURIST"));
    OBL_CHECK_EQ(tourist.m_Name, std::string("Tourist"));
    OBL_CHECK_EQ(tourist.m_Gender, 1);
    OBL_CHECK_EQ(tourist.m_Money, 250.0f);
    OBL_CHECK_EQ(tourist.m_Pref[0][0], 0.25f);
    OBL_CHECK_EQ(tourist.m_Pref[0][1], 0.75f);
    OBL_CHECK_EQ(f->m_guests[0].m_OpinionStart[5][0], 3.0f);
    OBL_CHECK_EQ(f->m_guests[0].m_OpinionStart[5][1], 4.0f);
}

OBL_TEST_CASE("world_params_process.unrecognized_guest_keyword_is_fatal")
{
    Fixture f;
    OBL_CHECK(!f.Parse("\"GUEST_TOURIST\"\n\"TOURIST_COLOR\" \"1\"\n"));
    OBL_CHECK_EQ(g_fatals.size(), size_t(1));
    OBL_CHECK_EQ(g_fatals[0], std::string("Unrecognized guest section \"COLOR\" in the gameplay data"));
}

OBL_TEST_CASE("world_params_process.vehicles_tables_events_beers_fines_and_scenery_create_then_continue")
{
    Fixture f;
    OBL_CHECK(f.Parse("\"VEHICLE_BOAT\"\n\"BOAT_CAPACITY\" \"5\"\n"
                      "\"TABLE_ROUND\"\n\"ROUND_GRIDWIDTH\" \"2\"\n"
                      "\"EVENT_SINGING\"\n\"SINGING_COST\" \"30\"\n"
                      "\"BEER_LAGER\"\n\"LAGER_PRICE\" \"2.5\"\n"
                      "\"FINE_LITTER\"\n\"LITTER_COST\" \"12.5\"\n"
                      "\"SCENERY_BIN\"\n\"BIN_COST2BUILD\" \"7\"\n"));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_numVehicles, 1);
    OBL_CHECK_EQ(f->m_vehicles[0].m_key, std::string("BOAT"));
    OBL_CHECK_EQ(f->m_vehicles[0].m_Capacity, 5);
    OBL_CHECK_EQ(f->m_numTables, 1);
    OBL_CHECK_EQ(f->m_tables[0].m_key, std::string("ROUND"));
    OBL_CHECK_EQ(f->m_numEvents, 1);
    OBL_CHECK_EQ(f->m_events[0].m_key, std::string("SINGING"));
    OBL_CHECK_EQ(f->m_events[0].m_cost, 30.0f);
    OBL_CHECK_EQ(f->m_numBeers, 1);
    OBL_CHECK_EQ(f->m_beers[0].m_key, std::string("LAGER"));
    OBL_CHECK_EQ(f->m_beers[0].m_Price, 2.5f);
    OBL_CHECK_EQ(f->m_numFines, 1);
    OBL_CHECK_EQ(f->m_fines[0].m_key, std::string("LITTER"));
    OBL_CHECK_EQ(f->m_fines[0].m_cost, 12.5f);
    OBL_CHECK_EQ(f->m_numScenery, 1);
    OBL_CHECK_EQ(f->m_scenery[0].m_key, std::string("BIN"));
    OBL_CHECK_EQ(f->m_scenery[0].m_Cost2Build, 7);
}

OBL_TEST_CASE("world_params_process.fine_field_other_than_cost_is_fatal")
{
    Fixture f;
    OBL_CHECK(!f.Parse("\"FINE_LITTER\"\n\"LITTER_NAME\" \"x\"\n"));
    OBL_CHECK_EQ(g_fatals.size(), size_t(1));
    OBL_CHECK_EQ(g_fatals[0], std::string("Unrecognized fine field \"NAME\" in the gameplay data"));
}

OBL_TEST_CASE("world_params_process.name_pools_take_the_text_key_after_the_keyword")
{
    Fixture f;
    OBL_CHECK(f.Parse("\"MALENAME\" \"NAME_JOHN\"\n\"FEMALENAME\" \"NAME_JANE\"\n\"SURNAME\" \"NAME_DOE\"\n\"MALENAME\" \"NAME_BOB\"\n"));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_nameCounts[0], 2);
    OBL_CHECK_EQ(f->m_nameCounts[1], 1);
    OBL_CHECK_EQ(f->m_nameCounts[2], 1);
    OBL_CHECK_EQ(std::string(f->m_names[0]), std::string("NAME_JOHN"));
    OBL_CHECK_EQ(std::string(f->m_names[1]), std::string("NAME_BOB"));
    OBL_CHECK_EQ(std::string(f->m_names[0x8c]), std::string("NAME_JANE"));
    OBL_CHECK_EQ(std::string(f->m_names[0x118]), std::string("NAME_DOE"));
}

OBL_TEST_CASE("world_params_process.unrecognized_prefix_is_fatal_and_stops_the_parse")
{
    Fixture f;
    OBL_CHECK(!f.Parse("\"COMFAM_A\" \"Alpha\"\n\"WHATEVER_X\" \"1\"\n\"COMFAM_B\" \"Beta\"\n"));
    OBL_CHECK_EQ(g_fatals.size(), size_t(1));
    OBL_CHECK_EQ(g_fatals[0], std::string("Unrecognized section \"WHATEVER\" in the gameplay data"));
    OBL_CHECK_EQ(f->m_numCommodityFamilies, 1);
}

OBL_TEST_CASE("world_params_process.jetty_and_security_keep_their_underscore_in_the_prefix")
{
    // "JETTY_x" / "SECURITY_x" are one prefix (a building key like SECURITY_POST): the prefix ends at the second '_'.
    Fixture f;
    OBL_CHECK(f.Parse("\"BUILD_JETTY_SMALL\"\n"
                      "\"JETTY_SMALL_NAME\" \"Small jetty\"\n"
                      "\"BUILD_SECURITY_POST\"\n"
                      "\"SECURITY_POST_NAME\" \"Post\"\n"));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_numBuildings, 2);
    OBL_CHECK_EQ(std::string(f->m_buildings[0].m_key), std::string("JETTY_SMALL"));
    OBL_CHECK_EQ(std::string(f->m_buildings[0].m_Name), std::string("Small jetty"));
    OBL_CHECK_EQ(std::string(f->m_buildings[1].m_key), std::string("SECURITY_POST"));
    OBL_CHECK_EQ(std::string(f->m_buildings[1].m_Name), std::string("Post"));
}

OBL_TEST_CASE("world_params_process.the_buffer_is_zeroed_outside_the_quotes_and_the_closing_quotes")
{
    Fixture f;
    std::string text = "xx \"COMFAM_A\" \"Alpha\" yy";
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('.');
    OBL_CHECK(wpp::Process(*f, kEnv, buffer.data(), static_cast<uint32_t>(text.size())));
    OBL_CHECK_EQ(buffer[0], '\0');
    OBL_CHECK_EQ(buffer[3], '"'); // the opening quote stays
    OBL_CHECK_EQ(buffer[12], '\0'); // the closing quote is NUL
    OBL_CHECK_EQ(buffer[11], 'A'); // the text between the quotes stays
    OBL_CHECK_EQ(buffer[text.size()], '.');
}

OBL_TEST_CASE("world_params_process.reset_clears_tables_and_can_keep_beers")
{
    Fixture f;
    f->m_beers.resize(1);
    f->m_beers[0].m_key = "BEER";
    f->m_beers[0].m_Price = 3.0f;
    f->m_events.resize(1);
    f->m_fines.resize(1);
    f->m_openingHours.resize(1);
    f->m_numEvents = 1;
    f->m_commodityFamilies.resize(1);
    f->m_commodities.resize(1);
    f->m_desires.resize(1);
    f->m_desires[0].m_families = {1, 0};
    wpp::Reset(*f, wpp::Beers::Keep);
    OBL_CHECK(f->m_commodityFamilies.empty());
    OBL_CHECK(f->m_commodities.empty());
    OBL_CHECK(f->m_desires.empty());
    OBL_CHECK(f->m_events.empty());
    OBL_CHECK(f->m_fines.empty());
    OBL_CHECK(f->m_openingHours.empty());
    OBL_CHECK_EQ(f->m_numEvents, 0);
    OBL_CHECK_EQ(f->m_beers[0].m_key, std::string("BEER"));
    OBL_CHECK_EQ(f->m_beers[0].m_Price, 3.0f);
    wpp::Reset(*f, wpp::Beers::Zero);
    OBL_CHECK(f->m_beers.empty());
}

OBL_TEST_CASE("world_params_process.commodity_and_desire_tables_grow_without_truncation")
{
    Fixture f;
    const std::string key(100, 'A');
    const std::string name(180, 'N');
    std::string text = "\"COMFAM_" + key + "\" \"" + name + "\"\n";
    for (int i = 0; i < 30; ++i) text += "\"COMFAM_F" + std::to_string(i) + "\" \"Family\"\n";
    for (int i = 0; i < 50; ++i) {
        const std::string suffix = std::to_string(i);
        text += "\"COM_C" + suffix + "\" \"" + name + "\" \"" + key + "\"\n";
        text += "\"DESIRE_D" + suffix + "\" \"" + name + "\"\n";
        for (int n = 0; n < 30; ++n) text += "\"D" + suffix + "_COMFAM\" \"" + key + "\"\n";
    }
    OBL_CHECK(f.Parse(text));
    OBL_CHECK(g_fatals.empty());
    OBL_CHECK_EQ(f->m_commodityFamilies.size(), size_t(31));
    OBL_CHECK_EQ(f->m_commodityFamilies[30].m_id, 30);
    OBL_CHECK_EQ(f->m_commodityFamilies[0].m_key, key);
    OBL_CHECK_EQ(f->m_commodityFamilies[0].m_name, name);
    OBL_CHECK_EQ(f->m_commodities.size(), size_t(50));
    OBL_CHECK_EQ(f->m_desires.size(), size_t(50));
    for (int i = 0; i < 50; ++i) {
        OBL_CHECK_EQ(f->m_commodities[i].m_id, i);
        OBL_CHECK_EQ(f->m_commodities[i].m_name, name);
        OBL_CHECK_EQ(f->m_commodities[i].m_family, 0);
        OBL_CHECK_EQ(f->m_desires[i].m_id, i);
        OBL_CHECK_EQ(f->m_desires[i].m_name, name);
        OBL_CHECK_EQ(f->m_desires[i].m_numFamilies, 30);
        OBL_CHECK(f->m_desires[i].m_families == std::vector<int>(30, 0));
    }
}
