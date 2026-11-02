#pragma once

#include "game/tables/world_params_tables.h"
#include "game/world/world_params_queries.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace obl::world_params_add {

using tables::WorldParams;
namespace queries = world_params_queries;

struct Env {
    // The game's FatalError(format, argument); the argument is the one %s of the message (nullptr when there is none).
    void (*fatal)(const char* format, const char* argument);
    // The game's operator new.
    void* (*allocate)(size_t bytes);
};

namespace detail {

inline bool Is(const char* text, const char* word)
{
    return std::strcmp(text, word) == 0;
}
inline void Copy(char* destination, const char* source)
{
    std::memcpy(destination, source, std::strlen(source) + 1);
}
inline void Copy(std::string& destination, const char* source)
{
    destination = source;
}
inline int ToInt(const char* text)
{
    return std::atoi(text);
}
inline float ToFloat(const char* text)
{
    return static_cast<float>(std::atof(text));
}
inline int16_t ToShort(const char* text)
{
    return static_cast<int16_t>(std::atoi(text));
}

// Table record `index` without the bounds check of operator[] (`this + base + index * size`).
template<class Def> Def& Record(Def* table, int index)
{
    return *reinterpret_cast<Def*>(reinterpret_cast<uintptr_t>(table) +
            static_cast<uintptr_t>(static_cast<ptrdiff_t>(index) * static_cast<ptrdiff_t>(sizeof(Def))));
}

template<class Def> Def& Record(std::vector<Def>& table, int index)
{
    if (static_cast<size_t>(index) >= table.size()) table.resize(static_cast<size_t>(index) + 1);
    return table[index];
}

template<class T> void Store(T* base, int index, T value)
{
    std::memcpy(reinterpret_cast<uint8_t*>(base) + static_cast<ptrdiff_t>(index) * static_cast<ptrdiff_t>(sizeof(T)), &value, sizeof(T));
}

// OPENINGHOURS lookup, used only by AddBuildingData.
inline int FindOpeningHoursId(const WorldParams& p, const char* key)
{
    return queries::FindIdByKey(p.m_openingHours, p.m_numOpeningHours, key);
}

// Start of the next quoted token: the character after the next '"' at or after `from`.
inline char* NextToken(char* from)
{
    while (*from != '"') ++from;
    return from + 1;
}

inline size_t QuotedValuesExtent(const char* value, int tokens, size_t limit = 4096)
{
    const char* p = value;
    for (int i = 0; i < tokens; ++i) {
        while (static_cast<size_t>(p - value) < limit && *p != '"') ++p;
        if (static_cast<size_t>(p - value) >= limit) return 0;
        ++p;
    }
    const size_t end = static_cast<size_t>(p - value) + std::strlen(p) + 1;
    return end <= limit ? end : 0;
}

// A guest keyword that names one entry of a pair list (the preferences, the tolerances, the effects and the starts) or of MAX_<desire>.
// The two-value lists are vectors of pairs; the index is the engine's index order.
using PairList = std::vector<std::array<float, 2>>;
struct GuestPairField {
    const char* keyword;
    PairList tables::GuestDef::* list;
    int index;
};
inline constexpr GuestPairField kGuestPairs[] = {
        {"ALCOHOLPREF", &tables::GuestDef::m_Pref, 0},
        {"AMUSEMENTPREF", &tables::GuestDef::m_Pref, 1},
        {"MEALSPREF", &tables::GuestDef::m_Pref, 2},
        {"PARTYFUNPREF", &tables::GuestDef::m_Pref, 3},
        {"SNACKSPREF", &tables::GuestDef::m_Pref, 4},
        {"SOFTDRINKSPREF", &tables::GuestDef::m_Pref, 5},
        {"SOUVENIRSPREF", &tables::GuestDef::m_Pref, 6},
        {"SPORTSFUNPREF", &tables::GuestDef::m_Pref, 7},
        {"SUNBATHEPREF", &tables::GuestDef::m_Pref, 8},
        {"SWIMMINGPREF", &tables::GuestDef::m_Pref, 9},
        {"WATERFUNPREF", &tables::GuestDef::m_Pref, 10},
        {"STROLLPREF", &tables::GuestDef::m_Pref, 11},
        {"SLEEPPREF", &tables::GuestDef::m_Pref, 12},
        {"BEAUTYTOL", &tables::GuestDef::m_OpinionTol, 0},
        {"CROWDINGTOL", &tables::GuestDef::m_OpinionTol, 1},
        {"TOTTYTOL", &tables::GuestDef::m_OpinionTol, 2},
        {"PRICETOL", &tables::GuestDef::m_OpinionTol, 3},
        {"VARIETYTOL", &tables::GuestDef::m_OpinionTol, 4},
        {"CLEANLINESSTOL", &tables::GuestDef::m_OpinionTol, 5},
        {"SAFETYTOL", &tables::GuestDef::m_OpinionTol, 6},
        {"BEAUTY_EFFECT", &tables::GuestDef::m_OpinionEffect, 0},
        {"CROWDING_EFFECT", &tables::GuestDef::m_OpinionEffect, 1},
        {"TOTTY_EFFECT", &tables::GuestDef::m_OpinionEffect, 2},
        {"PRICE_EFFECT", &tables::GuestDef::m_OpinionEffect, 3},
        {"VARIETY_EFFECT", &tables::GuestDef::m_OpinionEffect, 4},
        {"CLEANLINESS_EFFECT", &tables::GuestDef::m_OpinionEffect, 5},
        {"SAFETY_EFFECT", &tables::GuestDef::m_OpinionEffect, 6},
        {"BEAUTY_START", &tables::GuestDef::m_OpinionStart, 0},
        {"CROWDING_START", &tables::GuestDef::m_OpinionStart, 1},
        {"TOTTY_START", &tables::GuestDef::m_OpinionStart, 2},
        {"PRICE_START", &tables::GuestDef::m_OpinionStart, 3},
        {"VARIETY_START", &tables::GuestDef::m_OpinionStart, 4},
        {"CLEANLINESS_START", &tables::GuestDef::m_OpinionStart, 5},
        {"SAFETY_START", &tables::GuestDef::m_OpinionStart, 6},
};

// MAX_<desire> keywords, in the order of GuestDef::m_Max (the GuestObject desire order).
inline constexpr const char* kGuestMaxKeywords[] = {"MAX_ACCOMODATION", "MAX_ALCOHOL", "MAX_AMUSEMENT", "MAX_FIRSTAID", "MAX_HEADACHE",
        "MAX_MEALS", "MAX_PARTYFUN", "MAX_SHOWER", "MAX_SNACKS", "MAX_SOFTDRINKS", "MAX_SOUVENIRS", "MAX_SPORTSFUN", "MAX_SUNBATHE",
        "MAX_SWIMFUN", "MAX_TOILET", "MAX_WATERSPORTSFUN", "MAX_STROLL", "MAX_LEAVE", "MAX_VANDALISE", "MAX_FIGHT", "MAX_FLIRT",
        "MAX_UMBRELLAS", "MAX_SUNTAN", "MAX_HARDSEX", "MAX_SLEEP", "MAX_SCARED"};
static_assert(std::size(kGuestMaxKeywords) == tables::kGuestDesires);

// The scalar keywords AddGuestData handles (the MAX_<desire> ones are in kGuestMaxKeywords); the process uses them to classify lines.
inline constexpr const char* kGuestDataKeywords[] = {"NAME", "SPRITE", "DRUNKTYPE", "MONEY", "LITTER", "DEF_SPRITE", "ATTRACTIVE",
        "ATTRACTIVE_PREF", "SWIM_ENERGY", "SWIM_DIST", "COLOUR", "DRUNKDECAY", "FLIRTDECAY", "FIGHTDECAY", "VANDALISEDECAY", "NIGHTS_MIN",
        "NIGHTS_MAX", "SUNBURN_TIME", "ILL_DECAY", "START_BLADDER", "START_FIGHT", "START_FLIRT", "START_ILL", "START_DRUNK",
        "START_SUNBURN", "START_VANDALISE"};

} // namespace detail

