#pragma once

#include "core/ascii_case.h"
#include "game/ui/sprite.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// Construct (the building key): docs/architecture/objects.md. Pure logic: whatever leaves the object (the base
// constructor, the definition tables, the sprite bank, the RNG) goes through an `Env` that the game side maps onto the
// game's services (src/game/objects/building_construct.cpp) and the unit test onto a recorder.
//
// Env:
//   const BuildingDef* Definition(const char* key)           the building definition selected by key
//   int32_t DayTimeTicks()                                   current hour truncated to an integer; queried twice
//   int Rand()                                               the game's `rand` (0..32767)
//   int CommodityFamily(int id) / int AlcoholFamilyId()      the commodity family and the id of the ALCOHOL family
//   const BeerDef* Beer(int id) / int BeerCount()            beer definition lookup / WorldParams::m_numBeers
// const char* CommodityDisplayName(int id) commodity display name lookup (the "Beer" text, not the key)
//   SpriteInstance* CreateSprite(const char* name)           sprite creation from the bank
namespace obl::building_construction {

using object::Building;
using tables::BeerDef;
using tables::BuildingDef;
using ui::SpriteInstance;

constexpr double kRandScale = 3.0518509447574615e-05; // 1/32767 as a float, the exact scale of a draw
constexpr int32_t kDiscoSpriteType = 0xc; // the sprite type of BEACHDISCO_ANIM's sprite

// The slot that sells beer takes the price and cost of the current beer: cost = BASECOST + STRCOST * strength (the product and the sum stay in double precision
// (53 bits), the slot gets one float), price = PRICE. The first commodity of the definition whose display name is "Beer" is the slot.
template<class Env> void ApplyBeer(Building& building, const BuildingDef& def, Env& env)
{
    for (uint32_t slot = 0; slot < static_cast<uint32_t>(def.m_NumCommodities); ++slot) {
        if (!ascii::EqualsNoCase("Beer", env.CommodityDisplayName(def.m_Commodity[slot]))) continue;
        const BeerDef* beer = env.Beer(building.m_BeerType);
        building.m_Commod_Cost[slot] = beer->m_BaseCost;
        building.m_Commod_Cost[slot] =
                static_cast<float>(static_cast<double>(beer->m_StrCost) * static_cast<double>(building.m_BeerStrength) + beer->m_BaseCost);
        building.m_Commod_Price[slot] = beer->m_Price;
        break;
    }
}

// The first commodity of the ALCOHOL family settles the beer (and ends the search): the definition's default beer when it is in use (m_scriptFlag,
// 1 for every beer the data loads), else the first beer that is.
template<class Env> void ChooseBeer(Building& building, const BuildingDef& def, Env& env)
{
    building.m_BeerStrength = 0.5f;
    building.m_BeerType = -1;
    for (uint32_t i = 0; i < static_cast<uint32_t>(def.m_NumCommodities); ++i) {
        if (env.CommodityFamily(def.m_Commodity[i]) != env.AlcoholFamilyId()) continue;
        if (env.Beer(def.m_DefaultBeer)->m_scriptFlag) {
            if (building.m_BeerType != def.m_DefaultBeer) {
                building.m_BeerType = def.m_DefaultBeer;
                ApplyBeer(building, def, env);
            }
        } else {
            for (int id = 0; id < env.BeerCount(); ++id) {
                if (!env.Beer(id)->m_scriptFlag) continue;
                if (building.m_BeerType != id) {
                    building.m_BeerType = id;
                    ApplyBeer(building, def, env);
                }
                break;
            }
        }
        break;
    }
}

// RNG draws, in order: the repair interval (1..8), then the lights-on and lights-off hours.
// Building's constructor runs it after Prop's.
template<class Env> void Construct(Building& building, const char* key, Env& env)
{
    const BuildingDef& def = *env.Definition(key);
    building.m_definition = &def;

    building.m_numFemalestaff = 0;
    building.m_numMalestaff = 0;
    building.m_soundPlaying = 0;
    building.m_NumNonServestaff = 0;
    building.m_queue = nullptr;
    building.m_events = nullptr;
    building.m_OpenStatus = 1;
    building.m_NumGuests = 0;
    building.m_SatisfactionPercentage = 0;
    building.m_incomeToday = 0;
    building.m_costToday = 0;
    building.m_profitToday = 0;
    building.m_averageProfit = 0;
    building.m_PowerConsumption = 0;
    building.m_BuildingAge = 0;
    building.m_CurCapacity = 0;
    building.m_lastFaultDayTime = -1.0f;
    building.m_LastFaultDay = 0;
    building.m_NumFaults = 0;
    building.m_UserRepairInterval = 8;
    building.m_RepairInterval =
            static_cast<int32_t>(static_cast<uint32_t>(env.Rand()) % static_cast<uint32_t>(building.m_UserRepairInterval) + 1);
    building.m_RepairState = 0;
    building.m_LastRepairCall = env.DayTimeTicks();
    building.m_LastFailTime = env.DayTimeTicks();
    building.m_lastRepairDayTime = -1.0f;
    building.m_LastRepairDay = 0;
    building.m_wasBroken = 0;
    building.m_NumVehicles = 0;
    building.m_numLoungers = 0;
    building.m_NumVisitsToday = 0;
    building.m_VisitsSinceCleaned = 0;
    building.m_numTables = 0;
    building.m_burnLimbo = 0;
    building.m_NumTimesCleanedToday = 0;
    building.m_closedForPower = 0;
    building.m_userClose = 0;
    building.m_calledForInspection = 0;
    building.m_calledForRepair = 0;
    building.m_hasBall = 0;
    building.m_builtByUser = 0;
    std::memset(building.m_vehicleInUse, 0, sizeof(building.m_vehicleInUse));
    building.m_queue_direction[1] = 12.0f;
    building.m_queue_direction[0] = -12.0f;

    // EQPTHIRE (the equipment hire) starts with twenty of each toy.
    const int32_t toys = building.m_definition->m_Behavior == BuildingBehavior::EquipmentHire ? 20 : 0;
    building.m_NumSnorkels = toys;
    building.m_NumLilos = toys;
    building.m_NumBoards = toys;

    for (uint32_t i = 0; i < static_cast<uint32_t>(def.m_NumCommodities); ++i) {
        building.m_Commod_Intensity[i][0] = def.m_CommodityAB[i][0];
        building.m_Commod_Intensity[i][1] = def.m_CommodityAB[i][1];
        building.m_Commod_Price[i] = def.m_CommodityCost[i];
        building.m_Commod_Cost[i] = def.m_CommodityF[i];
        building.m_Commod_PriceFactor[i] = 1.0f;
    }
    ChooseBeer(building, def, env);

    std::memset(building.m_profitRecord, 0, sizeof(building.m_profitRecord));
    building.m_sprite = env.CreateSprite(def.m_Sprite.c_str());
    std::memset(building.m_animSprites, 0, sizeof(building.m_animSprites));
    for (uint32_t i = 0; i < static_cast<uint32_t>(def.m_NumAnimSprites); ++i) {
        SpriteInstance* sprite = env.CreateSprite(def.m_AnimSprites[i].m_Sprite.c_str());
        building.m_animSprites[i] = sprite;
        if (ascii::EqualsNoCase(def.m_AnimSprites[i].m_Sprite.c_str(), "BEACHDISCO_ANIM")) sprite->m_sprite->m_type = kDiscoSpriteType;
    }
    building.m_burn = 1;
    // Fix: the screen extents used to stay as the heap gave them until the first Draw baked the
    // building. A building demolished before that (never on screen, or headless) queued a block rebuild of that garbage
    // rectangle: on Linux a range of ~1e9 x 1e9 blocks that exhausted the heap. Empty extents queue nothing.
    building.m_rScrExtents = {};
    building.m_rScrRect = {};
    building.m_Entrance = 0;
    building.m_walkPoints = def.m_NumWalkPoints ? def.m_walkPoints.data() : nullptr;
    building.m_numWalkPoints = def.m_NumWalkPoints ? static_cast<uint32_t>(def.m_NumWalkPoints) : 0;
    building.m_LastMsgTime = 0;
    building.m_constructionSprite = nullptr;
    if (def.m_customConstruct) building.m_constructionSprite = env.CreateSprite(def.m_Construct.c_str());

    building.m_shortageSprite = env.CreateSprite("UI3_fb_shortage");
    building.m_spannerSprite = env.CreateSprite("UI3_fb_spanner");
    building.m_shortageSprite->m_fps = 2;
    building.m_spannerSprite->m_fps = 2;
    building.m_lightsOn = static_cast<float>(static_cast<double>(env.Rand()) * kRandScale + 19.5);
    building.m_InfluenceArea = -1;
    building.m_lightsOff = static_cast<float>(static_cast<double>(env.Rand()) * kRandScale + 5.5);
}

} // namespace obl::building_construction
