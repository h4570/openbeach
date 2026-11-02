#include "core/char16.h"
#include "core/ascii_case.h"
#include "game/objects/object_base.h"
#include "game/world/world_queries.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/commodity_providers.h"
#include "game/world/world_queries_api.h"
#include "game/ui/text_table.h"
#include "game/world/world_params.h"
#include "game/map/grid_state.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/tables/commodity_providers.h"
#include "game/objects/building_events_state.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/objects/building_methods.h"

#include <cstdint>
#include <cstdio>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace obl::object {
using game::GetCommodityProviders;
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::GetWorldParams;
using game::Rand;

namespace {

using commodity::Providers;
using map::Grid;
using object::Building;
using object::BuildingEvents;
using tables::BuildingDef;
using tables::WorldParams;

using ScoreFunc = float (*)(const Building*, int, int);
const ScoreFunc kProviderScore{&building_api::GetProviderScore};

// The float constants of the score in the order the score applies them; the intermediate sums stay in
// double (53-bit precision).
constexpr float kRandScale = std::bit_cast<float>(0x38000100u); // 1 / 32767
constexpr float kRandWeight = std::bit_cast<float>(0xba6bedfbu); // -0.0009
constexpr float kDistanceScale = 1.0f / 256.0f;
constexpr float kScoreBias = std::bit_cast<float>(0x3a83126fu); // 0.001
constexpr int kSouvenirFamily = 10;

// The six commodities the guests rent at a shop on the beach; they need the sea area in front of it to be open.
constexpr const char* kRentals[] = {"LILOS", "SURFHIRE", "SURFBOARD", "BOATHIRE", "SNORKEL", "PEDHIRE"};
constexpr const char* kLiveEvent = "LIVEVENT";
constexpr const char* kAlcohol = "ALCOHOL";

// Return the commodity script key (COM_<key>) rather than its display text.
const char* CommodityKey(const WorldParams& params, int commodity)
{
    return params.m_commodities[commodity].m_key.c_str();
}

const char* SlotKey(const Building& building, int slot)
{
    return CommodityKey(*GetWorldParams(), building.m_definition->m_Commodity[slot]);
}

// The id of the commodity family with this key, -1 when
// there is none; the data always has the families the code names, so -1 (never equal to a family) never matches.
int FamilyId(const WorldParams& params, const char* key)
{
    for (int i = 0; i < params.m_numCommodityFamilies; ++i)
        if (ascii::EqualsNoCase(params.m_commodityFamilies[i].m_key, key)) return params.m_commodityFamilies[i].m_id;
    return -1;
}

// A building with no beer selected carries -1. Its former reference price came from the zeroed spare event record.
const tables::BeerDef& BeerAt(const WorldParams& params, int beer)
{
    static const tables::BeerDef kNoBeer{};
    return beer == -1 ? kNoBeer : params.m_beers[beer];
}

const tables::EventDef& EventAt(const WorldParams& params, int event)
{
    return params.m_events[event];
}

// The show the schedule of a LIVEVENT building is on (the events' current slot index selects it). The schedule
// is not checked: a building with a LIVEVENT slot always has one.
const object::EventSlot* CurrentShow(const Building& building)
{
    const BuildingEvents* events = building.m_events;
    return events ? &events->m_slots[events->m_CurrentSlot] : nullptr;
}

} // namespace

bool IsSlotUsable(int buildingId, int slot)
{
    const auto* building = static_cast<const Building*>(object_manager::ObjectById(GetObjectManager(), buildingId));
    const char* key = SlotKey(*building, slot);
    bool rental = false;
    for (const char* name : kRentals) rental = rental || ascii::EqualsNoCase(key, name);
    if (!rental) return true;
    // The beach area containing the point numbers the building's spot in the list of beach areas, and the sea area with the same
    // number is the one that has to be open (the area opening flag kind 1).
    Grid* grid = GetGrid();
    int index = grid->GetBeachAreaOfPoint(building->m_WorldXYZ[0], building->m_WorldXYZ[1], building->m_WorldXYZ[2]);
    const map::Area* area = Grid::AreaAt(grid->m_seaAreas, index);
    return area && !area->m_shut;
}

double SlotPrice(const Building& building, int slot)
{
    if (ascii::EqualsNoCase(SlotKey(building, slot), kLiveEvent)) {
        const object::EventSlot* show = CurrentShow(building);
        return show ? show->m_price : 0.0;
    }
    return building.m_Commod_Price[slot];
}

