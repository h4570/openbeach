#include "core/ascii_case.h"
#include "unit_test.h"

#include "core/json_schema.h"
#include "importer/src/readers/building_list.h"
#include "importer/src/readers/building_behavior_keys.h"
#include "importer/src/writers/catalog_writer.h"
#include "game/tables/catalog_load.h"
#include "formats/schemas.h"
#include "temp_dir.h"
#include "importer/src/readers/world_params_process.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

// obl-convert catalog (converter/catalog.md): the converter's own BuildingList reader must give what the engine's parser
// (world_params_process.h) loads, field by field. The synthetic list uses invented keys and values (provenance rule); the data
// case compares the user's run/ file.

// A count mismatch reports both values, then leaves the case before indexing past the shorter list.
#define OBL_REQUIRE_EQ(a, b)                                                                                                               \
    do {                                                                                                                                   \
        OBL_CHECK_EQ(a, b);                                                                                                                \
        OBL_REQUIRE((a) == (b));                                                                                                           \
    } while (0)

using namespace obl::convert;
namespace fs = std::filesystem;
using obl::tables::WorldParams;

namespace {

std::vector<void*> g_heap;
std::vector<std::string> g_fatals;

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
const obl::world_params_add::Env kEnv{&RecordFatal, &Allocate};

struct Engine {
    std::unique_ptr<WorldParams> params = std::make_unique<WorldParams>();
    explicit Engine(const std::string& text)
    {
        g_fatals.clear();
        std::vector<char> buffer(text.begin(), text.end());
        buffer.push_back('.');
        ok = obl::world_params_process::Process(*params, kEnv, buffer.data(), static_cast<uint32_t>(text.size())) && g_fatals.empty();
    }
    ~Engine()
    {
        for (void* memory : g_heap) std::free(memory);
        g_heap.clear();
    }
    bool ok = false;
};

// The packs hold canonical spellings, so the parity checks compare names without case.
std::string Str(std::string text)
{
    for (char& c : text) c = obl::ascii::Lower(c);
    return text;
}
std::string Str(const char* text)
{
    return Str(std::string(text ? text : ""));
}

// Every value the converter keeps equals the engine's record.
void CompareWithEngine(const std::string& text, const WorldParams* loaded = nullptr)
{
    Engine engine(text);
    OBL_REQUIRE(engine.ok);
    const WorldParams& p = loaded ? *loaded : *engine.params;
    if (loaded) {
        // Compare all fixed fields, including ids, flags and unused slots. Heap contents are checked below.
        auto actual = std::make_unique<WorldParams>(*loaded);
        auto expected = std::make_unique<WorldParams>(*engine.params);
        // The engine's parser keeps no behavior; the catalog's value must be the one the key gives (building_behavior.h).
        for (auto& b : expected->m_buildings) b.m_Behavior = obl::BehaviorFromKey(b.m_key.c_str());
        for (auto* params : {actual.get(), expected.get()}) {
            for (auto& b : params->m_buildings) {
                b.m_walkPoints.clear();
                b.m_basePlateTypes.clear();
                b.m_footprint = nullptr;
            }
        }
        OBL_CHECK(std::memcmp(actual.get(), expected.get(), offsetof(WorldParams, m_openingHours)) == 0);
        // Keys and sprite names are canonical in the packs, so the spelling is left out of the comparison.
        for (auto* params : {actual.get(), expected.get()}) {
            const auto foldKeys = [](auto& rows) {
                for (auto& row : rows) row.m_key = Str(row.m_key);
            };
            foldKeys(params->m_openingHours);
            foldKeys(params->m_tables);
            foldKeys(params->m_events);
            foldKeys(params->m_beers);
            foldKeys(params->m_fines);
            for (auto& t : params->m_tables) {
                for (std::string* text : {&t.m_TableSprite, &t.m_ChairSprite, &t.m_SitDownAnim, &t.m_GetUpAnim, &t.m_waiterSprite,
                             &t.m_waiterWalkAnim, &t.m_waiterServeAnim, &t.m_callWaiterAnim, &t.m_waitWaiterAnim})
                    *text = Str(*text);
                for (auto& anim : t.m_satDownAnims) anim = Str(anim);
            }
        }
        OBL_CHECK(actual->m_openingHours == expected->m_openingHours);
        OBL_CHECK(actual->m_tables == expected->m_tables);
        OBL_CHECK(actual->m_events == expected->m_events);
        OBL_CHECK(actual->m_beers == expected->m_beers);
        OBL_CHECK(actual->m_fines == expected->m_fines);
    }
    BuildingList list;
    std::string error;
    const bool parsed = ParseBuildingList(text, list, error);
    if (!parsed) std::printf("  %s\n", error.c_str());
    OBL_REQUIRE(parsed);

    OBL_REQUIRE_EQ(list.families.size(), static_cast<size_t>(p.m_numCommodityFamilies));
    for (size_t i = 0; i < list.families.size(); ++i) {
        OBL_CHECK_EQ(Str(list.families[i].key), Str(p.m_commodityFamilies[i].m_key));
        OBL_CHECK_EQ(Str(list.families[i].name), Str(p.m_commodityFamilies[i].m_name));
    }
    OBL_REQUIRE_EQ(list.commodities.size(), static_cast<size_t>(p.m_numCommodities));
    for (size_t i = 0; i < list.commodities.size(); ++i) {
        OBL_CHECK_EQ(Str(list.commodities[i].key), Str(p.m_commodities[i].m_key));
        OBL_CHECK_EQ(Str(list.commodities[i].name), Str(p.m_commodities[i].m_name));
        OBL_CHECK_EQ(list.commodities[i].family, p.m_commodities[i].m_family);
    }
    OBL_REQUIRE_EQ(list.openingHours.size(), static_cast<size_t>(p.m_numOpeningHours));
    for (size_t i = 0; i < list.openingHours.size(); ++i) {
        OBL_CHECK_EQ(Str(list.openingHours[i].key), Str(p.m_openingHours[i].m_key));
        OBL_CHECK_EQ(list.openingHours[i].open, p.m_openingHours[i].m_open);
        OBL_CHECK_EQ(list.openingHours[i].close, p.m_openingHours[i].m_close);
    }
    OBL_REQUIRE_EQ(list.desires.size(), static_cast<size_t>(p.m_numDesires));
    for (size_t i = 0; i < list.desires.size(); ++i) {
        const auto& d = p.m_desires[i];
        OBL_CHECK_EQ(Str(list.desires[i].key), Str(d.m_key));
        OBL_CHECK_EQ(Str(list.desires[i].name), Str(d.m_name));
        OBL_CHECK(list.desires[i].families == d.m_families);
    }
    OBL_REQUIRE_EQ(list.staff.size(), static_cast<size_t>(p.m_numStaff));
    for (size_t i = 0; i < list.staff.size(); ++i) {
        const BlStaff& s = list.staff[i];
        const auto& d = p.m_staff[i];
        OBL_CHECK_EQ(Str(s.key), Str(d.m_key));
        OBL_CHECK_EQ(Str(s.name), Str(d.m_Name));
        OBL_CHECK_EQ(s.female, d.m_female != 0);
        OBL_CHECK_EQ(s.wage, d.m_Wage);
        OBL_CHECK_EQ(Str(s.sprite), Str(d.m_Sprite));
        OBL_CHECK_EQ(Str(s.script), Str(d.m_ObjectType));
        OBL_CHECK_EQ(Str(s.menu), Str(d.m_Menu));
        OBL_CHECK_EQ(s.recommendedWage, d.m_RecommendedWage);
        OBL_CHECK_EQ(s.scale, d.m_scale);
        OBL_CHECK_EQ(s.expectationGrowth, d.m_expectationGrowth);
        OBL_CHECK_EQ(s.initialHappiness, d.m_initialHappiness);
        OBL_CHECK_EQ(s.startHour, d.m_StartHour);
        OBL_CHECK_EQ(s.endHour, d.m_EndHour);
    }
    OBL_REQUIRE_EQ(list.events.size(), static_cast<size_t>(p.m_numEvents));
    for (size_t i = 0; i < list.events.size(); ++i) {
        const auto& d = p.m_events[i];
        OBL_CHECK_EQ(Str(list.events[i].key), Str(d.m_key));
        OBL_CHECK_EQ(list.events[i].cost, d.m_cost);
        OBL_CHECK_EQ(list.events[i].price, d.m_price);
        OBL_CHECK_EQ(list.events[i].intensity[0], d.m_intensity[0]);
        OBL_CHECK_EQ(list.events[i].intensity[1], d.m_intensity[1]);
    }
    OBL_REQUIRE_EQ(list.beers.size(), static_cast<size_t>(p.m_numBeers));
    for (size_t i = 0; i < list.beers.size(); ++i) {
        const BlBeer& b = list.beers[i];
        const auto& d = p.m_beers[i];
        OBL_CHECK_EQ(Str(b.key), Str(d.m_key));
        OBL_CHECK_EQ(Str(b.brand), Str(d.m_Brand));
        OBL_CHECK_EQ(Str(b.cocktail), Str(d.m_Cocktail));
        OBL_CHECK_EQ(b.baseCost, d.m_BaseCost);
        OBL_CHECK_EQ(b.strengthCost, d.m_StrCost);
        OBL_CHECK_EQ(b.alcoholStrength, d.m_AlcStr);
        OBL_CHECK_EQ(b.price, d.m_Price);
        OBL_CHECK_EQ(b.effect, static_cast<int>(d.m_Effect));
    }
    OBL_REQUIRE_EQ(list.fines.size(), static_cast<size_t>(p.m_numFines));
    for (size_t i = 0; i < list.fines.size(); ++i) {
        OBL_CHECK_EQ(Str(list.fines[i].key), Str(p.m_fines[i].m_key));
        OBL_CHECK_EQ(list.fines[i].cost, p.m_fines[i].m_cost);
    }
    OBL_REQUIRE_EQ(list.vehicles.size(), static_cast<size_t>(p.m_numVehicles));
    for (size_t i = 0; i < list.vehicles.size(); ++i) {
        const BlVehicle& v = list.vehicles[i];
        const auto& d = p.m_vehicles[i];
        OBL_CHECK_EQ(Str(v.key), Str(d.m_key));
        OBL_CHECK_EQ(Str(v.name), Str(d.m_Name));
        OBL_CHECK_EQ(Str(v.sprite), Str(d.m_Sprite));
        OBL_CHECK_EQ(v.width, d.m_GridWidth);
        OBL_CHECK_EQ(v.height, d.m_GridHeight);
        OBL_CHECK_EQ(v.capacity, d.m_Capacity);
        OBL_CHECK_EQ(v.maxSpeed, d.m_MaxSpeed);
        OBL_CHECK_EQ(Str(v.towed), Str(d.m_Towed));
        OBL_CHECK_EQ(v.force, d.m_Force);
        OBL_CHECK_EQ(v.mass, d.m_Mass);
        OBL_CHECK_EQ(v.moveType, static_cast<int>(d.m_MoveType));
        OBL_CHECK_EQ(Str(v.animSet), Str(d.m_AnimSet));
        OBL_CHECK(v.staff == d.m_Staff);
        OBL_CHECK_EQ(v.creationPoint[0], d.m_CreationPoint[0]);
        OBL_CHECK_EQ(v.creationPoint[1], d.m_CreationPoint[1]);
    }
    OBL_REQUIRE_EQ(list.tables.size(), static_cast<size_t>(p.m_numTables));
    for (size_t i = 0; i < list.tables.size(); ++i) {
        const BlTable& t = list.tables[i];
        const auto& d = p.m_tables[i];
        OBL_CHECK_EQ(Str(t.key), Str(d.m_key));
        OBL_CHECK_EQ(Str(t.name), Str(d.m_Name));
        OBL_CHECK_EQ(Str(t.tableSprite), Str(d.m_TableSprite));
        OBL_CHECK_EQ(Str(t.chairSprite), Str(d.m_ChairSprite));
        OBL_CHECK_EQ(t.width, d.m_GridWidth);
        OBL_CHECK_EQ(t.height, d.m_GridHeight);
        OBL_REQUIRE_EQ(t.walkPoints.size(), static_cast<size_t>(d.m_NumWalkPts));
        for (size_t w = 0; w < t.walkPoints.size(); ++w) {
            OBL_CHECK_EQ(t.walkPoints[w][0], d.m_walkPoints[w][0]);
            OBL_CHECK_EQ(t.walkPoints[w][1], d.m_walkPoints[w][1]);
        }
        OBL_REQUIRE_EQ(t.chairs.size(), static_cast<size_t>(d.m_NumChairs));
        OBL_CHECK_EQ(t.chairs.size(), static_cast<size_t>(d.m_NumChairPos));
        OBL_CHECK_EQ(t.chairs.size(), static_cast<size_t>(d.m_NumChairDirs));
        OBL_CHECK_EQ(t.chairs.size(), static_cast<size_t>(d.m_NumChairWalkLists));
        size_t serve = 0;
        for (size_t c = 0; c < t.chairs.size(); ++c) {
            const BlChair& chair = t.chairs[c];
            OBL_CHECK_EQ(chair.pos[0], d.m_chairPositions[c][0]);
            OBL_CHECK_EQ(chair.pos[1], d.m_chairPositions[c][1]);
            OBL_CHECK_EQ(chair.dir, d.m_chairDirections[c]);
            const std::vector<int>& walk = d.m_chairWalkPoints[c];
            OBL_CHECK(chair.walkPoints == std::vector<int>(walk.begin() + 1, walk.begin() + 1 + walk[0]));
            if (chair.servePoint) {
                OBL_CHECK_EQ((*chair.servePoint)[0], d.m_servePoints[c][0]);
                OBL_CHECK_EQ((*chair.servePoint)[1], d.m_servePoints[c][1]);
                ++serve;
            }
        }
        OBL_CHECK_EQ(serve, static_cast<size_t>(d.m_NumServePoints));
        OBL_CHECK_EQ(Str(t.sitDownAnim), Str(d.m_SitDownAnim));
        OBL_CHECK_EQ(Str(t.getUpAnim), Str(d.m_GetUpAnim));
        OBL_REQUIRE_EQ(t.satDownAnims.size(), static_cast<size_t>(d.m_NumSatDownAnims));
        for (size_t a = 0; a < t.satDownAnims.size(); ++a) OBL_CHECK_EQ(Str(t.satDownAnims[a]), Str(d.m_satDownAnims[a]));
        OBL_CHECK_EQ(t.serveTable, d.m_serveTable != 0);
        OBL_CHECK_EQ(t.hasWaiter, d.m_hasPlates != 0);
        OBL_CHECK_EQ(t.offsetX, d.m_XOffsetTable);
        OBL_CHECK_EQ(t.offsetY, d.m_YOffsetTable);
        OBL_CHECK_EQ(Str(t.waiterSprite), Str(d.m_waiterSprite));
        OBL_CHECK_EQ(Str(t.waiterWalkAnim), Str(d.m_waiterWalkAnim));
        OBL_CHECK_EQ(Str(t.waiterServeAnim), Str(d.m_waiterServeAnim));
        OBL_CHECK_EQ(Str(t.callWaiterAnim), Str(d.m_callWaiterAnim));
        OBL_CHECK_EQ(Str(t.waitWaiterAnim), Str(d.m_waitWaiterAnim));
    }
    OBL_REQUIRE_EQ(list.scenery.size(), static_cast<size_t>(p.m_numScenery));
    for (size_t i = 0; i < list.scenery.size(); ++i) {
        const BlScenery& s = list.scenery[i];
        const auto& d = p.m_scenery[i];
        OBL_CHECK_EQ(Str(s.key), Str(d.m_key));
        OBL_CHECK_EQ(Str(s.name), Str(d.m_Name));
        OBL_CHECK_EQ(Str(s.sprite), Str(d.m_Sprite));
        OBL_CHECK_EQ(Str(s.anim), Str(d.m_Anim));
        OBL_CHECK_EQ(Str(s.menu), Str(d.m_Menu));
        OBL_CHECK_EQ(s.width, d.m_GridWidth);
        OBL_CHECK_EQ(s.height, d.m_GridHeight);
        OBL_CHECK_EQ(s.cost, d.m_Cost2Build);
        OBL_CHECK_EQ(s.capacity, d.m_Capacity);
        OBL_CHECK_EQ(s.icon, d.m_Icon);
        OBL_CHECK_EQ(s.category, d.m_Category);
        OBL_CHECK_EQ(s.pathFlag, d.m_PathFlag);
        OBL_CHECK_EQ(s.buildOn, static_cast<uint32_t>(d.m_BuildOn));
        OBL_CHECK_EQ(s.rating, d.m_Rating);
        OBL_CHECK_EQ(s.collide, d.m_collide != 0);
        OBL_CHECK_EQ(s.clean, d.m_clean != 0);
        OBL_CHECK_EQ(s.repair, d.m_repair != 0);
        OBL_CHECK_EQ(s.burn, d.m_burn != 0);
        OBL_CHECK_EQ(s.userRemove, d.m_userRemove != 0);
        OBL_CHECK_EQ(s.vegetation, d.m_vegFlag != 0);
        OBL_CHECK_EQ(s.water, d.m_water != 0);
    }
    OBL_REQUIRE_EQ(list.buildings.size(), static_cast<size_t>(p.m_numBuildings));
    for (size_t i = 0; i < list.buildings.size(); ++i) {
        const BlBuilding& b = list.buildings[i];
        const auto& d = p.m_buildings[i];
        OBL_CHECK_EQ(Str(b.key), Str(d.m_key));
        OBL_CHECK_EQ(Str(b.name), Str(d.m_Name));
        OBL_CHECK_EQ(Str(b.sprite), Str(d.m_Sprite));
        OBL_CHECK_EQ(Str(b.tree), Str(d.m_Tree));
        OBL_CHECK_EQ(Str(b.menu), Str(d.m_Menu));
        OBL_CHECK_EQ(b.customConstruct, d.m_customConstruct != 0);
        OBL_CHECK_EQ(Str(b.construct), Str(d.m_Construct));
        OBL_CHECK_EQ(Str(b.maleFile), Str(d.m_MaleFileName));
        OBL_CHECK_EQ(Str(b.femaleFile), Str(d.m_FemaleFileName));
        OBL_CHECK_EQ(b.icon, d.m_Icon);
        OBL_CHECK_EQ(b.category, d.m_Category);
        OBL_CHECK_EQ(b.cost, d.m_Cost2Build);
        OBL_CHECK_EQ(b.refund, d.m_RefundValue);
        OBL_CHECK_EQ(b.width, d.m_GridWidth);
        OBL_CHECK_EQ(b.height, d.m_GridHeight);
        OBL_CHECK_EQ(b.guestCapacity, d.m_GuestCapacity);
        OBL_CHECK_EQ(b.buildTime, d.m_BuildTime);
        OBL_CHECK_EQ(b.demolishTime, d.m_DemolishTime);
        OBL_CHECK_EQ(b.openingHours, d.m_OpeningHours);
        OBL_CHECK_EQ(b.maxInstances, d.m_MaxInstances);
        OBL_CHECK_EQ(b.attractor, d.m_Attractor);
        OBL_CHECK_EQ(b.maxQueue, d.m_MaxQueue);
        OBL_CHECK_EQ(b.maxStaff, d.m_MaxStaff);
        OBL_CHECK_EQ(b.power, d.m_Power);
        OBL_CHECK_EQ(b.failPercent, d.m_FailPercent);
        OBL_CHECK_EQ(b.failAge, d.m_FailAge);
        OBL_CHECK_EQ(static_cast<float>(b.maxBuilders), d.m_MaxBuilders);
        OBL_CHECK_EQ(b.avFailTime, d.m_AvFailTime);
        for (int k = 0; k < 2; ++k) {
            OBL_CHECK_EQ(b.serve[k], d.m_ServeOffset[k]);
            OBL_CHECK_EQ(b.staffOffset[k], d.m_StaffOffset[k]);
            OBL_CHECK_EQ(b.entrance[k], d.m_EntranceOffset[k]);
        }
        OBL_CHECK_EQ(b.buildOn, d.m_BuildOn);
        OBL_REQUIRE_EQ(b.commodities.size(), static_cast<size_t>(d.m_NumCommodities));
        for (size_t c = 0; c < b.commodities.size(); ++c) {
            const BlBuildingCommodity& x = b.commodities[c];
            OBL_CHECK_EQ(x.commodity, d.m_Commodity[c]);
            OBL_CHECK_EQ(x.intensityMen, d.m_CommodityAB[c][0]);
            OBL_CHECK_EQ(x.intensityWomen, d.m_CommodityAB[c][1]);
            OBL_CHECK_EQ(x.dailyIntensityFactor, d.m_CommodityC[c]);
            OBL_CHECK_EQ(x.price, d.m_CommodityCost[c]);
            OBL_CHECK_EQ(x.dailyPriceFactor, d.m_CommodityE[c]);
            OBL_CHECK_EQ(x.cost, d.m_CommodityF[c]);
        }
        OBL_CHECK(b.staff == std::vector<int>(d.m_Staff, d.m_Staff + d.m_NumStaff));
        OBL_CHECK(b.vehicles == std::vector<int>(d.m_Vehicle.begin(), d.m_Vehicle.begin() + d.m_NumVehicles));
        OBL_REQUIRE_EQ(b.vehiclePlaces.size(), static_cast<size_t>(d.m_NumVehiclePlaces));
        for (size_t v = 0; v < b.vehiclePlaces.size(); ++v) {
            OBL_CHECK_EQ(b.vehiclePlaces[v].dir, d.m_VehicleDir[v]);
            OBL_CHECK_EQ(b.vehiclePlaces[v].x, d.m_VehicleGridOffset[v][0]);
            OBL_CHECK_EQ(b.vehiclePlaces[v].y, d.m_VehicleGridOffset[v][1]);
        }
        OBL_REQUIRE_EQ(b.tables.size(), static_cast<size_t>(d.m_NumTables));
        for (size_t t = 0; t < b.tables.size(); ++t) {
            OBL_CHECK_EQ(b.tables[t].table, d.m_Table[t]);
            OBL_CHECK_EQ(b.tables[t].x, d.m_TablePos[t][0]);
            OBL_CHECK_EQ(b.tables[t].y, d.m_TablePos[t][1]);
        }
        // The engine's event slots as the game reads them: m_Event[0 .. m_NumEvents).
        OBL_REQUIRE(d.m_NumEvents <= 5);
        OBL_CHECK(b.events == std::vector<int>(d.m_Event.begin(), d.m_Event.begin() + d.m_NumEvents));
        OBL_REQUIRE_EQ(b.anims.size(), static_cast<size_t>(d.m_NumAnimSprites));
        for (size_t a = 0; a < b.anims.size(); ++a) {
            OBL_CHECK_EQ(Str(b.anims[a].sprite), Str(d.m_AnimSprites[a].m_Sprite));
            for (size_t k = 0; k < 4; ++k) {
                OBL_CHECK_EQ(b.anims[a].points[k][0], d.m_AnimSprites[a].m_X[k]);
                OBL_CHECK_EQ(b.anims[a].points[k][1], d.m_AnimSprites[a].m_Y[k]);
            }
            OBL_CHECK_EQ(b.anims[a].flag, d.m_AnimSprites[a].m_animated != 0);
        }
        OBL_REQUIRE_EQ(b.walkPoints.size(), static_cast<size_t>(d.m_NumWalkPoints));
        for (size_t w = 0; w < b.walkPoints.size(); ++w) {
            OBL_CHECK_EQ(b.walkPoints[w][0], d.m_walkPoints[w][0]);
            OBL_CHECK_EQ(b.walkPoints[w][1], d.m_walkPoints[w][1]);
        }
        OBL_REQUIRE_EQ(b.basePlates.size(), static_cast<size_t>(d.m_NumBasePlates));
        for (size_t k = 0; k < b.basePlates.size(); ++k) OBL_CHECK_EQ(b.basePlates[k], d.m_basePlateTypes[k]);
    }
}

// Invented records of every kind: the JETTY prefix rule, X/Y pairs, multi-token lines, the EVENT slot quirk with five events.
const char* const kList = R"(// comments hold anything but quotes
"COMFAM_NONE" "Nothing"
"COMFAM_FOOD" "Food Things"
"COMFAM_FUN" "Fun"
"COM_PIES" "Pies" "FOOD"
"COM_RIDES" "Rides" "FUN"
"HOURS_ALWAYS" "0" "0"
"HOURS_LATE" "18.5" "2"
"DESIRE_PECKISH" "Peckish"
"PECKISH_COMFAM" "FOOD"
"PECKISH_COMFAM" "FUN"
"STAFF_COOK" "Cook" "F" "120" "COOKSPRITE" "CookScript" "MenuCook" "100" "1.25" "0.1" "0.85" "8" "20"
"STAFF_PILOT" "Pilot" "M" "90" "PilotSprite" "PilotScript" "MenuPilot" "95" "1" "0.2" "0.5" "6" "22"
"EVENT_SONG" "EVENT_DANCE" "EVENT_MAGIC" "EVENT_JUGGLE" "EVENT_MIME"
"SONG_COST" "10.5"  "SONG_DEF_PRICE" "3.3"  "SONG_MALE_INTENSITY" "0.7"  "SONG_FEMALE_INTENSITY" "0.4"
"MIME_COST" "1"
"BEER_FIZZ"
"FIZZ_BRAND" "Fizz Brand" "FIZZ_BASECOST" "0.85" "FIZZ_STRCOST" "0.1" "FIZZ_ALCSTR" "4.5" "FIZZ_PRICE" "2.2" "FIZZ_EFFECT" "HAPPY"
"BEER_SOUR"
"SOUR_COCKTAIL" "Sour Mix" "SOUR_EFFECT" "VANDAL"
"FINE_SMALL" "SMALL_COST" "25"
"VEHICLE_RAFT"
"RAFT_NAME" "Raft" "RAFT_SPRITE" "RaftSprite" "RAFT_GRIDWIDTH" "2" "RAFT_GRIDHEIGHT" "1" "RAFT_CAPACITY" "3"
"RAFT_TERRAIN" "SEA" "RAFT_MAX_SPEED" "1.5" "RAFT_TOWED" "NONE" "RAFT_FORCE" "0.3" "RAFT_MASS" "40"
"RAFT_MOVETYPE" "BOUNDARY" "RAFT_ANIMSET" "RaftAnims" "RAFT_STAFF" "PILOT" "RAFT_CREATION_PNTX" "-3" "RAFT_CREATION_PNTY" "7.25"
"TABLE_DESK"
"DESK_NAME" "Desk" "DESK_TABLE_SPRITE" "DeskTop" "DESK_CHAIR_SPRITE" "Stool" "DESK_GRIDWIDTH" "1" "DESK_GRIDHEIGHT" "2"
"DESK_CAPACITY" "2" "DESK_NUM_WALK_PTS" "3"
"DESK_WALK_PNT_X" "-1" "DESK_WALK_PNT_Y" "2" "DESK_WALK_PNT_X" "3" "DESK_WALK_PNT_Y" "4" "DESK_WALK_PNT_X" "5.5" "DESK_WALK_PNT_Y" "6"
"DESK_NUM_CHAIRPOS_PTS" "2" "DESK_CHAIRPOS_X" "1" "DESK_CHAIRPOS_Y" "2" "DESK_CHAIRPOS_X" "3" "DESK_CHAIRPOS_Y" "4"
"DESK_CHAIR_DIRECTION" "SW" "DESK_CHAIR_DIRECTION" "E"
"DESK_CHAIR_NUM_WALKPNT" "2" "DESK_CHAIR_WALKPNT" "0" "DESK_CHAIR_WALKPNT" "1"
"DESK_CHAIR_NUM_WALKPNT" "1" "DESK_CHAIR_WALKPNT" "3"
"DESK_SIT_DOWN_ANIM" "SitAnim" "DESK_NUM_SAT_DOWN_ANIM" "2" "DESK_SAT_DOWN_ANIM" "SatA" "DESK_SAT_DOWN_ANIM" "SatB"
"DESK_GET_UP_ANIM" "" "DESK_SERVE_TABLE" "1" "DESK_XOFFSET_TABLE" "-4" "DESK_YOFFSET_TABLE" "" "DESK_HAS_WAITER" "1"
"DESK_WAITER_SPRITE" "Server" "DESK_WAITER_WALK_ANIM" "Walk" "DESK_WAITER_SERVE_ANIM" "Serve" "DESK_CALL_WAITER_ANIM" "Call"
"DESK_WAIT_WAITER_ANIM" "Wait" "DESK_WAITER_SERVE_POINT_X" "8" "DESK_WAITER_SERVE_POINT_Y" "9"
"SCENERY_Rock" "Rock_NAME" "Rock" "Rock_SPRITE" "ROCK" "Rock_ANIM" "Rock_Still" "Rock_GRIDWIDTH" "1" "Rock_GRIDHEIGHT" "1"
"Rock_COST2BUILD" "15" "Rock_BUILDON" "LAND" "Rock_BUILDON" "PATH" "Rock_COLLIDE" "1" "Rock_CAPACITY" "0" "Rock_MENU" "SceneryMenu"
"Rock_ICON" "4" "Rock_CATEGORY" "ORNAMENTS" "Rock_RATING" "0.3" "Rock_CLEAN" "0" "Rock_REPAIR" "1" "Rock_BURN" "1"
"Rock_USERREMOVE" "1" "Rock_VEGFLAG" "0" "Rock_PATHFLAG" "2" "Rock_WATER" "0"
"BUILD_KIOSK"
"KIOSK_NAME" "KIOSK_NAME_TEXT" "KIOSK_SPRITE" "KioskSprite" "KIOSK_ICON" "3" "KIOSK_COST2BUILD" "1500" "KIOSK_REFUNDVALUE" "500"
"KIOSK_GRIDWIDTH" "3" "KIOSK_GRIDHEIGHT" "2" "KIOSK_XSERVEOFFSET" "1" "KIOSK_YSERVEOFFSET" "-2" "KIOSK_STAFF_XOFFSET" "4"
"KIOSK_GUESTCAPACITY" "6" "KIOSK_BUILDTIME" "12" "KIOSK_DEMOLISHTIME" "3" "KIOSK_OPENINGHOURS" "LATE" "KIOSK_MAXINSTANCES" "2"
"KIOSK_ATTRACTOR" "FOOD" "KIOSK_TREE" "NONE" "KIOSK_MENU" "KioskMenu" "KIOSK_MAXSTAFF" "2" "KIOSK_STAFF" "COOK" "KIOSK_STAFF" "COOK"
"KIOSK_COMMODITY" "PIES" "0.5" "0.25" "1" "2.5" "1.01" "0.85"
"KIOSK_ANIMSPRITE" "Smoke" "1" "2" "3" "4" "5" "6" "7" "8" "1"
"KIOSK_BUILDON" "BEACH" "KIOSK_BUILDON" "WATER" "KIOSK_POWER" "-5" "KIOSK_NUM_WALKPOINTS" "2"
"KIOSK_WALKPOINTX" "0.5" "KIOSK_WALKPOINTY" "1" "KIOSK_WALKPOINTX" "2" "KIOSK_WALKPOINTY" "3.75"
"KIOSK_AVFAILTIME" "30.5" "KIOSK_FAIL_PERCENT" "10" "KIOSK_FAIL_AGE" "5" "KIOSK_MAX_QUEUE" "4" "KIOSK_CATEGORY" "Food"
"KIOSK_NUM_BASEPLATES" "2" "KIOSK_BASEPLATETYPE" "1" "KIOSK_BASEPLATETYPE" "3" "KIOSK_MAXBUILDERS" "2"
"KIOSK_CONSTRUCT" "KioskBuild" "KIOSK_MALEFILENAME" "male_kiosk" "KIOSK_FEMALEFILENAME" "female_kiosk"
"KIOSK_EVENT" "JUGGLE"
"KIOSK_TABLE" "DESK" "KIOSK_TABLE_POSX" "1.5" "KIOSK_TABLE_POSY" "-2"
"KIOSK_VEHICLE" "RAFT" "KIOSK_VEHICLE_DIR" "2" "KIOSK_VEHICLE_GRID_XOFFSET" "-1" "KIOSK_VEHICLE_GRID_YOFFSET" "5"
"BUILD_JETTY_TIP"
"JETTY_TIP_NAME" "Tip" "JETTY_TIP_SPRITE" "TipSprite" "JETTY_TIP_GRIDWIDTH" "1" "JETTY_TIP_GRIDHEIGHT" "1"
"JETTY_TIP_BUILDON" "SHALLOW_WATER" "JETTY_TIP_CATEGORY" "Other" "JETTY_TIP_COST2BUILD" "50"
)";