// Appends the staff type at m_numStaff.
inline void AddStaff(WorldParams& p, const Env& env, const char* key, const char* name, const char* sex, const char* wage,
        const char* sprite, const char* objectType, const char* menu, const char* recommendedWage, const char* scale,
        const char* expectationGrowth, const char* initialHappiness, const char* startHour, const char* endHour)
{
    using namespace detail;
    tables::StaffDef& d = Record(p.m_staff, p.m_numStaff);
    d.m_id = p.m_numStaff;
    Copy(d.m_key, key);
    Copy(d.m_Name, name);
    if (sex[0] == 'F')
        d.m_female = 1;
    else if (sex[0] == 'M')
        d.m_female = 0;
    else {
        env.fatal("Staff gender must be M or F, got \"%s\"", key);
        return;
    }
    d.m_Wage = ToInt(wage);
    Copy(d.m_Sprite, sprite);
    Copy(d.m_ObjectType, objectType);
    Copy(d.m_Menu, menu);
    d.m_RecommendedWage = ToInt(recommendedWage);
    d.m_scale = ToFloat(scale);
    d.m_expectationGrowth = ToFloat(expectationGrowth);
    d.m_initialHappiness = ToFloat(initialHappiness);
    d.m_StartHour = ToInt(startHour);
    d.m_EndHour = ToInt(endHour);
    ++p.m_numStaff;
}