double SlotReferencePrice(const Building& building, int slot)
{
    const WorldParams& params = *GetWorldParams();
    const BuildingDef& def = *building.m_definition;
    // Products of two floats are exact in a double, which is what the double arithmetic gives.
    double factor = building.m_Commod_PriceFactor[slot];
    if (params.m_commodities[def.m_Commodity[slot]].m_family == FamilyId(params, kAlcohol))
        return static_cast<double>(BeerAt(params, building.m_BeerType).m_Price) * factor;
    if (ascii::EqualsNoCase(SlotKey(building, slot), kLiveEvent)) {
        const object::EventSlot* show = CurrentShow(building);
        return show ? static_cast<double>(EventAt(params, show->m_EventID).m_price) * factor : 0.0;
    }
    return static_cast<double>(def.m_CommodityCost[slot]) * factor;
}

namespace {} // namespace

namespace {
// Hash building ids in object-list order so replacing a demolished building invalidates provider caches even when the count stays
// unchanged. Otherwise a later guest search could follow a cached provider id whose object no longer exists.
uint64_t BuildingSetSignature(const object::Manager& objects)
{
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t b = 0; b < objects.m_buildings.size(); ++b) hash = (hash ^ objects.m_buildings[b]->m_id) * 1099511628211ull;
    return hash;
}
// The commodity family each provider list collects, by desire index: a desire that no building serves (umbrellas,
// hard sex, sleep, scared and the unused slot) asks for the catalog's empty family "NONE".
constexpr const char* kProviderFamilyKeys[commodity::kFamilies] = {
        "ACCOMDTN",
        "ALCOHOL",
        "AMUSEMNT",
        "FIRSTAID",
        "PILLS",
        "MEALS",
        "PARTYFUN",
        "SHOWER",
        "SNACKS",
        "SOFTDRNK",
        "SOUVENIR",
        "SPORTFUN",
        "SUNBATHE",
        "SWIMFUN",
        "RELIEF",
        "WATERFUN",
        "STROLL",
        "LEAVE",
        "VANDAL",
        "FIGHT",
        "FLIRT",
        "NONE",
        "LOTION",
        "NONE",
        "NONE",
        "NONE",
};

} // namespace

void Rebuild(Providers& providers)
{
    const int buildings = GetWorld()->m_numBuildings;
    object::Manager* objects = GetObjectManager();
    const uint64_t signature = BuildingSetSignature(*objects);
    if (providers.buildingCount == buildings && providers.hasSignature && providers.signature == signature) return;
    providers.hasSignature = true;
    providers.signature = signature;
    WorldParams* params = GetWorldParams();
    const char* const* keys = kProviderFamilyKeys;
    for (int family = 0; family < commodity::kFamilies; ++family) {
        // The id is looked up for every family first; the data has every family.
        const int familyId = world::world_params::GetCommodityFamilyId(params, keys[family]);
        std::vector<commodity::ProviderRecord>& records = providers.records[family];
        records.clear();
        for (uint32_t b = 0; b < objects->m_buildings.size(); ++b) {
            auto* building = static_cast<Building*>(objects->m_buildings[b]);
            const BuildingDef& def = *building->m_definition;
            // The slot counter is unsigned; a definition without commodities skips the loop.
            for (uint32_t slot = 0; slot < static_cast<uint32_t>(def.m_NumCommodities); ++slot) {
                if (params->m_commodities[def.m_Commodity[slot]].m_family != familyId) continue;
                int tile[2];
                building->GetTilePosition(tile);
                records.push_back({static_cast<int>(building->m_id), static_cast<int>(slot), tile[0], tile[1]});
            }
        }
    }
    providers.buildingCount = buildings;
}

namespace {
ProviderSearchObserver g_searchObserver = nullptr;

void WriteThought(void*, Building& building, const Char16* text, int guestId)
{
    BuildingAddThought(&building, const_cast<Char16*>(text), guestId);
}

// The thought of a refused candidate: the text key is `prefix + name + digit`, the digit the next rand() % 3 + 1.
void RefuseWithThought(const char* prefix, const char* name, Building& building, int guestId, ThoughtSink sink, void* context)
{
    std::string key = std::string(prefix) + name + static_cast<char>('0' + Rand() % 3 + 1);
    const Char16* text = ui::text_table::Lookup(key.c_str());
    sink(context, building, text, guestId);
}
} // namespace

