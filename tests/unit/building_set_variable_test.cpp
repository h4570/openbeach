#include "unit_test.h"

#include "game/objects/building_set_variable.h"

#include <cstring>
#include <string>
#include <vector>

using namespace obl;
using object::Building;
using tables::BeerDef;
using tables::BuildingDef;
namespace sv = building_set_variable;

namespace {

struct Env {
    std::vector<std::string> calls;
    BuildingDef* definition = nullptr;
    BeerDef beer{};
    std::vector<std::string> names; // by commodity id
    bool eventActive[3] = {false, false, false};

    const char* CommodityDisplayName(int id) { return names[id].c_str(); }
    const BeerDef* Beer(int id)
    {
        calls.push_back("beer " + std::to_string(id));
        return &beer;
    }
    void Open(Building&) { calls.push_back("open"); }
    void Close(Building&) { calls.push_back("close"); }
    bool EventSlotActive(Building&, int slot) { return eventActive[slot]; }
    void SetEventPrice(Building&, int slot, uint32_t word)
    {
        calls.push_back("event " + std::to_string(slot) + " " + std::to_string(word));
    }
    void SetBeachOpenMode(Building&, int mode) { calls.push_back("beach " + std::to_string(mode)); }
    void Fatal(const char* key) { calls.push_back(std::string("fatal ") + key); }
};

struct Fixture {
    Env env;
    Building building{};
    BuildingDef definition{};
    Fixture()
    {
        building.m_definition = &definition;
        definition.m_NumCommodities = 2;
        definition.m_Commodity[0] = 0;
        definition.m_Commodity[1] = 1;
        env.names = {"Pizza", "BEER"};
        env.beer.m_BaseCost = 0.5f;
        env.beer.m_StrCost = 2.0f;
        building.m_BeerType = 3;
        building.m_BeerStrength = 0.5f;
    }
    void Set(const char* key, uint32_t word) { sv::SetVariable(building, key, &word, env); }
    void Set(const char* key, float real)
    {
        uint32_t word;
        std::memcpy(&word, &real, sizeof(word));
        Set(key, word);
    }
};

} // namespace

OBL_TEST_CASE("building_set_variable.repair_interval_is_at_least_one")
{
    Fixture f;
    f.Set(sv::kRepairInterval, 5u);
    OBL_CHECK_EQ(f.building.m_UserRepairInterval, 5);
    OBL_CHECK_EQ(f.building.m_RepairInterval, 5);
    f.Set(sv::kRepairInterval, 0u);
    OBL_CHECK_EQ(f.building.m_UserRepairInterval, 1);
    OBL_CHECK_EQ(f.building.m_RepairInterval, 1);
}

OBL_TEST_CASE("building_set_variable.commodity_price_takes_the_slot_after_the_prefix")
{
    Fixture f;
    f.Set("COMMOD_PRICE_3", 2.5f);
    OBL_CHECK(f.building.m_Commod_Price[3] == 2.5f);
    f.Set("COMMOD_PRICE_0", 1.0f);
    OBL_CHECK(f.building.m_Commod_Price[0] == 1.0f);
    // The prefix is compared by length: anything after it is the number (atoi stops at the first non-digit).
    f.Set("COMMOD_PRICE_2x", 7.0f);
    OBL_CHECK(f.building.m_Commod_Price[2] == 7.0f);
}

OBL_TEST_CASE("building_set_variable.beer_strength_reprices_the_beer_slot")
{
    Fixture f;
    f.Set(sv::kBeerStrength, 0.8f);
    OBL_CHECK(f.building.m_BeerStrength == 0.8f);
    // cost = BASECOST + STRCOST * strength in the slot of the commodity named Beer (slot 1).
    OBL_CHECK(f.building.m_Commod_Cost[1] == static_cast<float>(2.0 * static_cast<double>(0.8f) + 0.5));
    OBL_CHECK_EQ(f.env.calls.size(), 1u);
    OBL_CHECK_EQ(f.env.calls[0], "beer 3");
}

OBL_TEST_CASE("building_set_variable.beer_price_goes_to_the_beer_slot")
{
    Fixture f;
    f.Set(sv::kBeerPrice, 9.5f);
    OBL_CHECK(f.building.m_Commod_Price[1] == 9.5f);
    OBL_CHECK(f.building.m_Commod_Price[0] == 0.0f);
}

OBL_TEST_CASE("building_set_variable.no_beer_slot_writes_the_word_before_the_table")
{
    Fixture f;
    f.env.names = {"Pizza", "Chips"};
    f.Set(sv::kBeerPrice, 4.0f);
    // Slot -1 of the prices is the word before them (the last intensity word), slot -1 of the costs the last price factor.
    OBL_CHECK(f.building.m_Commod_Intensity[4][1] == 4.0f);
    f.Set(sv::kBeerStrength, 1.0f);
    OBL_CHECK(f.building.m_Commod_PriceFactor[4] == 2.5f);
}

OBL_TEST_CASE("building_set_variable.open_word_picks_open_or_close")
{
    Fixture f;
    f.Set(sv::kOpen, 0u);
    f.Set(sv::kOpen, 1u);
    f.Set(sv::kOpen, 0x100u);
    OBL_CHECK_EQ(f.env.calls.size(), 3u);
    OBL_CHECK_EQ(f.env.calls[0], "open");
    OBL_CHECK_EQ(f.env.calls[1], "close");
    OBL_CHECK_EQ(f.env.calls[2], "close");
}

OBL_TEST_CASE("building_set_variable.show_price_is_set_only_while_the_slot_is_idle")
{
    Fixture f;
    f.env.eventActive[1] = true;
    f.Set("EVENT0_PRICE", 7u);
    f.Set("EVENT1_PRICE", 8u);
    f.Set("EVENT2_PRICE_SUFFIX", 9u); // strncmp over the twelve characters: the rest is ignored
    OBL_CHECK_EQ(f.env.calls.size(), 2u);
    OBL_CHECK_EQ(f.env.calls[0], "event 0 7");
    OBL_CHECK_EQ(f.env.calls[1], "event 2 9");
}

OBL_TEST_CASE("building_set_variable.beach_open_zero_means_mode_one")
{
    Fixture f;
    f.Set(sv::kBeachOpen, 0u);
    f.Set(sv::kBeachOpen, 1u);
    OBL_CHECK_EQ(f.env.calls[0], "beach 1");
    OBL_CHECK_EQ(f.env.calls[1], "beach 0");
}

OBL_TEST_CASE("building_set_variable.unrecognized_keyword_is_fatal_and_case_matters")
{
    Fixture f;
    f.Set("beer_strength", 0.8f);
    f.Set("NOPE", 1u);
    OBL_CHECK_EQ(f.env.calls.size(), 2u);
    OBL_CHECK_EQ(f.env.calls[0], "fatal beer_strength");
    OBL_CHECK_EQ(f.env.calls[1], "fatal NOPE");
}