bool LoadSchemas(obl::json_schema::Registry& registry)
{
    std::string error;
    const bool ok = obl::formats::LoadEmbeddedSchemas(registry, error);
    if (!ok) std::printf("  %s\n", error.c_str());
    return ok;
}

const CatalogFile* Find(const std::vector<CatalogFile>& files, const std::string& path)
{
    for (const CatalogFile& file : files)
        if (file.path == path) return &file;
    return nullptr;
}

std::string ParseError(const std::string& text)
{
    BuildingList list;
    std::string error;
    return ParseBuildingList(text, list, error) ? std::string() : error;
}

bool Contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

OBL_TEST_CASE("convert.catalog_reader_matches_engine")
{
    CompareWithEngine(kList);
    BuildingList list;
    std::string error;
    OBL_REQUIRE(ParseBuildingList(kList, list, error));
    // The engine's EVENT quirk: the index is the event count (5), the building's count becomes JUGGLE's id + 1 over zero slots.
    OBL_CHECK(list.buildings[0].events == std::vector<int>({0, 0, 0, 0}));
    OBL_CHECK_EQ(list.buildings[1].key, std::string("JETTY_TIP"));
    OBL_CHECK_EQ(list.buildings[0].buildOn, 0xeu);
}

namespace {
struct LoadedCatalog {
    std::unique_ptr<WorldParams> params = std::make_unique<WorldParams>();
    bool Load(const fs::path& pack, std::string& error) { return obl::catalog::LoadPack(pack, *params, error); }
};
void CheckCatalogPack(const fs::path& pack, const std::string& reference)
{
    LoadedCatalog loaded;
    std::string error;
    const bool ok = loaded.Load(pack, error);
    if (!ok) std::printf("  %s\n", error.c_str());
    OBL_REQUIRE(ok);
    CompareWithEngine(reference, loaded.params.get());
}
} // namespace