ProviderAnswer FindProvider(
        Providers& providers, const ProviderSearch& search, int& closed, int& beachClosed, ThoughtSink sink, void* context)
{
    if (!sink) sink = &WriteThought;
    const int family = search.family;
    ProviderAnswer answer;
    if (providers.records[family].empty()) {
        // Nobody offers the family: the complaint counter of the feedback manager grows, except for souvenirs while no souvenir shop
        // may be built yet (nothing to complain about).
        closed = 0;
        beachClosed = 0;
        bool complain = true;
        if (family == kSouvenirFamily) {
            int shop = world::world_params::GetBuildingId(GetWorldParams(), "SOUVSHOP");
            complain = GetWorld()->BuildingAllowed(shop);
        }
        if (complain) ++providers.complaints[family];
        return answer;
    }

    object::Manager* objects = GetObjectManager();
    const WorldParams& params = *GetWorldParams();
    float best = -1.0f;
    for (const commodity::ProviderRecord& record : providers.records[family]) {
        auto* building = static_cast<Building*>(object_manager::ObjectById(objects, record.id));
        if (static_cast<int>(building->m_id) == search.excludedId) {
            if (!answer.found) answer.retry = true;
            continue;
        }
        const bool usable = IsSlotUsable(record.id, record.slot);
        if (building->m_OpenStatus == 0 && usable) {
            int distance = std::abs(search.tile[1] - record.y) + std::abs(search.tile[0] - record.x);
            float distanceTerm = static_cast<float>(distance);
            float score = kProviderScore(building, record.slot, search.preference != 0 ? 1 : 0);
            int draw = Rand();
            double value = static_cast<double>(draw) * kRandScale * kRandWeight;
            value += 1.0 - static_cast<double>(distanceTerm) * kDistanceScale;
            value += score;
            value += kScoreBias;
            if (value > static_cast<double>(best)) {
                best = static_cast<float>(value);
                answer.id = record.id;
                answer.slot = record.slot;
                answer.idSet = answer.slotSet = true;
                answer.found = true;
            }
            continue;
        }
        // A "broken" branch taken when (3 - RepairState) * 0.33 == 3.0 is left out: no integer RepairState
        // makes that true (3 / 0.33 is not whole), so `broken` stays 0.
        if (!BuildingIsUserOrPowerClosedDuringHours(building)) {
            if (usable) {
                // Open hours, not shut by the player or the power cut, yet not open: a short wait, no thought.
                answer.retry = true;
                continue;
            }
            if (!answer.found && closed == 0) { // `broken` is always 0
                answer.id = record.id;
                answer.slot = record.slot;
                answer.idSet = answer.slotSet = true;
            }
            ++beachClosed;
            RefuseWithThought("GUEST_THOUGHT_BEACH_CLOSED_", CommodityKey(params, building->m_definition->m_Commodity[record.slot]),
                    *building, search.guestId, sink, context);
        } else {
            if (!answer.found) {
                answer.id = record.id;
                answer.idSet = true;
            }
            ++closed;
            RefuseWithThought("GUEST_THOUGHT_CLOSED_", building->m_definition->m_key.c_str(), *building, search.guestId, sink, context);
        }
    }
    if (answer.found) answer.retry = false;
    return answer;
}

void SetProviderSearchObserver(ProviderSearchObserver observer)
{
    g_searchObserver = observer;
}

namespace {

// ---- handlers ---------------------------------------------------------------------------------------------------------------

// Provider diagnostics count queries; the drivers below exercise every slot and commodity through these entry points.
test::CallCounter g_usable("objects.providers-is-slot-usable");
test::CallCounter g_price("objects.building-get-slot-price");
test::CallCounter g_reference("objects.building-get-slot-reference-price");
test::CallCounter g_rebuild("objects.providers-rebuild");
test::CallCounter g_find("objects.providers-find-provider");

} // namespace

// ---- facades (commodity_providers.h) -------------------------------------------------------------------------------------