// Appends a text key to the male, female or surname pool.
inline void AddNameData(WorldParams& p, const Env& env, const char* keyword, const char* textKey)
{
    using namespace detail;
    // Pools share one array: male names from 0, female names from 0x8c, surnames from 0x118 (WorldParams::m_nameCounts).
    int index;
    int* count;
    if (Is(keyword, "MALENAME")) {
        count = &p.m_nameCounts[0];
        index = *count;
    } else if (Is(keyword, "FEMALENAME")) {
        count = &p.m_nameCounts[1];
        index = 0x8c + *count;
    } else if (Is(keyword, "SURNAME")) {
        count = &p.m_nameCounts[2];
        index = 0x118 + *count;
    } else {
        env.fatal("Unrecognized name list field \"%s\" in the gameplay data", keyword);
        return;
    }
    Copy(reinterpret_cast<char*>(&Record(p.m_names, index)), textKey);
    ++*count;
}

// Stores one keyword of event `index`.
inline void AddEventData(WorldParams& p, const Env& env, int index, const char* keyword, const char* value)
{
    using namespace detail;
    tables::EventDef& d = Record(p.m_events, index);
    if (Is(keyword, "COST"))
        d.m_cost = ToFloat(value);
    else if (Is(keyword, "DEF_PRICE"))
        d.m_price = ToFloat(value);
    else if (Is(keyword, "MALE_INTENSITY"))
        d.m_intensity[0] = ToFloat(value);
    else if (Is(keyword, "FEMALE_INTENSITY"))
        d.m_intensity[1] = ToFloat(value);
    else
        env.fatal("Unrecognized event field \"%s\" in the gameplay data", keyword);
}

// Stores one keyword of beer `index`.
inline void AddBeerData(WorldParams& p, const Env& env, int index, const char* keyword, const char* value)
{
    using namespace detail;
    tables::BeerDef& d = Record(p.m_beers, index);
    d.m_scriptFlag = 1;
    if (Is(keyword, "BRAND"))
        Copy(d.m_Brand, value);
    else if (Is(keyword, "BASECOST"))
        d.m_BaseCost = ToFloat(value);
    else if (Is(keyword, "COCKTAIL"))
        Copy(d.m_Cocktail, value);
    else if (Is(keyword, "STRCOST"))
        d.m_StrCost = ToFloat(value);
    else if (Is(keyword, "ALCSTR"))
        d.m_AlcStr = ToFloat(value);
    else if (Is(keyword, "PRICE"))
        d.m_Price = ToFloat(value);
    else if (Is(keyword, "EFFECT")) {
        if (Is(value, "FIGHT"))
            d.m_Effect = 0;
        else if (Is(value, "FLIRT"))
            d.m_Effect = 1;
        else if (Is(value, "VANDAL"))
            d.m_Effect = 2;
        else if (Is(value, "HAPPY"))
            d.m_Effect = 3;
        else if (Is(value, "SAD"))
            d.m_Effect = 4;
        else
            env.fatal("Unrecognized beer effect \"%s\" in the gameplay data", value);
    } else
        env.fatal("Unrecognized event field \"%s\" in the gameplay data", keyword);
}

// Stores one keyword of vehicle `index`.
inline void AddVehicleData(WorldParams& p, const Env& env, int index, const char* keyword, const char* value)
{
    using namespace detail;
    tables::VehicleDef& d = Record(p.m_vehicles, index);
    if (Is(keyword, "NAME"))
        Copy(d.m_Name, value);
    else if (Is(keyword, "SPRITE"))
        Copy(d.m_Sprite, value);
    else if (Is(keyword, "GRIDWIDTH"))
        d.m_GridWidth = ToInt(value);
    else if (Is(keyword, "GRIDHEIGHT"))
        d.m_GridHeight = ToInt(value);
    else if (Is(keyword, "CAPACITY"))
        d.m_Capacity = ToInt(value);
    else if (Is(keyword, "TERRAIN")) {
    } else if (Is(keyword, "MAX_SPEED"))
        d.m_MaxSpeed = ToFloat(value);
    else if (Is(keyword, "TOWED"))
        Copy(d.m_Towed, value);
    else if (Is(keyword, "FORCE"))
        d.m_Force = ToFloat(value);
    else if (Is(keyword, "MASS"))
        d.m_Mass = ToFloat(value);
    else if (Is(keyword, "MOVETYPE")) {
        if (Is(value, "POINTS"))
            d.m_MoveType = 1;
        else if (Is(value, "BOUNDARY"))
            d.m_MoveType = 2;
        else
            env.fatal("Unrecognized vehicle movement \"%s\" in the gameplay data", value);
    } else if (Is(keyword, "ANIMSET"))
        Copy(d.m_AnimSet, value);
    else if (Is(keyword, "STAFF")) {
        const int id = queries::FindStaffId(p, value);
        if (id == queries::kNotFound) {
            env.fatal("Unrecognized staff type \"%s\" in the gameplay data", value);
            return;
        }
        d.m_Staff.push_back(id);
        ++d.m_NumStaff;
    } else if (Is(keyword, "CREATION_PNTX"))
        d.m_CreationPoint[0] = ToFloat(value);
    else if (Is(keyword, "CREATION_PNTY"))
        d.m_CreationPoint[1] = ToFloat(value);
    else
        env.fatal("Unrecognized vehicle field \"%s\" in the gameplay data", keyword);
}