OBL_TEST_CASE("catalog.load_matches_engine")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    BuildingList list;
    std::string error;
    OBL_REQUIRE(ParseBuildingList(kList, list, error));
    const obl::test::TempPath tmp("obl_catalog_load");
    int written = 0;
    OBL_REQUIRE(WriteCatalog(list, {}, *obl::convert::OpenDirectorySink(tmp.path), schemas, written, error));
    CheckCatalogPack(tmp.path, kList);
}

OBL_TEST_CASE("catalog.load_commodity_and_desire_strings_and_family_vector")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    BuildingList list;
    std::string error;
    OBL_REQUIRE(ParseBuildingList(kList, list, error));
    const std::string key(100, 'a');
    const std::string name(180, 'N');
    list.families[0].key = key;
    list.families[0].name = name;
    list.commodities[0].key = key;
    list.commodities[0].name = name;
    list.desires[0].key = key;
    list.desires[0].name = name;
    list.desires[0].families.assign(30, 0);
    const obl::test::TempPath tmp("obl_catalog_commodities");
    int written = 0;
    OBL_REQUIRE(WriteCatalog(list, {}, *obl::convert::OpenDirectorySink(tmp.path), schemas, written, error));
    LoadedCatalog loaded;
    OBL_REQUIRE(loaded.Load(tmp.path, error));
    OBL_CHECK_EQ(loaded.params->m_commodityFamilies[0].m_key, key);
    OBL_CHECK_EQ(loaded.params->m_commodityFamilies[0].m_name, name);
    OBL_CHECK_EQ(loaded.params->m_commodities[0].m_key, key);
    OBL_CHECK_EQ(loaded.params->m_commodities[0].m_name, name);
    OBL_CHECK_EQ(loaded.params->m_desires[0].m_key, key);
    OBL_CHECK_EQ(loaded.params->m_desires[0].m_name, name);
    OBL_CHECK_EQ(loaded.params->m_desires[0].m_numFamilies, 30);
    OBL_CHECK(loaded.params->m_desires[0].m_families == list.desires[0].families);
}