namespace commodity_providers {

bool IsSlotUsable(Providers*, int buildingId, int slot)
{
    g_usable.Count();
    return object::IsSlotUsable(buildingId, slot);
}

double SlotPrice(Building* building, int slot)
{
    g_price.Count();
    return object::SlotPrice(*building, slot);
}

double SlotReferencePrice(Building* building, int slot)
{
    g_reference.Count();
    return object::SlotReferencePrice(*building, slot);
}

void Rebuild(Providers* providers)
{
    const int before = providers->buildingCount;
    object::Rebuild(*providers);
    if (providers->buildingCount != before) g_rebuild.Count();
}

int FindProvider(Providers* providers, int* slot, char* found, int* broken, int* closed, int* beachClosed, char* retry, int preference,
        int family, int* tile, int excludedId, int guestId)
{
    const ProviderSearch search{preference, family, tile, excludedId, guestId};
    ProviderSearchRecord record{search, {tile[0], tile[1]}, *closed, *beachClosed};
    g_find.Count();
    const bool searched = !providers->records[family].empty();
    ProviderAnswer answer = FindProvider(*providers, search, *closed, *beachClosed);
    // A search over a non-empty list always writes the slot, one over an empty list never does.
    if (searched) *slot = answer.slot;
    *found = answer.found;
    *broken = answer.broken;
    *retry = answer.retry;
    const int result = answer.id;
    record.result = result;
    record.slot = *slot;
    record.closed = *closed;
    record.beachClosed = *beachClosed;
    record.broken = *broken;
    record.found = *found != 0;
    record.retry = *retry != 0;
    if (g_searchObserver) g_searchObserver(record);
    return result;
}

} // namespace commodity_providers