// Stores one keyword of scenery `index`. An unrecognized keyword is ignored.
inline void AddSceneryData(WorldParams& p, const Env& env, int index, const char* keyword, const char* value)
{
    using namespace detail;
    tables::SceneryDef& d = Record(p.m_scenery, index);
    if (Is(keyword, "NAME"))
        Copy(d.m_Name, value);
    else if (Is(keyword, "SPRITE"))
        Copy(d.m_Sprite, value);
    else if (Is(keyword, "ANIM"))
        Copy(d.m_Anim, value);
    else if (Is(keyword, "GRIDWIDTH"))
        d.m_GridWidth = ToInt(value);
    else if (Is(keyword, "GRIDHEIGHT"))
        d.m_GridHeight = ToInt(value);
    else if (Is(keyword, "COST2BUILD"))
        d.m_Cost2Build = ToInt(value);
    else if (Is(keyword, "BUILDON")) {
        // An unrecognized value is ignored (the building version of the keyword is a FatalError).
        if (Is(value, "SHALLOW_WATER"))
            d.m_BuildOn |= 4;
        else if (Is(value, "LAND"))
            d.m_BuildOn |= 1;
        else if (Is(value, "DEEP_SEA"))
            d.m_BuildOn |= 8;
        else if (Is(value, "BEACH"))
            d.m_BuildOn |= 2;
        else if (Is(value, "PATH"))
            d.m_BuildOn |= 0x80;
    } else if (Is(keyword, "COLLIDE"))
        d.m_collide = ToInt(value) != 0;
    else if (Is(keyword, "CAPACITY"))
        d.m_Capacity = ToInt(value);
    else if (Is(keyword, "MENU"))
        Copy(d.m_Menu, value);
    else if (Is(keyword, "ICON"))
        d.m_Icon = ToInt(value);
    else if (Is(keyword, "CATEGORY")) {
        static const char* const kCategories[] = {"FURNITURE", "PATHS", "PLANTS", "ORNAMENTS", "TREES", "SHRUBS", "BILLBOARD", "OTHER"};
        for (int i = 0; i < 8; ++i)
            if (Is(value, kCategories[i])) {
                d.m_Category = i;
                return;
            }
        env.fatal("Unrecognized scenery category", nullptr);
    } else if (Is(keyword, "RATING"))
        d.m_Rating = ToFloat(value);
    else if (Is(keyword, "CLEAN"))
        d.m_clean = ToInt(value) != 0;
    else if (Is(keyword, "REPAIR"))
        d.m_repair = ToInt(value) != 0;
    else if (Is(keyword, "BURN"))
        d.m_burn = ToInt(value) != 0;
    else if (Is(keyword, "USERREMOVE"))
        d.m_userRemove = ToInt(value) != 0;
    else if (Is(keyword, "VEGFLAG"))
        d.m_vegFlag = ToInt(value) != 0;
    else if (Is(keyword, "PATHFLAG"))
        d.m_PathFlag = ToInt(value);
    else if (Is(keyword, "WATER"))
        d.m_water = ToInt(value) != 0;
}