OBL_TEST_CASE("catalog.load_refuses_invalid_pack_transactionally")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    BuildingList list;
    std::string error;
    OBL_REQUIRE(ParseBuildingList(kList, list, error));
    const obl::test::TempPath tmp("obl_catalog_bad");
    int written = 0;
    OBL_REQUIRE(WriteCatalog(list, {}, *obl::convert::OpenDirectorySink(tmp.path), schemas, written, error));
    LoadedCatalog loaded;
    loaded.params->m_numGuests = 7;
    const auto file = tmp.path / "catalog/buildings/kiosk.json";
    std::ifstream in(file);
    auto doc = nlohmann::json::parse(in);
    in.close();
    const auto reference = doc;
    const auto refuse = [&](const nlohmann::json& bad, const char* reason) {
        {
            std::ofstream out(file);
            out << bad;
        }
        OBL_CHECK(!loaded.Load(tmp.path, error));
        OBL_CHECK(error.find(reason) != std::string::npos);
        OBL_CHECK(error.find("kiosk.json") != std::string::npos);
        OBL_CHECK_EQ(loaded.params->m_numGuests, 7);
    };
    doc["operation"]["opening_hours"] = "absent";
    refuse(doc, "missing opening_hours");
    doc = reference;
    doc["points"]["serve"][0] = 32768;
    refuse(doc, "engine range");
    doc = reference;
    doc["engine_strings"] = {{"/key", "BAYWATCH"}};
    refuse(doc, "engine_strings");
    doc = reference;
    doc["key"] = "other";
    refuse(doc, "key differs from index");
    doc = reference;
    doc["version"] = 3;
    refuse(doc, "version");
    doc = reference;
    doc["name"] = std::string(128, 'x');
    refuse(doc, "engine string capacity");
    {
        std::ofstream out(file);
        out << "{";
    }
    OBL_CHECK(!loaded.Load(tmp.path, error));
    OBL_CHECK(error.find("invalid JSON") != std::string::npos);
    fs::remove(file);
    OBL_CHECK(!loaded.Load(tmp.path, error));
    OBL_CHECK(error.find("kiosk.json") != std::string::npos);
}

