#pragma once

#include "core/ascii_case.h"
#include "game/objects/building_construction.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

// SetVariable of the building (SetVariable, `(key, value)`): docs/architecture/objects.md. The GUI scripts of the
// building's dialogs hand over a data keyword and a pointer to a 4-byte word (an int or a float). Keywords are compared by `strcmp` (case matters), the
// two prefixed ones (`COMMOD_PRICE_<n>`, `EVENT<n>_PRICE...`) by `strncmp`; an unknown keyword ends the game (FatalError). Pure logic over object::Building; what
// reaches the game goes through an `Env`:
// const char* CommodityDisplayName(int id) the commodity's display name from the world parameters
//   const BeerDef* Beer(int id)                the beer definition from the world parameters
//   void Open(Building&) / Close(Building&)   opens / closes the building
//   bool EventSlotActive(Building&, int slot)       whether the show slot of m_events is active
// void SetEventPrice(Building&, int slot, uint32_t word): the price of the show slot
//   void SetBeachOpenMode(Building&, int mode)      sets the beach mode (0: beach open, 1: closed)
//   void Fatal(const char* key)                FatalError("A script set unrecognized building variable \"%s\"", key)
namespace obl::building_set_variable {

using object::Building;

constexpr const char* kRepairInterval = "BUILDING_REPAIRINTERVAL";
constexpr const char* kCommodityPricePrefix = "COMMOD_PRICE_"; // 13 characters, then the slot number (atoi)
constexpr size_t kCommodityPricePrefixLength = 13;
constexpr const char* kBeerStrength = "BEER_STRENGTH";
constexpr const char* kBeerPrice = "BUILDING_BEERPRICE";
constexpr const char* kOpen = "BI_BUILDING_OPEN";
constexpr const char* kEventPricePrefix[3] = {"EVENT0_PRICE", "EVENT1_PRICE", "EVENT2_PRICE"}; // strncmp over all 12 characters
constexpr size_t kEventPricePrefixLength = 12;
constexpr const char* kBeachOpen = "STAFF_BEACH_OPEN";

inline bool Is(const char* key, const char* name)
{
    return std::strcmp(key, name) == 0;
}
inline uint32_t Word(const void* value)
{
    uint32_t word;
    std::memcpy(&word, value, sizeof(word));
    return word;
}
// The word is copied as raw bytes, so a signalling NaN keeps its bits (a float load and store would quiet it).
inline void StoreWord(float& destination, const void* value)
{
    std::memcpy(&destination, value, sizeof(float));
}

// The slot of the first commodity of the definition whose display name is "Beer", or -1. Callers use -1 unchecked: it
// then writes one word before the table.
template<class Env> int BeerSlot(const Building& building, Env& env)
{
    const tables::BuildingDef& def = *building.m_definition;
    for (uint32_t slot = 0; slot < static_cast<uint32_t>(def.m_NumCommodities); ++slot)
        if (ascii::EqualsNoCase("Beer", env.CommodityDisplayName(def.m_Commodity[slot]))) return static_cast<int>(slot);
    return -1;
}

// The slots are indexed from m_Commod_Price / m_Commod_Cost without a range check: the unknown slot -1 of
// the beer lands on the word before the table (the last intensity word for a price, the last price factor for a cost).
inline float& PriceWord(Building& building, int slot)
{
    return (&building.m_Commod_Price[0])[slot];
}
inline float& CostWord(Building& building, int slot)
{
    return (&building.m_Commod_Cost[0])[slot];
}

template<class Env> void SetVariable(Building& building, const char* key, const void* value, Env& env)
{
    if (Is(key, kRepairInterval)) {
        building.m_UserRepairInterval = static_cast<int32_t>(Word(value));
        if (building.m_UserRepairInterval == 0) building.m_UserRepairInterval = 1;
        building.m_RepairInterval = building.m_UserRepairInterval;
        return;
    }
    if (std::strncmp(key, kCommodityPricePrefix, kCommodityPricePrefixLength) == 0) {
        const int slot = std::atoi(key + kCommodityPricePrefixLength);
        StoreWord(PriceWord(building, slot), value);
        return;
    }
    if (Is(key, kBeerStrength)) {
        StoreWord(building.m_BeerStrength, value);
        const int slot = BeerSlot(building, env);
        const tables::BeerDef* beer = env.Beer(building.m_BeerType);
        CostWord(building, slot) = beer->m_BaseCost;
        CostWord(building, slot) =
                static_cast<float>(static_cast<double>(beer->m_StrCost) * static_cast<double>(building.m_BeerStrength) + beer->m_BaseCost);
        return;
    }
    if (Is(key, kBeerPrice)) {
        StoreWord(PriceWord(building, BeerSlot(building, env)), value);
        return;
    }
    if (Is(key, kOpen)) {
        if (Word(value) == 0)
            env.Open(building);
        else
            env.Close(building);
        return;
    }
    for (int slot = 0; slot < 3; ++slot) {
        if (std::strncmp(key, kEventPricePrefix[slot], kEventPricePrefixLength) != 0) continue;
        if (!env.EventSlotActive(building, slot)) env.SetEventPrice(building, slot, Word(value));
        return;
    }
    if (Is(key, kBeachOpen)) {
        env.SetBeachOpenMode(building, Word(value) == 0 ? 1 : 0);
        return;
    }
    env.Fatal(key);
}

} // namespace obl::building_set_variable