// Stores one keyword of guest type `index`. An unrecognized keyword is ignored.
inline void AddGuestData(WorldParams& p, const Env& env, int index, const char* keyword, const char* value)
{
    using namespace detail;
    tables::GuestDef& d = Record(p.m_guests, index);
    if (Is(keyword, "GENDER")) {
        if (Is(value, "M"))
            d.m_Gender = 0;
        else if (Is(value, "F"))
            d.m_Gender = 1;
        else
            env.fatal("Guest gender must be M or F, got \"%s\"", value);
        return;
    }
    if (Is(keyword, "DRUNK_TYPE")) {
        if (Is(value, "TIRED"))
            d.m_DrunkType = 1;
        else if (Is(value, "HORNY"))
            d.m_DrunkType = 2;
        else if (Is(value, "FIGHT"))
            d.m_DrunkType = 3;
        else if (Is(value, "VANDALISE"))
            d.m_DrunkType = 4;
        else
            env.fatal("Guest gender must be M or F, got \"%s\"", value);
        return;
    }
    if (Is(keyword, "NAME"))
        Copy(d.m_Name, value);
    else if (Is(keyword, "SPRITE"))
        Copy(d.m_Sprite, value);
    else if (Is(keyword, "DRUNKTYPE"))
        d.m_DrunkType = static_cast<uint8_t>(ToInt(value));
    else if (Is(keyword, "MONEY"))
        d.m_Money = ToFloat(value);
    else if (Is(keyword, "LITTER"))
        d.m_Litter = ToFloat(value);
    else if (Is(keyword, "DEF_SPRITE"))
        d.m_DefSprite = ToInt(value);
    else if (Is(keyword, "ATTRACTIVE"))
        d.m_Attractive = ToFloat(value);
    else if (Is(keyword, "ATTRACTIVE_PREF"))
        d.m_AttractivePref = ToFloat(value);
    else if (Is(keyword, "SWIM_ENERGY"))
        d.m_SwimEnergy = ToFloat(value);
    else if (Is(keyword, "SWIM_DIST"))
        d.m_SwimDist = ToFloat(value);
    else if (Is(keyword, "COLOUR"))
        d.m_Colour = static_cast<uint8_t>(ToInt(value));
    else if (Is(keyword, "DRUNKDECAY"))
        d.m_DrunkDecay = ToFloat(value);
    else if (Is(keyword, "FLIRTDECAY"))
        d.m_FlirtDecay = ToFloat(value);
    else if (Is(keyword, "FIGHTDECAY"))
        d.m_FightDecay = ToFloat(value);
    else if (Is(keyword, "VANDALISEDECAY"))
        d.m_VandaliseDecay = ToFloat(value);
    else if (Is(keyword, "NIGHTS_MIN"))
        d.m_NightsMin = static_cast<uint8_t>(ToInt(value));
    else if (Is(keyword, "NIGHTS_MAX"))
        d.m_NightsMax = static_cast<uint8_t>(ToInt(value));
    else if (Is(keyword, "SUNBURN_TIME"))
        d.m_SunburnTime = ToFloat(value);
    else if (Is(keyword, "ILL_DECAY"))
        d.m_IllDecay = ToFloat(value);
    else if (Is(keyword, "START_BLADDER"))
        d.m_StartBladder = ToFloat(value);
    else if (Is(keyword, "START_FIGHT"))
        d.m_StartFight = ToFloat(value);
    else if (Is(keyword, "START_FLIRT"))
        d.m_StartFlirt = ToFloat(value);
    else if (Is(keyword, "START_ILL"))
        d.m_StartIll = ToFloat(value);
    else if (Is(keyword, "START_DRUNK"))
        d.m_StartDrunk = ToFloat(value);
    else if (Is(keyword, "START_SUNBURN"))
        d.m_StartSunburn = ToFloat(value);
    else if (Is(keyword, "START_VANDALISE"))
        d.m_StartVandalise = ToFloat(value);
    else {
        for (size_t i = 0; i < std::size(kGuestMaxKeywords); ++i)
            if (Is(keyword, kGuestMaxKeywords[i])) {
                d.m_Max[i] = ToInt(value);
                return;
            }
    }
}

// Stores a float range (`first`, `second`) for one keyword of guest type `index`.
inline void AddGuestFloatRange(WorldParams& p, const Env& env, int index, const char* keyword, const char* first, const char* second)
{
    using namespace detail;
    tables::GuestDef& d = Record(p.m_guests, index);
    const std::array<float, 2> pair{ToFloat(first), ToFloat(second)};
    if (Is(keyword, "SLEEPTIME")) {
        d.m_SleepTime = pair;
        return;
    }
    if (Is(keyword, "WAKETIME")) {
        d.m_WakeTime = pair;
        return;
    }
    for (const GuestPairField& field : kGuestPairs) {
        if (!Is(keyword, field.keyword)) continue;
        (d.*field.list)[static_cast<size_t>(field.index)] = pair;
        return;
    }
    env.fatal("Unrecognized guest range field \"%s\" in the gameplay data", keyword);
}