OBL_TEST_CASE("catalog.load_own_keys_and_cp1252_text")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    BuildingList list;
    std::string error;
    OBL_REQUIRE(ParseBuildingList("\"COMFAM_cafe\" \"Caf\xE9\"", list, error));
    const obl::test::TempPath tmp("obl_catalog_own");
    int written = 0;
    OBL_REQUIRE(WriteCatalog(list, {}, *obl::convert::OpenDirectorySink(tmp.path), schemas, written, error));
    const auto file = tmp.path / "catalog/commodity_families/cafe.json";
    std::ifstream in(file);
    auto doc = nlohmann::json::parse(in);
    in.close();
    LoadedCatalog loaded;
    OBL_REQUIRE(loaded.Load(tmp.path, error));
    OBL_CHECK_EQ(Str(loaded.params->m_commodityFamilies[0].m_key), "cafe");
    OBL_CHECK_EQ(std::string(loaded.params->m_commodityFamilies[0].m_name), "Caf\xE9");
    // A valid schema cannot promise that a string fits the transitional engine's code page.
    doc["name"] = "\xE6\xBC\xA2";
    {
        std::ofstream out(file);
        out << doc;
    }
    OBL_CHECK(!loaded.Load(tmp.path, error));
    OBL_CHECK(error.find("code page") != std::string::npos);
    OBL_CHECK_EQ(std::string(loaded.params->m_commodityFamilies[0].m_name), "Caf\xE9");
    const auto indexFile = tmp.path / "catalog/index.json";
    std::ifstream indexIn(indexFile);
    auto index = nlohmann::json::parse(indexIn);
    indexIn.close();
    {
        doc["name"] = "Cafe";
        std::ofstream(file) << doc;
        auto slots = index;
        slots["desire_slots"].erase(25);
        std::ofstream(indexFile) << slots;
        OBL_CHECK(!loaded.Load(tmp.path, error));
        OBL_CHECK(error.find("desire_slots") != std::string::npos);
    }
    index["commodity_families"] = nlohmann::json::array();
    for (int i = 0; i <= WorldParams::kMaxCommodityFamilies; ++i) index["commodity_families"].push_back("family" + std::to_string(i));
    {
        std::ofstream out(indexFile);
        out << index;
    }
    OBL_CHECK(!loaded.Load(tmp.path, error));
    OBL_CHECK(error.find("index.json") != std::string::npos);
    OBL_CHECK(error.find("engine capacity") != std::string::npos);
}