namespace {

// ---- call-drive --------------------------------------------------------------------------------------------------------
// The living world has a few providers and no rentals on the wrong side of a closed sea area, so `call-drive` asks every
// slot of every building and then every commodity of the data in the first slot of one building, with the sea areas open,
// shut and mixed, a schedule of shows for LIVEVENT and every beer. Everything it changes is put back at the end.

struct Restore {
    BuildingDef* def;
    int commodity[5];
    Building* building;
    BuildingEvents* events;
    int beerType;
    float price[5], factor[5];
    std::vector<std::pair<map::Area*, bool>> areas;
};

Restore Save(Building* building)
{
    Restore saved{};
    saved.def = const_cast<BuildingDef*>(building->m_definition);
    std::memcpy(saved.commodity, saved.def->m_Commodity.data(), sizeof(saved.commodity));
    saved.building = building;
    saved.events = building->m_events;
    saved.beerType = building->m_BeerType;
    std::memcpy(saved.price, building->m_Commod_Price, sizeof(saved.price));
    std::memcpy(saved.factor, building->m_Commod_PriceFactor, sizeof(saved.factor));
    for (map::Area& node : GetGrid()->m_seaAreas) saved.areas.emplace_back(&node, node.m_shut);
    return saved;
}

void Put(const Restore& saved)
{
    std::memcpy(saved.def->m_Commodity.data(), saved.commodity, sizeof(saved.commodity));
    saved.building->m_events = saved.events;
    saved.building->m_BeerType = saved.beerType;
    std::memcpy(saved.building->m_Commod_Price, saved.price, sizeof(saved.price));
    std::memcpy(saved.building->m_Commod_PriceFactor, saved.factor, sizeof(saved.factor));
    for (auto [node, shut] : saved.areas) node->m_shut = shut;
}

Building* BuildingAt(uint32_t i)
{
    return static_cast<Building*>(GetObjectManager()->m_buildings[i]);
}

uint32_t Next(uint32_t& seed)
{
    return (seed = seed * 1664525u + 1013904223u) >> 8;
}

// Every commodity of the data in the first slot of each building (and every slot count), so that each family collects every kind of record.
void DriveRebuild(int rounds)
{
    object::Manager* objects = GetObjectManager();
    Providers* providers = GetCommodityProviders();
    if (!GetWorld() || !objects || !providers) {
        OBL_LOG("autotest: FAILED call-drive commodity-providers-rebuild: no world");
        return;
    }
    const WorldParams& params = *GetWorldParams();
    for (int round = 0; round < rounds; ++round) {
        providers->Invalidate();
        commodity_providers::Rebuild(providers);
        for (uint32_t b = 0; b < objects->m_buildings.size(); ++b) {
            Building* building = BuildingAt(b);
            Restore saved = Save(building);
            const int count = saved.def->m_NumCommodities;
            for (int commodity = 0; commodity < params.m_numCommodities; ++commodity) {
                saved.def->m_Commodity[0] = commodity;
                saved.def->m_NumCommodities = commodity % 6;
                providers->Invalidate();
                commodity_providers::Rebuild(providers);
            }
            saved.def->m_NumCommodities = count;
            Put(saved);
        }
        // Back to the world's own tables.
        providers->Invalidate();
        commodity_providers::Rebuild(providers);
    }
}

void DriveUsable(int rounds)
{
    object::Manager* objects = GetObjectManager();
    Providers* providers = GetCommodityProviders();
    if (!GetWorld() || !objects || !providers || objects->m_buildings.size() == 0) {
        OBL_LOG("autotest: FAILED call-drive commodity-providers-is-slot-usable: no world");
        return;
    }
    const WorldParams& params = *GetWorldParams();
    uint32_t seed = 0x2468ace;
    for (int round = 0; round < rounds; ++round) {
        for (uint32_t b = 0; b < objects->m_buildings.size(); ++b) {
            Building* building = BuildingAt(b);
            Restore saved = Save(building);
            for (int slot = 0; slot < saved.def->m_NumCommodities; ++slot)
                commodity_providers::IsSlotUsable(providers, static_cast<int>(building->m_id), slot);
            // Every commodity of the data in slot 0, sea areas open, shut and mixed.
            for (int commodity = 0; commodity < params.m_numCommodities; ++commodity) {
                saved.def->m_Commodity[0] = commodity;
                for (int mode = 0; mode < 3; ++mode) {
                    for (auto [node, shut] : saved.areas) node->m_shut = mode == 2 ? (Next(seed) & 1) != 0 : mode != 0;
                    commodity_providers::IsSlotUsable(providers, static_cast<int>(building->m_id), 0);
                }
            }
            Put(saved);
        }
    }
}

void DrivePrices(int rounds)
{
    object::Manager* objects = GetObjectManager();
    if (!GetWorld() || !objects || objects->m_buildings.size() == 0) {
        OBL_LOG("autotest: FAILED call-drive building-get-slot-price: no world");
        return;
    }
    const WorldParams& params = *GetWorldParams();
    uint32_t seed = 0x13579bd;
    for (int round = 0; round < rounds; ++round) {
        for (uint32_t b = 0; b < objects->m_buildings.size(); ++b) {
            Building* building = BuildingAt(b);
            Restore saved = Save(building);
            for (int slot = 0; slot < saved.def->m_NumCommodities; ++slot) {
                commodity_providers::SlotPrice(building, slot);
                commodity_providers::SlotReferencePrice(building, slot);
            }
            // A schedule of three shows with distinct prices for the LIVEVENT slot, every beer for the alcohol family, odd
            // factors; each commodity of the data in each of the five slots.
            BuildingEvents events{};
            for (int show = 0; show < 3; ++show) {
                events.m_slots[show].m_price = 1.5f + static_cast<float>(show) * 2.25f;
                events.m_slots[show].m_EventID = static_cast<int32_t>(Next(seed) % static_cast<uint32_t>(params.m_numEvents));
            }
            building->m_events = &events;
            for (int slot = 0; slot < 5; ++slot) {
                building->m_Commod_Price[slot] = static_cast<float>(Next(seed) & 0xffff) / 64.0f;
                building->m_Commod_PriceFactor[slot] = static_cast<float>(Next(seed) & 0xff) / 7.0f;
            }
            for (int commodity = 0; commodity < params.m_numCommodities; ++commodity) {
                for (int slot = 0; slot < 5; ++slot) {
                    saved.def->m_Commodity[slot] = commodity;
                    events.m_CurrentSlot = static_cast<int32_t>(Next(seed) % 3);
                    for (int beer = -1; beer < params.m_numBeers; ++beer) {
                        building->m_BeerType = beer;
                        commodity_providers::SlotPrice(building, slot);
                        commodity_providers::SlotReferencePrice(building, slot);
                    }
                }
            }
            Put(saved);
        }
    }
}

// ---- life cycle ----------------------------------------------------------------------------------------------------------

test::CallCounter g_construct("objects.providers-construct"), g_destruct("objects.providers-destruct");

} // namespace

namespace commodity_providers {

Providers* Create()
{
    g_construct.Count();
    return new Providers();
}

void Destroy(Providers* providers)
{
    g_destruct.Count();
    delete providers;
}

} // namespace commodity_providers

bool RegisterCommodityProviderDrivers()
{
    g_rebuild.SetDriver(&DriveRebuild);
    g_usable.SetDriver(&DriveUsable);
    g_price.SetDriver(&DrivePrices);
    return true;
}

} // namespace obl::object