// Stores one keyword of table `index`.
inline void AddTableData(WorldParams& p, const Env& env, int index, const char* keyword, const char* value)
{
    using namespace detail;
    tables::TableDef& d = Record(p.m_tables, index);
    if (Is(keyword, "NAME"))
        Copy(d.m_Name, value);
    else if (Is(keyword, "TABLE_SPRITE"))
        Copy(d.m_TableSprite, value);
    else if (Is(keyword, "CHAIR_SPRITE"))
        Copy(d.m_ChairSprite, value);
    else if (Is(keyword, "GRIDWIDTH"))
        d.m_GridWidth = ToInt(value);
    else if (Is(keyword, "GRIDHEIGHT"))
        d.m_GridHeight = ToInt(value);
    else if (Is(keyword, "CAPACITY")) {
        const int capacity = ToInt(value);
        d.m_NumChairs = static_cast<uint32_t>(capacity);
        d.m_chairDirections.assign(static_cast<size_t>(capacity), 0);
        d.m_chairWalkPoints.assign(d.m_NumChairs, {});
    } else if (Is(keyword, "NUM_WALK_PTS")) {
        d.m_walkPoints.assign(static_cast<size_t>(ToInt(value)), {});
    } else if (Is(keyword, "WALK_PNT_X")) {
        Record(d.m_walkPoints, d.m_NumWalkPts)[0] = ToFloat(value);
    } else if (Is(keyword, "WALK_PNT_Y")) {
        Record(d.m_walkPoints, d.m_NumWalkPts)[1] = ToFloat(value);
        ++d.m_NumWalkPts;
    } else if (Is(keyword, "NUM_CHAIRPOS_PTS")) {
        d.m_chairPositions.assign(static_cast<size_t>(ToInt(value)), {});
    } else if (Is(keyword, "CHAIRPOS_X")) {
        Record(d.m_chairPositions, d.m_NumChairPos)[0] = ToFloat(value);
    } else if (Is(keyword, "CHAIRPOS_Y")) {
        Record(d.m_chairPositions, d.m_NumChairPos)[1] = ToFloat(value);
        ++d.m_NumChairPos;
    } else if (Is(keyword, "CHAIR_DIRECTION")) {
        static const char* const kDirections[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        for (int i = 0; i < 8; ++i)
            if (Is(value, kDirections[i])) {
                Record(d.m_chairDirections, d.m_NumChairDirs) = i;
                ++d.m_NumChairDirs;
                return;
            }
        env.fatal("Invalid direction in table - %s", value);
    } else if (Is(keyword, "CHAIR_NUM_WALKPNT")) {
        // The list holds its count first, then the walk point ids (CHAIR_WALKPNT).
        std::vector<int>& list = Record(d.m_chairWalkPoints, d.m_NumChairWalkLists);
        list.assign(static_cast<size_t>(ToInt(value)) + 1, 0);
        list[0] = ToInt(value);
        ++d.m_NumChairWalkLists;
        d.m_ChairWalkIdx = 1;
    } else if (Is(keyword, "CHAIR_WALKPNT")) {
        Record(Record(d.m_chairWalkPoints, d.m_NumChairWalkLists - 1), d.m_ChairWalkIdx) = ToInt(value);
        ++d.m_ChairWalkIdx;
    } else if (Is(keyword, "SIT_DOWN_ANIM"))
        Copy(d.m_SitDownAnim, value);
    else if (Is(keyword, "NUM_SAT_DOWN_ANIM")) {
        d.m_satDownAnims.assign(static_cast<size_t>(ToInt(value)), std::string());
    } else if (Is(keyword, "SAT_DOWN_ANIM")) {
        Record(d.m_satDownAnims, d.m_NumSatDownAnims) = value;
        ++d.m_NumSatDownAnims;
    } else if (Is(keyword, "GET_UP_ANIM"))
        Copy(d.m_GetUpAnim, value);
    else if (Is(keyword, "SERVE_TABLE"))
        d.m_serveTable = ToInt(value) != 0;
    else if (Is(keyword, "XOFFSET_TABLE"))
        d.m_XOffsetTable = ToInt(value);
    else if (Is(keyword, "YOFFSET_TABLE"))
        d.m_YOffsetTable = ToInt(value);
    else if (Is(keyword, "HAS_WAITER"))
        d.m_hasPlates = ToInt(value) != 0;
    else if (Is(keyword, "WAITER_SPRITE"))
        Copy(d.m_waiterSprite, value);
    else if (Is(keyword, "WAITER_WALK_ANIM"))
        Copy(d.m_waiterWalkAnim, value);
    else if (Is(keyword, "WAITER_SERVE_ANIM"))
        Copy(d.m_waiterServeAnim, value);
    else if (Is(keyword, "CALL_WAITER_ANIM"))
        Copy(d.m_callWaiterAnim, value);
    else if (Is(keyword, "WAIT_WAITER_ANIM"))
        Copy(d.m_waitWaiterAnim, value);
    else if (Is(keyword, "WAITER_SERVE_POINT_X")) {
        if (d.m_NumServePoints == 0) d.m_servePoints.assign(d.m_NumChairs, {});
        Record(d.m_servePoints, d.m_NumServePoints)[0] = ToFloat(value);
    } else if (Is(keyword, "WAITER_SERVE_POINT_Y")) {
        Record(d.m_servePoints, d.m_NumServePoints)[1] = ToFloat(value);
        ++d.m_NumServePoints;
    } else
        env.fatal("Unrecognized table field \"%s\" in the gameplay data", keyword);
}

// Stores one keyword of building `index`. `value` is modified for ANIMSPRITE and
// COMMODITY (see the header comment); Process owns the buffer.
inline void AddBuildingData(WorldParams& p, const Env& env, int index, const char* keyword, char* value)
{
    using namespace detail;
    tables::BuildingDef& d = Record(p.m_buildings, index);
    d.m_DefaultBeer = 0;
    if (Is(keyword, "NAME"))
        Copy(d.m_Name, value);
    else if (Is(keyword, "SPRITE"))
        Copy(d.m_Sprite, value);
    else if (Is(keyword, "ICON"))
        d.m_Icon = ToInt(value);
    else if (Is(keyword, "COST2BUILD"))
        d.m_Cost2Build = ToInt(value);
    else if (Is(keyword, "REFUNDVALUE"))
        d.m_RefundValue = ToInt(value);
    else if (Is(keyword, "GRIDWIDTH"))
        d.m_GridWidth = ToInt(value);
    else if (Is(keyword, "GRIDHEIGHT"))
        d.m_GridHeight = ToInt(value);
    else if (Is(keyword, "XSERVEOFFSET"))
        d.m_ServeOffset[0] = ToShort(value);
    else if (Is(keyword, "YSERVEOFFSET"))
        d.m_ServeOffset[1] = ToShort(value);
    else if (Is(keyword, "STAFF_XOFFSET"))
        d.m_StaffOffset[0] = ToShort(value);
    else if (Is(keyword, "STAFF_YOFFSET"))
        d.m_StaffOffset[1] = ToShort(value);
    else if (Is(keyword, "XENTRANCEOFFSET"))
        d.m_EntranceOffset[0] = ToShort(value);
    else if (Is(keyword, "YENTRANCEOFFSET"))
        d.m_EntranceOffset[1] = ToShort(value);
    else if (Is(keyword, "GUESTCAPACITY"))
        d.m_GuestCapacity = ToInt(value);
    else if (Is(keyword, "BUILDTIME"))
        d.m_BuildTime = ToInt(value);
    else if (Is(keyword, "OPENINGHOURS")) {
        const int id = FindOpeningHoursId(p, value);
        if (id == queries::kNotFound) {
            env.fatal("Unrecognized opening hours \"%s\" in the gameplay data", value);
            return;
        }
        d.m_OpeningHours = id;
    } else if (Is(keyword, "MAXINSTANCES"))
        d.m_MaxInstances = ToInt(value);
    else if (Is(keyword, "ATTRACTOR")) {
        const int id = queries::FindCommodityFamilyId(p, value);
        if (id == queries::kNotFound) {
            env.fatal("Unrecognized commodity family \"%s\" in the gameplay data", value);
            return;
        }
        d.m_Attractor = id;
    } else if (Is(keyword, "TREE"))
        Copy(d.m_Tree, value);
    else if (Is(keyword, "MENU"))
        Copy(d.m_Menu, value);
    else if (Is(keyword, "ANIMSPRITE")) {
        if (d.m_NumAnimSprites >= tables::kBuildingAnimSprites) {
            env.fatal("A building has more than eight animation sprites: \"%s\"", value);
            return;
        }
        tables::AnimSpriteDef& sprite = Record(d.m_AnimSprites, d.m_NumAnimSprites);
        ++d.m_NumAnimSprites;
        Copy(sprite.m_Sprite, value);
        char* token[9];
        char* at = value;
        for (char*& t : token) at = t = NextToken(at);
        sprite.m_X[0] = ToInt(token[0]);
        sprite.m_Y[0] = ToInt(token[1]);
        sprite.m_X[1] = ToInt(token[2]);
        sprite.m_Y[1] = ToInt(token[3]);
        sprite.m_X[2] = ToInt(token[4]);
        sprite.m_Y[2] = ToInt(token[5]);
        sprite.m_X[3] = ToInt(token[6]);
        sprite.m_Y[3] = ToInt(token[7]);
        sprite.m_animated = ToInt(token[8]) != 0;
        for (char* t : token) t[-1] = 0;
    } else if (Is(keyword, "COMMODITY")) {
        const int slot = d.m_NumCommodities;
        char* token[6];
        char* at = value;
        for (char*& t : token) at = t = NextToken(at);
        const int id = queries::FindCommodityId(p, value);
        if (id == queries::kNotFound) {
            env.fatal("Unrecognized commodity \"%s\" in the gameplay data", value);
            return;
        }
        Record(d.m_Commodity, slot) = id;
        Record(d.m_CommodityAB, slot)[0] = ToFloat(token[0]);
        Record(d.m_CommodityAB, slot)[1] = ToFloat(token[1]);
        Record(d.m_CommodityC, slot) = ToFloat(token[2]);
        Record(d.m_CommodityCost, slot) = ToFloat(token[3]);
        Record(d.m_CommodityE, slot) = ToFloat(token[4]);
        Record(d.m_CommodityF, slot) = ToFloat(token[5]);
        ++d.m_NumCommodities;
        for (char* t : token) t[-1] = 0;
    } else if (Is(keyword, "MAXSTAFF"))
        d.m_MaxStaff = ToInt(value);
    else if (Is(keyword, "STAFF")) {
        const int id = queries::FindStaffId(p, value);
        if (id == queries::kNotFound) {
            env.fatal("Unrecognized staff type \"%s\" in the gameplay data", value);
            return;
        }
        Store(d.m_Staff, d.m_NumStaff, id);
        ++d.m_NumStaff;
    } else if (Is(keyword, "VEHICLE")) {
        const int id = queries::FindVehicleId(p, value);
        if (id == queries::kNotFound) {
            env.fatal("Unrecognized vehicle type \"%s\" in the gameplay data", value);
            return;
        }
        Record(d.m_Vehicle, d.m_NumVehicles) = id;
        ++d.m_NumVehicles;
    } else if (Is(keyword, "VEHICLE_DIR"))
        Record(d.m_VehicleDir, d.m_NumVehiclePlaces) = ToInt(value);
    else if (Is(keyword, "VEHICLE_GRID_XOFFSET"))
        Record(d.m_VehicleGridOffset, d.m_NumVehiclePlaces)[0] = ToInt(value);
    else if (Is(keyword, "VEHICLE_GRID_YOFFSET")) {
        Record(d.m_VehicleGridOffset, d.m_NumVehiclePlaces)[1] = ToInt(value);
        ++d.m_NumVehiclePlaces;
    } else if (Is(keyword, "BUILDON")) {
        if (Is(value, "SHALLOW_WATER"))
            d.m_BuildOn |= 4;
        else if (Is(value, "LAND"))
            d.m_BuildOn |= 1;
        else if (Is(value, "DEEP_SEA"))
            d.m_BuildOn |= 8;
        else if (Is(value, "BEACH"))
            d.m_BuildOn |= 2;
        else if (Is(value, "WATER"))
            d.m_BuildOn |= 0xc;
        else
            env.fatal("Unrecognized building ground type \"%s\" in the gameplay data", value);
    } else if (Is(keyword, "TABLE")) {
        const int id = queries::FindTableId(p, value);
        if (id == queries::kNotFound) {
            env.fatal("Unrecognized table type \"%s\" in the gameplay data", value);
            return;
        }
        Store(d.m_Table, d.m_NumTables, id);
    } else if (Is(keyword, "TABLE_POSX"))
        Record(d.m_TablePos, d.m_NumTables)[0] = ToFloat(value);
    else if (Is(keyword, "TABLE_POSY")) {
        Record(d.m_TablePos, d.m_NumTables)[1] = ToFloat(value);
        ++d.m_NumTables;
    } else if (Is(keyword, "EVENT")) {
        const int eventCount = p.m_numEvents;
        const int id = queries::FindEventId(p, value);
        if (id == queries::kNotFound) {
            env.fatal("Unrecognized event \"%s\" in the gameplay data", value);
            return;
        }
        if (eventCount < tables::kBuildingEventSlots)
            d.m_Event[eventCount] = id;
        else if (eventCount == tables::kBuildingEventSlots)
            d.m_NumEvents = id;
        else {
            env.fatal("Building event number out of range: \"%s\"", value);
            return;
        }
        ++d.m_NumEvents;
    } else if (Is(keyword, "CONSTRUCT")) {
        Copy(d.m_Construct, value);
        d.m_customConstruct = 1;
    } else if (Is(keyword, "POWER"))
        d.m_Power = ToInt(value);
    else if (Is(keyword, "NUM_WALKPOINTS")) {
        d.m_NumWalkPoints = 0;
        const int count = ToInt(value);
        d.m_walkPoints.assign(count > 0 ? static_cast<size_t>(count) : 0, {});
    } else if (Is(keyword, "WALKPOINTX"))
        Record(d.m_walkPoints, d.m_NumWalkPoints)[0] = ToFloat(value);
    else if (Is(keyword, "WALKPOINTY")) {
        Record(d.m_walkPoints, d.m_NumWalkPoints)[1] = ToFloat(value);
        ++d.m_NumWalkPoints;
    } else if (Is(keyword, "AVFAILTIME"))
        d.m_AvFailTime = ToFloat(value);
    else if (Is(keyword, "FAIL_PERCENT"))
        d.m_FailPercent = ToInt(value);
    else if (Is(keyword, "FAIL_AGE"))
        d.m_FailAge = ToInt(value);
    else if (Is(keyword, "DEMOLISHTIME"))
        d.m_DemolishTime = ToInt(value);
    else if (Is(keyword, "MAX_QUEUE"))
        d.m_MaxQueue = ToInt(value);
    else if (Is(keyword, "CATEGORY")) {
        // An unrecognized value leaves the category alone.
        if (Is(value, "Hotel"))
            d.m_Category = 0;
        else if (Is(value, "Food"))
            d.m_Category = 1;
        else if (Is(value, "Leisure"))
            d.m_Category = 2;
        else if (Is(value, "Other"))
            d.m_Category = 3;
    } else if (Is(keyword, "NUM_BASEPLATES")) {
        d.m_NumBasePlates = 0;
        const int count = ToInt(value);
        d.m_basePlateTypes.assign(count > 0 ? static_cast<size_t>(count) : 0, 0);
    } else if (Is(keyword, "BASEPLATETYPE")) {
        Record(d.m_basePlateTypes, d.m_NumBasePlates) = ToInt(value);
        ++d.m_NumBasePlates;
    } else if (Is(keyword, "MAXBUILDERS"))
        d.m_MaxBuilders = static_cast<float>(ToInt(value));
    else if (Is(keyword, "FEMALEFILENAME"))
        Copy(d.m_FemaleFileName, value);
    else if (Is(keyword, "MALEFILENAME"))
        Copy(d.m_MaleFileName, value);
    else
        env.fatal("Unrecognized building field \"%s\" in the gameplay data", keyword);
}

} // namespace obl::world_params_add