OBL_TEST_CASE("convert.catalog_documents_validate")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    BuildingList list;
    std::string error;
    OBL_REQUIRE(ParseBuildingList(kList, list, error));
    std::vector<CatalogFile> files;
    OBL_REQUIRE(CatalogDocuments(list, {}, files, error));
    OBL_CHECK_EQ(files.size(), static_cast<size_t>(1 + 3 + 2 + 2 + 1 + 26 + 2 + 5 + 2 + 1 + 1 + 1 + 1 + 2));
    for (const CatalogFile& file : files) {
        std::string problems;
        const bool valid = obl::formats::ValidateDocument(schemas, file.schema, file.document, problems);
        if (!valid) std::printf("  %s: %s\n", file.path.c_str(), problems.c_str());
        OBL_CHECK(valid);
    }
    const CatalogFile* index = Find(files, "catalog/index.json");
    OBL_REQUIRE(index != nullptr);
    OBL_CHECK_EQ(index->document["buildings"].dump(), std::string(R"(["kiosk","jetty_tip"])"));
    OBL_CHECK_EQ(index->document["scenery"].dump(), std::string(R"(["rock"])"));
    OBL_REQUIRE(index->document["desire_slots"].size() == 26u);
    OBL_CHECK_EQ(index->document["desire_slots"][21].get<std::string>(), std::string("umbrellas"));
    const CatalogFile* slot = Find(files, "catalog/desire_slots/intimacy.json");
    OBL_REQUIRE(slot != nullptr);
    OBL_CHECK_EQ(slot->document["script_name"].get<std::string>(), std::string("HardSex"));
    OBL_REQUIRE(Find(files, "catalog/desire_slots/toilet.json") != nullptr);
    OBL_CHECK(!Find(files, "catalog/desire_slots/toilet.json")->document.contains("script_name"));
    const CatalogFile* kiosk = Find(files, "catalog/buildings/kiosk.json");
    OBL_REQUIRE(kiosk != nullptr);
    const auto& k = kiosk->document;
    OBL_CHECK_EQ(k["behavior"], "none");
    OBL_CHECK_EQ(Find(files, "catalog/buildings/jetty_tip.json")->document["behavior"], "jetty");
    OBL_CHECK_EQ(k["sprite"], "kiosksprite");
    OBL_CHECK_EQ(k["operation"]["opening_hours"], "late");
    OBL_CHECK_EQ(k["operation"]["attractor"], "food");
    OBL_CHECK_EQ(k["operation"]["staff"].dump(), std::string(R"({"types":["cook","cook"],"max":2})"));
    OBL_CHECK_EQ(k["commodities"][0].dump(),
            std::string(
                    R"({"commodity":"pies","intensity":[0.5,0.25],"daily_intensity_factor":1.0,"price":2.5,"daily_price_factor":1.01,"cost":0.85})"));
    OBL_CHECK_EQ(k["events"].dump(), std::string(R"(["song","song","song","song"])"));
    OBL_CHECK_EQ(k["build_on"].dump(), std::string(R"(["beach","shallow_water","deep_sea"])"));
    OBL_CHECK_EQ(k["construction"]["anim"], "kioskbuild");
    OBL_CHECK_EQ(k["tables"].dump(), std::string(R"([{"table":"desk","pos":[1.5,-2.0]}])"));
    OBL_CHECK_EQ(k["vehicle_places"].dump(), std::string(R"([{"dir":2,"offset":[-1,5]}])"));
    const CatalogFile* desk = Find(files, "catalog/tables/desk.json");
    OBL_REQUIRE(desk != nullptr);
    OBL_CHECK_EQ(desk->document["chairs"].dump(),
            std::string(
                    R"([{"pos":[1.0,2.0],"dir":"sw","walk_points":[0,1],"serve_point":[8.0,9.0]},{"pos":[3.0,4.0],"dir":"e","walk_points":[3]}])"));
    OBL_CHECK(!desk->document.contains("get_up_anim"));
    const CatalogFile* rock = Find(files, "catalog/scenery/rock.json");
    OBL_REQUIRE(rock != nullptr);
    OBL_CHECK_EQ(rock->document["build_on"].dump(), std::string(R"(["land","path"])"));
    OBL_CHECK_EQ(rock->document["anim"], "rock_still");

    // Texts are Windows-1252 and become UTF-8.
    OBL_REQUIRE(ParseBuildingList("\"COMFAM_CAFE\" \"Caf\xE9\"", list, error));
    OBL_REQUIRE(CatalogDocuments(list, {}, files, error));
    OBL_REQUIRE(Find(files, "catalog/commodity_families/cafe.json") != nullptr);
    OBL_CHECK_EQ(Find(files, "catalog/commodity_families/cafe.json")->document["name"], "Caf\xC3\xA9");

    // A building takes entry 0 of the opening hours and families it does not name; a list without them is refused.
    OBL_REQUIRE(ParseBuildingList("\"BUILD_HUT\" \"HUT_NAME\" \"Hut\"", list, error));
    OBL_CHECK(!CatalogDocuments(list, {}, files, error));
    OBL_CHECK(error.find("no opening hours or commodity family for HUT") != std::string::npos);
}

