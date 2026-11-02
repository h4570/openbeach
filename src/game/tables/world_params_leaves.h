#pragma once

#include "game/tables/building_behavior.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace obl::tables {

// File/catalog order defines IDs and desire-family priority.
struct CommodityFamilyDef {
    bool operator==(const CommodityFamilyDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_name;
};

struct CommodityDef {
    bool operator==(const CommodityDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_name;
    int m_family{};
};

// One of the guest desire slots; the slot id indexes GuestDef::m_Max and the guest and weather desire arrays.
struct DesireSlotDef {
    bool operator==(const DesireSlotDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_scriptName; // the world scripts' Weather modifier word; empty when the slot has none
};

struct DesireDef {
    bool operator==(const DesireDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_name;
    int m_numFamilies{}; // retained for the reader contract; matches the vector size
    std::vector<int> m_families;
};

// Definition IDs are indices in catalog order so references stay stable across loads.
struct OpeningHoursDef {
    bool operator==(const OpeningHoursDef&) const = default;
    int m_id{};
    std::string m_key;
    float m_open{}; // hour of day; HOURS_ALLHOURS = 0 0
    float m_close{};
};

struct EventDef {
    bool operator==(const EventDef&) const = default;
    int m_id{};
    std::string m_key;
    uint8_t m_available{};
    float m_price{}; // initial event slot price; live entertainment applies the slot's price multiplier
    float m_cost{};
    float m_intensity[2]{}; // male/female show intensity, scaled by BuildingEvents
};

struct BeerDef {
    bool operator==(const BeerDef&) const = default;
    int m_id{};
    std::string m_key;
    char m_Brand[0x40]{}; // BRAND
    char m_Cocktail[0x40]{}; // COCKTAIL (read by the GUI list of kind 0xc)
    float m_BaseCost{}; // BASECOST
    float m_StrCost{}; // STRCOST
    float m_Price{}; // PRICE: the initial slot price of a building selling it (the building's constructor)
    float m_AlcStr{}; // ALCSTR
    int m_Effect{}; // EFFECT: FIGHT 0, FLIRT 1, VANDAL 2, HAPPY 3, SAD 4
    uint8_t m_scriptFlag{}; // The beer line sets 1 on every call
};

struct FineDef {
    bool operator==(const FineDef&) const = default;
    int m_id{};
    std::string m_key;
    float m_cost{};
};

// STAFF_<key>: a staff type (the staff reader). Its text fields are strings; m_female keeps the int of the reader contract.
struct StaffDef {
    bool operator==(const StaffDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_Name; // display name
    int m_female{}; // `F` = 1, `M` = 0
    int m_Wage{}; // initial staff wage
    std::string m_Sprite;
    std::string m_ObjectType; // the type string of the staff object (script name)
    std::string m_Menu;
    int m_RecommendedWage{}; // base salary expectation; the severance of Staff::Sack is ten times this
    float m_scale{};
    float m_expectationGrowth{}; // daily growth factor of the salary expectation
    float m_initialHappiness{};
    int m_StartHour{};
    int m_EndHour{};
};

// Sizes of the engine's index-ordered arrays: the thirteen preferences, the seven opinions and the 26 MAX_<desire> counts.
inline constexpr size_t kGuestPreferences = 13, kGuestOpinions = 7, kGuestDesires = 26;

// GUEST_<key>: a guest type. Its text fields are strings; the two-value arrays are vectors in the engine's index order
// (world_params_tables.h), sized here so a fresh record reads as zeros. The sleep and wake times are fixed pairs.
struct GuestDef {
    bool operator==(const GuestDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_Name;
    std::string m_Sprite;
    int m_Gender{}; // 0 = M, 1 = F
    float m_Money{};
    float m_SwimEnergy{};
    float m_SwimDist{};
    float m_Attractive{};
    int m_DefSprite{};
    uint8_t m_DrunkType{}; // TIRED/HORNY/FIGHT/VANDALISE = 1..4
    float m_AttractivePref{};
    uint8_t m_Colour{};
    uint8_t m_NightsMin{};
    uint8_t m_NightsMax{};
    float m_Litter{};
    float m_DrunkDecay{};
    float m_FightDecay{};
    float m_FlirtDecay{};
    float m_VandaliseDecay{};
    // ALCOHOL, AMUSEMENT, MEALS, PARTYFUN, SNACKS, SOFTDRINKS, SOUVENIRS, SPORTSFUN, SUNBATHE, SWIMMING,
    // WATERFUN, STROLL, SLEEP
    std::vector<std::array<float, 2>> m_Pref = std::vector<std::array<float, 2>>(kGuestPreferences);
    std::vector<std::array<float, 2>> m_OpinionStart = std::vector<std::array<float, 2>>(kGuestOpinions);
    std::vector<std::array<float, 2>> m_OpinionEffect = std::vector<std::array<float, 2>>(kGuestOpinions);
    std::vector<std::array<float, 2>> m_OpinionTol = std::vector<std::array<float, 2>>(kGuestOpinions);
    std::array<float, 2> m_SleepTime{};
    std::array<float, 2> m_WakeTime{};
    std::vector<int> m_Max = std::vector<int>(kGuestDesires); // MAX_<desire>, indexed like Guest desires
    float m_SunburnTime{};
    float m_IllDecay{};
    float m_StartBladder{};
    float m_StartFight{};
    float m_StartFlirt{};
    float m_StartDrunk{};
    float m_StartIll{}; // -> FoodIll
    float m_StartSunburn{}; // -> SunBurnLevel
    float m_StartVandalise{};
};

// SCENERY_<key>: bins, benches, lamps, plants... Its text fields are strings; the flags keep the byte type of the reader contract.
struct SceneryDef {
    bool operator==(const SceneryDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_Name;
    std::string m_Sprite;
    std::string m_Anim;
    int m_Category{}; // FURNITURE, PATHS, PLANTS, ORNAMENTS, TREES, SHRUBS, BILLBOARD, OTHER = 0..7
    int m_GridWidth{};
    int m_GridHeight{};
    int m_Cost2Build{};
    uint32_t m_BuildOn{}; // LAND 1, BEACH 2, SHALLOW_WATER 4, DEEP_SEA 8, PATH 0x80
    uint8_t m_collide{};
    int m_Capacity{}; // users at once (bench seats)
    std::string m_Menu;
    int m_Icon{};
    float m_Rating{};
    uint8_t m_clean{};
    uint8_t m_repair{};
    uint8_t m_water{};
    int m_PathFlag{};
    uint8_t m_burn{};
    uint8_t m_userRemove{};
    uint8_t m_vegFlag{};
};

// VEHICLE_<key>. The staff list is a vector; m_NumStaff stays the reader contract and matches its size.
struct VehicleDef {
    bool operator==(const VehicleDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_Name;
    std::string m_Sprite;
    int m_GridWidth{};
    int m_GridHeight{};
    int m_Capacity{};
    float m_Force{};
    float m_MaxSpeed{};
    float m_Mass{};
    uint8_t m_MoveType{}; // MOVETYPE: POINTS 1, BOUNDARY 2
    int m_NumStaff{};
    std::string m_Towed;
    std::string m_AnimSet;
    std::vector<int> m_Staff; // StaffDef ids
    float m_CreationPoint[2]{}; // CREATION_PNTX/Y
};

// TABLE_<key>: a table with its chairs, walk points and waiter. The NUM_*, CAPACITY and CHAIR_NUM_WALKPNT lines size the vectors;
// the lines after them fill the vectors with a running count (the Y line of a pair advances it). The *Num* counts are the reader
// contract and equal the number of fills; the per-chair vectors have m_NumChairs entries.
struct TableDef {
    bool operator==(const TableDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_Name;
    std::string m_TableSprite;
    std::string m_ChairSprite;
    std::string m_SitDownAnim;
    std::vector<std::string> m_satDownAnims; // SAT_DOWN_ANIM, in file order
    std::string m_GetUpAnim;
    int m_GridWidth{};
    int m_GridHeight{};
    uint32_t m_NumChairs{}; // CAPACITY
    int m_NumSatDownAnims{};
    int m_NumServePoints{}; // WAITER_SERVE_POINT_Y advances it
    int m_NumWalkPts{}; // WALK_PNT_Y advances it
    std::vector<std::array<float, 2>> m_walkPoints;
    int m_NumChairPos{}; // CHAIRPOS_Y advances it
    std::vector<std::array<float, 2>> m_chairPositions;
    int m_NumChairDirs{}; // CHAIR_DIRECTION: N 0, NE 1, E 2, SE 3, S 4, SW 5, W 6, NW 7
    std::vector<int> m_chairDirections;
    // Each list is [count, walk point ids...]: the count is also its first element (CHAIR_NUM_WALKPNT); an empty list has no lines.
    std::vector<std::vector<int>> m_chairWalkPoints;
    int m_ChairWalkIdx{}; // the fill position in the last list
    int m_NumChairWalkLists{};
    uint8_t m_hasPlates{}; // HAS_WAITER
    std::string m_callWaiterAnim;
    std::string m_waitWaiterAnim;
    std::string m_waiterWalkAnim;
    std::string m_waiterServeAnim;
    std::string m_waiterSprite;
    std::vector<std::array<float, 2>> m_servePoints; // empty without serve points, else m_NumChairs entries
    int m_XOffsetTable{};
    int m_YOffsetTable{};
    uint8_t m_serveTable{}; // SERVE_TABLE
};

// One ANIMSPRITE line of a building: sprite name, eight coordinates and a flag. The building reader stores the
// coordinates in file order x0 y0 x1 y1 x2 y2 x3 y3 as m_X[0..3] and m_Y[0..3] (the meaning of the quad is not settled).
struct AnimSpriteDef {
    bool operator==(const AnimSpriteDef&) const = default;
    std::string m_Sprite;
    int m_X[4]{};
    int m_Y[4]{};
    uint8_t m_animated{}; // 1 when the ninth number is not 0
};

// The engine's slot counts of one building: commodities, vehicle places, events, tables and ANIMSPRITE entries. The vectors start at
// these sizes, and the converter and the catalog loader refuse more.
inline constexpr int kBuildingCommoditySlots = 5, kBuildingVehiclePlaces = 5, kBuildingEventSlots = 5, kBuildingTableSlots = 10,
                     kBuildingAnimSprites = 8;

// A building's placement grid (catalog `footprint`): width, height, then four 25 x 25 layers (placement, work, depth, body). The
// engine reads only the (height + 2) x (width + 2) area of each layer; the rest stays zero.
inline constexpr int kFootprintCells = 25;
struct BuildingFootprint {
    bool operator==(const BuildingFootprint&) const = default;
    int m_width{};
    int m_height{};
    int32_t m_layers[4][kFootprintCells][kFootprintCells]{};
};

// BUILD_<key> - building type. The fields are the reader contract of the building parser and the catalog loader.
struct BuildingDef {
    bool operator==(const BuildingDef&) const = default;
    int m_id{};
    std::string m_key;
    std::string m_Name;
    std::string m_Sprite;
    int m_Icon{};
    int m_Category{}; // CATEGORY: 0 Hotel, 1 Food, 2 Leisure, 3 Other; Other does not count toward unbuilt scenario buildings
    int m_Cost2Build{};
    int m_RefundValue{};
    int m_GridWidth{};
    int m_GridHeight{};
    int m_GuestCapacity{};
    int m_BuildTime{};
    int m_DemolishTime{};
    int m_OpeningHours{}; // OpeningHoursDef id
    int m_MaxInstances{};
    int m_Attractor{}; // CommodityFamilyDef id, 0 when absent
    int m_NumCommodities{}; // COMMODITY lines, at most 5
    int m_MaxQueue{}; // 0 = served without a queue object
    // COMMODITY "com" a b c d e f supplies six values per slot: male and female intensity, daily intensity growth, initial price,
    // daily price growth and cost per serving. New-day messages multiply both intensities and the slot's price factor by their
    // respective growth values. Reference prices combine the initial price with that factor.
    // The per-slot arrays are vectors of the engine's counts (five commodity slots, five vehicle places, five events, ten tables).
    std::vector<int> m_Commodity = std::vector<int>(kBuildingCommoditySlots); // CommodityDef ids
    std::vector<std::array<float, 2>> m_CommodityAB = std::vector<std::array<float, 2>>(kBuildingCommoditySlots);
    std::vector<float> m_CommodityC = std::vector<float>(kBuildingCommoditySlots);
    std::vector<float> m_CommodityCost = std::vector<float>(kBuildingCommoditySlots);
    std::vector<float> m_CommodityE = std::vector<float>(kBuildingCommoditySlots);
    std::vector<float> m_CommodityF = std::vector<float>(kBuildingCommoditySlots);
    int m_MaxStaff{};
    int m_NumStaff{}; // STAFF lines (the building's initial staff spawn)
    int m_Staff[10]{}; // StaffDef ids
    int m_NumVehicles{};
    std::vector<int> m_Vehicle = std::vector<int>(kBuildingVehiclePlaces); // VehicleDef ids
    int m_NumVehiclePlaces{}; // VEHICLE_DIR and VEHICLE_GRID_XOFFSET write place n, VEHICLE_GRID_YOFFSET advances it
    std::vector<std::array<int, 2>> m_VehicleGridOffset = std::vector<std::array<int, 2>>(kBuildingVehiclePlaces);
    std::vector<int> m_VehicleDir = std::vector<int>(kBuildingVehiclePlaces);
    // The catalog's `behavior` (building_behavior.h): the engine's dispatch value, read from the building's key by the converter.
    BuildingBehavior m_Behavior{};
    int16_t m_ServeOffset[2]{}; // X/YSERVEOFFSET
    int16_t m_StaffOffset[2]{}; // STAFF_X/YOFFSET
    uint32_t m_BuildOn{}; // BUILDON mask: LAND 1, BEACH 2, SHALLOW_WATER 4, DEEP_SEA 8, WATER 0xc
    std::string m_Tree; // preceding building in the build tree, "NONE" when free
    std::string m_Menu; // GUI dialog
    int m_NumAnimSprites{}; // at most kBuildingAnimSprites (the parser's limit)
    std::vector<AnimSpriteDef> m_AnimSprites = std::vector<AnimSpriteDef>(kBuildingAnimSprites); // the catalog loader sets its count
    int16_t m_EntranceOffset[2]{}; // X/YENTRANCEOFFSET
    // Bug: EVENT writes its id at index WorldParams::m_numEvents, i.e. into m_NumEvents with the
    // five events of the data, then increments it - STAGE ends with m_NumEvents = id + 1 and m_Event all zero. The parser
    // writes that aliasing out (world_params_add.h); the list holds the five slots only. With six events the id lands past
    // m_NumEvents and only the count grows.
    std::vector<int> m_Event = std::vector<int>(kBuildingEventSlots); // EventDef ids
    int m_NumEvents{};
    uint8_t m_customConstruct{}; // CONSTRUCT given
    std::string m_Construct;
    int m_Power{}; // negative = generator output
    float m_AvFailTime{}; // hours between breakdown rolls
    int m_FailPercent{};
    int m_FailAge{};
    std::string m_MaleFileName;
    std::string m_FemaleFileName;
    std::vector<std::array<float, 2>> m_walkPoints; // NUM_WALKPOINTS pairs
    int m_NumWalkPoints{}; // WALKPOINTY advances it
    int m_NumBasePlates{};
    std::vector<int> m_basePlateTypes; // BASEPLATETYPE values, NUM_BASEPLATES long
    float m_MaxBuilders{};
    int m_Table[10]{}; // TableDef ids
    std::vector<std::array<float, 2>> m_TablePos = std::vector<std::array<float, 2>>(kBuildingTableSlots);
    int m_NumTables{}; // TABLE and TABLE_POSX write table n, TABLE_POSY advances it
    // The catalog loader's footprint grid, null without one. Shared and immutable, so copies of a record read the same grid.
    std::shared_ptr<const BuildingFootprint> m_footprint;
    int m_DefaultBeer{}; // BeerDef id; reset by every BUILD_ line, set by the scenario's DefaultBeer<X>
};

} // namespace obl::tables