// A grid as FindFootprints returns it: four layers of 25 x 25 cells after width and height.
ExeFootprint Grid(const char* building, int width, int height)
{
    ExeFootprint grid;
    grid.building = building;
    grid.width = width;
    grid.height = height;
    grid.cells.assign(4 * 25 * 25, 0);
    return grid;
}
int32_t& Cell(ExeFootprint& grid, int layer, int row, int column)
{
    return grid.cells[static_cast<size_t>((layer * 25 + row) * 25 + column)];
}

OBL_TEST_CASE("catalog.load_footprint_grid_matches_the_cut")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    BuildingList list;
    std::string error;
    OBL_REQUIRE(ParseBuildingList(kList, list, error));
    ExeFootprint kiosk = Grid("KIOSK", 3, 2);
    Cell(kiosk, 0, 0, 4) = 0x200;
    Cell(kiosk, 1, 1, 1) = 0x10;
    Cell(kiosk, 2, 2, 3) = 7;
    Cell(kiosk, 3, 3, 4) = static_cast<int32_t>(0x80000001u);
    const obl::test::TempPath tmp("obl_catalog_footprint");
    int written = 0;
    OBL_REQUIRE(WriteCatalog(list, {kiosk}, *obl::convert::OpenDirectorySink(tmp.path), schemas, written, error));
    LoadedCatalog loaded;
    OBL_REQUIRE(loaded.Load(tmp.path, error));
    // The engine's grid: width, height, then the four 25 x 25 layers as FindFootprints read them (the cells outside the cut are 0).
    std::vector<int32_t> expected{3, 2};
    expected.insert(expected.end(), kiosk.cells.begin(), kiosk.cells.end());
    size_t id = 0;
    while (id < list.buildings.size() && list.buildings[id].key != "KIOSK") ++id;
    OBL_REQUIRE(id < list.buildings.size());
    const auto* grid = reinterpret_cast<const int32_t*>(loaded.params->m_buildings[id].m_footprint.get());
    OBL_REQUIRE(grid != nullptr);
    OBL_CHECK(std::memcmp(grid, expected.data(), expected.size() * sizeof(int32_t)) == 0);

    // A layer with a missing row is refused as a whole.
    const auto file = tmp.path / "catalog/buildings/kiosk.json";
    nlohmann::json doc;
    {
        std::ifstream in(file);
        doc = nlohmann::json::parse(in);
    }
    doc["footprint"]["layers"]["work"].erase(0);
    {
        std::ofstream out(file);
        out << doc.dump();
    }
    LoadedCatalog bad;
    OBL_CHECK(!bad.Load(tmp.path, error));
}

OBL_TEST_CASE("convert.catalog_footprints")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    BuildingList list;
    std::string error;
    OBL_REQUIRE(ParseBuildingList(kList, list, error));
    // KIOSK is 3 x 2: four rows of five cells per layer; corners of the area mark which index is the row.
    ExeFootprint kiosk = Grid("KIOSK", 3, 2);
    Cell(kiosk, 0, 0, 4) = 0x200;
    Cell(kiosk, 0, 3, 0) = 1;
    Cell(kiosk, 1, 1, 1) = 0x10;
    Cell(kiosk, 2, 2, 3) = 7;
    Cell(kiosk, 3, 3, 4) = static_cast<int32_t>(0x80000001u);
    std::vector<CatalogFile> files;
    OBL_REQUIRE(CatalogDocuments(list, {kiosk}, files, error));
    const CatalogFile* file = Find(files, "catalog/buildings/kiosk.json");
    OBL_REQUIRE(file != nullptr);
    std::string problems;
    const bool valid = obl::formats::ValidateDocument(schemas, file->schema, file->document, problems);
    if (!valid) std::printf("  %s\n", problems.c_str());
    OBL_CHECK(valid);
    const auto& footprint = file->document["footprint"];
    OBL_CHECK_EQ(footprint["size"].dump(), std::string("[3,2]"));
    OBL_CHECK_EQ(footprint["layers"]["placement"].dump(),
            std::string(R"([[{},{},{},{},{"margin":1}],[{},{},{},{},{}],[{},{},{},{},{}],[{"terrain":1},{},{},{},{}]])"));
    OBL_CHECK_EQ(footprint["layers"]["work"][1][1]["building"], 1);
    OBL_CHECK_EQ(footprint["layers"]["depth"][2][3], 7);
    OBL_CHECK_EQ(footprint["layers"]["body"][3][4], 0x80000001u);
    OBL_CHECK(!Find(files, "catalog/buildings/jetty_tip.json")->document.contains("footprint"));

    ExeFootprint outside = kiosk;
    Cell(outside, 2, 4, 0) = 1; // row h + 2
    OBL_CHECK(!CatalogDocuments(list, {outside}, files, error));
    OBL_CHECK(Contains(error, "outside the area"));
    outside = kiosk;
    Cell(outside, 3, 0, 5) = 1; // column w + 2
    OBL_CHECK(!CatalogDocuments(list, {outside}, files, error));
    OBL_CHECK(Contains(error, "outside the area"));
    OBL_CHECK(!CatalogDocuments(list, {Grid("KIOSK", 2, 3)}, files, error));
    OBL_CHECK(Contains(error, "size differs"));
    OBL_CHECK(!CatalogDocuments(list, {Grid("SHED", 1, 1)}, files, error));
    OBL_CHECK(Contains(error, "unrecognized building"));
    OBL_CHECK(!CatalogDocuments(list, {kiosk, kiosk}, files, error));
    OBL_CHECK(Contains(error, "second footprint"));
}

OBL_TEST_CASE("convert.catalog_float_numbers")
{
    OBL_CHECK_EQ(FloatNumber(0.85f).dump(), std::string("0.85"));
    OBL_CHECK_EQ(FloatNumber(1.01f).dump(), std::string("1.01"));
    OBL_CHECK_EQ(FloatNumber(-3.0f).dump(), std::string("-3.0"));
    OBL_CHECK_EQ(FloatNumber(16777216.0f).dump(), std::string("16777216.0"));
}

OBL_TEST_CASE("convert.catalog_reader_refuses")
{
    const std::string base = "\"COMFAM_NONE\" \"None\" \"HOURS_ALWAYS\" \"0\" \"0\" \"BUILD_HUT\" ";
    OBL_CHECK(Contains(ParseError("\"COMFAM_NONE\" \"None"), "odd number of quotes"));
    OBL_CHECK(Contains(ParseError(base + "\"HUT_COLOUR\" \"1\""), "unrecognized building field"));
    OBL_CHECK(Contains(ParseError(base + "\"HUT_OPENINGHOURS\" \"NEVER\""), "opening hours not found"));
    OBL_CHECK(Contains(ParseError(base + "\"HUT_BUILDON\" \"MOON\""), "invalid BUILDON"));
    OBL_CHECK(Contains(ParseError(base + "\"HUT_CATEGORY\" \"Shops\""), "unrecognized building category"));
    OBL_CHECK(Contains(ParseError(base + "\"HUT_WALKPOINTX\" \"1\""), "WALKPOINT without NUM_WALKPOINTS"));
    OBL_CHECK(Contains(ParseError(base + "\"HUT_NUM_BASEPLATES\" \"1\" \"HUT_BASEPLATETYPE\" \"1\" \"HUT_BASEPLATETYPE\" \"2\""),
            "past its capacity"));
    OBL_CHECK(Contains(ParseError(base + "\"HUT_ANIMSPRITE\" \"Smoke\" \"1\""), "line ends early"));
    OBL_CHECK(Contains(ParseError(base + "\"BUILD_HUT\""), "duplicate key"));
    OBL_CHECK(Contains(ParseError(base + "\"GUEST_LAD\""), "guest data"));
    OBL_CHECK(Contains(ParseError(base + "\"WHATEVER_X\" \"1\""), "unrecognized keyword prefix"));
    OBL_CHECK(Contains(ParseError(base + "\"NOUNDERSCORE\""), "without '_'"));
    OBL_CHECK(Contains(
            ParseError("\"STAFF_X\" \"X\" \"Q\" \"1\" \"s\" \"s\" \"m\" \"1\" \"1\" \"1\" \"1\" \"1\" \"1\""), "sex isn't M or F"));
    OBL_CHECK(Contains(
            ParseError("\"TABLE_T\" \"T_CAPACITY\" \"2\" \"T_NUM_CHAIRPOS_PTS\" \"1\" \"T_CHAIRPOS_X\" \"1\" \"T_CHAIRPOS_Y\" \"1\""),
            "chair counts disagree"));
    // Six events: the EVENT index would land past the building's count field.
    OBL_CHECK(Contains(ParseError(base + "\"EVENT_A\" \"EVENT_B\" \"EVENT_C\" \"EVENT_D\" \"EVENT_E\" \"EVENT_F\" \"HUT_EVENT\" \"A\""),
            "EVENT index past"));

    // Keys that cannot become content keys.
    BuildingList list;
    std::string error;
    OBL_REQUIRE(ParseBuildingList("\"SCENERY_Rock\" \"SCENERY_ROCK\"", list, error));
    std::vector<CatalogFile> files;
    OBL_CHECK(!CatalogDocuments(list, {}, files, error));
    OBL_CHECK(Contains(error, "differ only in case"));
    OBL_REQUIRE(ParseBuildingList("\"SCENERY_A-B\"", list, error));
    OBL_CHECK(!CatalogDocuments(list, {}, files, error));
    OBL_CHECK(Contains(error, "not a content key"));
}

OBL_TEST_CASE("convert.building_behavior_from_key")
{
    using obl::BehaviorFromKey;
    using obl::BuildingBehavior;
    OBL_CHECK(BehaviorFromKey("STAGE") == BuildingBehavior::Stage);
    OBL_CHECK(BehaviorFromKey("SECURITY_POST") == BuildingBehavior::SecurityPost);
    OBL_CHECK(BehaviorFromKey("COCKTAIL") == BuildingBehavior::CocktailBar);
    OBL_CHECK(BehaviorFromKey("BAR2") == BuildingBehavior::Bar);
    // Substring, as the engine's strstr: every jetty piece, whatever its suffix.
    OBL_CHECK(BehaviorFromKey("JETTY_END") == BuildingBehavior::Jetty);
    OBL_CHECK(BehaviorFromKey("MYJETTY") == BuildingBehavior::Jetty);
    // Exact and case-sensitive like strcmp; the engine's "BARBEQUE" matches no key, so BARBECUE keeps no special case.
    OBL_CHECK(BehaviorFromKey("stage") == BuildingBehavior::None);
    OBL_CHECK(BehaviorFromKey("STAGE2") == BuildingBehavior::None);
    OBL_CHECK(BehaviorFromKey("BARBECUE") == BuildingBehavior::None);
    OBL_CHECK(BehaviorFromKey("THESEA") == BuildingBehavior::None);
}

OBL_TEST_CASE("convert.building_behavior_schema_matches_enum")
{
    std::ifstream in(fs::path(OBL_REPO_DIR) / "data" / "schemas" / "building.schema.json");
    const auto schema = nlohmann::json::parse(in, nullptr, false);
    OBL_REQUIRE(!schema.is_discarded());
    std::vector<std::string> names;
    for (const auto& entry : obl::kBuildingBehaviorNames) {
        names.emplace_back(entry.name);
        OBL_CHECK_EQ(std::string(obl::BuildingBehaviorName(entry.behavior)), std::string(entry.name));
    }
    OBL_CHECK(schema["properties"]["behavior"]["enum"].get<std::vector<std::string>>() == names);
}

OBL_TEST_CASE("data.convert_catalog_matches_engine")
{
    const fs::path game(OBL_GAME_DIR);
    const fs::path file = game / "Data" / "Script" / "BuildingList.ble";
    if (!fs::exists(file)) OBL_SKIP("no run/Data/Script/BuildingList.ble");
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (char& c : text) c = static_cast<char>(~static_cast<unsigned char>(c));
    CompareWithEngine(text);

    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    const obl::test::TempPath tmp("obl_convert_catalog_data");
    const fs::path& out = tmp.path;
    int written = 0;
    std::string error;
    const bool ok =
            ConvertCatalog(*obl::convert::OpenDirectorySource(game), *obl::convert::OpenDirectorySink(out), schemas, written, error);
    if (!ok) std::printf("  %s\n", error.c_str());
    OBL_CHECK(ok);
    OBL_CHECK(fs::exists(out / "catalog" / "index.json"));
    OBL_CHECK(written > 1);
    CheckCatalogPack(out, text);
    int withFootprint = 0;
    for (const auto& entry : fs::directory_iterator(out / "catalog" / "buildings")) {
        std::ifstream json(entry.path());
        if (nlohmann::json::parse(json, nullptr, false).contains("footprint")) ++withFootprint;
    }
    OBL_CHECK_EQ(withFootprint, 48);
    // The engine takes the grids from the pack: exactly the buildings whose file has a footprint get a grid.
    BuildingList list;
    OBL_REQUIRE(ParseBuildingList(text, list, error));
    LoadedCatalog loadedFootprints;
    OBL_REQUIRE(loadedFootprints.Load(out, error));
    OBL_REQUIRE(loadedFootprints.params->m_buildings.size() >= list.buildings.size());
    int gridCount = 0;
    for (size_t id = 0; id < list.buildings.size(); ++id) {
        std::string key = list.buildings[id].key;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::ifstream json(out / "catalog" / "buildings" / (key + ".json"));
        const bool hasFootprint = nlohmann::json::parse(json, nullptr, false).contains("footprint");
        const bool hasGrid = loadedFootprints.params->m_buildings[id].m_footprint != nullptr;
        if (hasFootprint != hasGrid) std::printf("  %s: footprint %d, grid %d\n", key.c_str(), hasFootprint, hasGrid);
        OBL_CHECK(hasFootprint == hasGrid);
        gridCount += hasGrid;
    }
    OBL_CHECK_EQ(gridCount, withFootprint);
    const auto behavior = [&](const char* key) {
        std::ifstream json(out / "catalog" / "buildings" / (std::string(key) + ".json"));
        return nlohmann::json::parse(json, nullptr, false).value("behavior", std::string());
    };
    OBL_CHECK_EQ(behavior("stage"), "stage");
    OBL_CHECK_EQ(behavior("jetty_front"), "jetty");
    OBL_CHECK_EQ(behavior("jetty_mid"), "jetty");
    OBL_CHECK_EQ(behavior("jetty_end"), "jetty");
    OBL_CHECK_EQ(behavior("security_post"), "security_post");
    OBL_CHECK_EQ(behavior("loo"), "toilet");
    OBL_CHECK_EQ(behavior("icestall"), "ice_stall");
    OBL_CHECK_EQ(behavior("barbecue"), "none"); // the engine names "BARBEQUE"
    OBL_CHECK_EQ(behavior("thesea"), "none");
}

OBL_TEST_CASE("convert.embedded_schemas_match_checked_in_documents")
{
    obl::json_schema::Registry embedded;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(embedded, error));
    OBL_CHECK(embedded.CheckRefs().empty());
    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(fs::path(OBL_REPO_DIR) / "data" / "schemas")) {
        if (!entry.path().filename().string().ends_with(".schema.json")) continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const auto expected = nlohmann::json::parse(in);
        const auto* actual = embedded.Find(expected.at("$id").get<std::string>());
        OBL_REQUIRE(actual);
        OBL_CHECK(*actual == expected);
        ++count;
    }
    OBL_CHECK(count > 0);
    const nlohmann::ordered_json invalid = {{"format", "obl.sprite"}};
    OBL_CHECK(!obl::formats::ValidateDocument(embedded, "obl.sprite", invalid, error));
}
