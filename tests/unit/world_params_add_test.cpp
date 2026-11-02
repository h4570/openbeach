#include "unit_test.h"
#include "importer/src/readers/world_params_add.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
namespace wpa = obl::world_params_add;
using tables::WorldParams;

// The world params add routine*Data / AddStaff / AddGuestFloatRange / AddNameData (docs/specs/world-params.md). The checks read the records' fields,
// so a wrong field in the keyword table fails here without the game.

namespace {

struct Fatals {
    std::vector<std::string> messages;
};
Fatals g_fatals;
std::vector<void*> g_heap;

void RecordFatal(const char* format, const char* argument)
{
    char text[256];
    std::snprintf(text, sizeof(text), format, argument ? argument : "");
    g_fatals.messages.push_back(text);
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
    Fixture() { g_fatals.messages.clear(); }
    ~Fixture()
    {
        for (void* memory : g_heap) std::free(memory);
        g_heap.clear();
    }
    WorldParams& operator*() { return *params; }
    WorldParams* operator->() { return params.get(); }
};

// A multi-value string as the world params process routine builds it: the first token, then every further token behind its opening quote
// (the closing quotes are NUL).
std::vector<char> Quoted(std::initializer_list<const char*> tokens)
{
    std::vector<char> text;
    bool first = true;
    for (const char* token : tokens) {
        if (!first) text.push_back('"');
        first = false;
        text.insert(text.end(), token, token + std::strlen(token) + 1);
    }
    return text;
}

void AddStaffType(Fixture& f, const char* key, const char* sex = "F")
{
    wpa::AddStaff(*f, kEnv, key, "Display Name", sex, "12", "sprite.spr", "OBJTYPE", "MENU_X", "40", "1.25", "0.5", "0.75", "8", "17");
}

} // namespace

OBL_TEST_CASE("world_params_add.staff_appends_records_in_file_order")
{
    Fixture f;
    AddStaffType(f, "BAR_F");
    AddStaffType(f, "BAR_M", "M");
    OBL_CHECK_EQ(f->m_numStaff, 2);
    const auto& first = f->m_staff[0];
    OBL_CHECK_EQ(first.m_id, 0);
    OBL_CHECK_EQ(first.m_key, std::string("BAR_F"));
    OBL_CHECK_EQ(first.m_Name, std::string("Display Name"));
    OBL_CHECK_EQ(first.m_female, 1);
    OBL_CHECK_EQ(first.m_Wage, 12);
    OBL_CHECK_EQ(first.m_Sprite, std::string("sprite.spr"));
    OBL_CHECK_EQ(first.m_ObjectType, std::string("OBJTYPE"));
    OBL_CHECK_EQ(first.m_Menu, std::string("MENU_X"));
    OBL_CHECK_EQ(first.m_RecommendedWage, 40);
    OBL_CHECK_EQ(first.m_scale, 1.25f);
    OBL_CHECK_EQ(first.m_expectationGrowth, 0.5f);
    OBL_CHECK_EQ(first.m_initialHappiness, 0.75f);
    OBL_CHECK_EQ(first.m_StartHour, 8);
    OBL_CHECK_EQ(first.m_EndHour, 17);
    OBL_CHECK_EQ(f->m_staff[1].m_id, 1);
    OBL_CHECK_EQ(f->m_staff[1].m_key, std::string("BAR_M"));
    OBL_CHECK_EQ(f->m_staff[1].m_female, 0);
    OBL_CHECK_EQ(f->m_numStaff, 2); // m_numStaff
}

OBL_TEST_CASE("world_params_add.staff_rejects_a_sex_other_than_m_or_f")
{
    Fixture f;
    AddStaffType(f, "ODD", "X");
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(1));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Staff gender must be M or F, got \"ODD\""));
}

OBL_TEST_CASE("world_params_add.names_fill_three_pools_of_one_array")
{
    Fixture f;
    wpa::AddNameData(*f, kEnv, "MALENAME", "NAME_M0");
    wpa::AddNameData(*f, kEnv, "MALENAME", "NAME_M1");
    wpa::AddNameData(*f, kEnv, "FEMALENAME", "NAME_F0");
    wpa::AddNameData(*f, kEnv, "SURNAME", "NAME_S0");
    OBL_CHECK_EQ(f->m_nameCounts[0], 2);
    OBL_CHECK_EQ(f->m_nameCounts[1], 1);
    OBL_CHECK_EQ(f->m_nameCounts[2], 1);
    OBL_CHECK_EQ(f->m_nameCounts[0], 2);
    OBL_CHECK_EQ(std::string(f->m_names[0]), std::string("NAME_M0"));
    OBL_CHECK_EQ(std::string(f->m_names[1]), std::string("NAME_M1"));
    OBL_CHECK_EQ(std::string(f->m_names[0x8c]), std::string("NAME_F0"));
    OBL_CHECK_EQ(std::string(f->m_names[0x118]), std::string("NAME_S0"));
    wpa::AddNameData(*f, kEnv, "NICKNAME", "x");
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(1));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Unrecognized name list field \"NICKNAME\" in the gameplay data"));
}

OBL_TEST_CASE("world_params_add.event_fields")
{
    Fixture f;
    wpa::AddEventData(*f, kEnv, 2, "COST", "100");
    wpa::AddEventData(*f, kEnv, 2, "DEF_PRICE", "7.5");
    wpa::AddEventData(*f, kEnv, 2, "MALE_INTENSITY", "0.25");
    wpa::AddEventData(*f, kEnv, 2, "FEMALE_INTENSITY", "0.125");
    const auto& event = f->m_events[2];
    OBL_CHECK_EQ(event.m_cost, 100.0f);
    OBL_CHECK_EQ(event.m_price, 7.5f);
    OBL_CHECK_EQ(event.m_intensity[0], 0.25f);
    OBL_CHECK_EQ(event.m_intensity[1], 0.125f);
    OBL_CHECK_EQ(event.m_available, static_cast<uint8_t>(0)); // availability is the scenario script's
    OBL_CHECK(g_fatals.messages.empty());
    wpa::AddEventData(*f, kEnv, 2, "BOGUS", "1");
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Unrecognized event field \"BOGUS\" in the gameplay data"));
}

OBL_TEST_CASE("world_params_add.beer_fields_and_effects")
{
    Fixture f;

    wpa::AddBeerData(*f, kEnv, 1, "BRAND", "Grimleys");
    wpa::AddBeerData(*f, kEnv, 1, "COCKTAIL", "Spritzer");
    wpa::AddBeerData(*f, kEnv, 1, "BASECOST", "0.5");
    wpa::AddBeerData(*f, kEnv, 1, "STRCOST", "0.25");
    wpa::AddBeerData(*f, kEnv, 1, "ALCSTR", "4.5");
    wpa::AddBeerData(*f, kEnv, 1, "PRICE", "3");
    OBL_CHECK_EQ(f->m_beers[1].m_Brand, std::string("Grimleys"));
    OBL_CHECK_EQ(f->m_beers[1].m_Cocktail, std::string("Spritzer"));
    OBL_CHECK_EQ(f->m_beers[1].m_BaseCost, 0.5f);
    OBL_CHECK_EQ(f->m_beers[1].m_StrCost, 0.25f);
    OBL_CHECK_EQ(f->m_beers[1].m_Price, 3.0f);
    OBL_CHECK_EQ(f->m_beers[1].m_AlcStr, 4.5f);
    OBL_CHECK_EQ(f->m_beers[1].m_scriptFlag, static_cast<uint8_t>(1)); // every call sets it
    static const char* const kEffects[] = {"FIGHT", "FLIRT", "VANDAL", "HAPPY", "SAD"};
    for (int i = 0; i < 5; ++i) {
        wpa::AddBeerData(*f, kEnv, 1, "EFFECT", kEffects[i]);
        OBL_CHECK_EQ(f->m_beers[1].m_Effect, i);
    }
    OBL_CHECK(g_fatals.messages.empty());
    wpa::AddBeerData(*f, kEnv, 1, "EFFECT", "ANGRY");
    wpa::AddBeerData(*f, kEnv, 1, "BOGUS", "1");
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(2));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Unrecognized beer effect \"ANGRY\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[1], std::string("Unrecognized event field \"BOGUS\" in the gameplay data"));
}

OBL_TEST_CASE("world_params_add.vehicle_fields")
{
    Fixture f;
    AddStaffType(f, "DRIVER");
    AddStaffType(f, "MATE");
    wpa::AddVehicleData(*f, kEnv, 3, "NAME", "Ferry");
    wpa::AddVehicleData(*f, kEnv, 3, "SPRITE", "ferry.spr");
    wpa::AddVehicleData(*f, kEnv, 3, "GRIDWIDTH", "3");
    wpa::AddVehicleData(*f, kEnv, 3, "GRIDHEIGHT", "2");
    wpa::AddVehicleData(*f, kEnv, 3, "CAPACITY", "30");
    wpa::AddVehicleData(*f, kEnv, 3, "TERRAIN", "WATER");
    wpa::AddVehicleData(*f, kEnv, 3, "MAX_SPEED", "2.5");
    wpa::AddVehicleData(*f, kEnv, 3, "TOWED", "trailer");
    wpa::AddVehicleData(*f, kEnv, 3, "FORCE", "10");
    wpa::AddVehicleData(*f, kEnv, 3, "MASS", "400");
    wpa::AddVehicleData(*f, kEnv, 3, "MOVETYPE", "BOUNDARY");
    wpa::AddVehicleData(*f, kEnv, 3, "ANIMSET", "ferry_anims");
    wpa::AddVehicleData(*f, kEnv, 3, "STAFF", "MATE");
    wpa::AddVehicleData(*f, kEnv, 3, "STAFF", "DRIVER");
    wpa::AddVehicleData(*f, kEnv, 3, "CREATION_PNTX", "12.5");
    wpa::AddVehicleData(*f, kEnv, 3, "CREATION_PNTY", "-4");
    OBL_CHECK(g_fatals.messages.empty());
    const auto& v = f->m_vehicles[3];
    OBL_CHECK_EQ(v.m_Name, std::string("Ferry"));
    OBL_CHECK_EQ(v.m_Sprite, std::string("ferry.spr"));
    OBL_CHECK_EQ(v.m_GridWidth, 3);
    OBL_CHECK_EQ(v.m_GridHeight, 2);
    OBL_CHECK_EQ(v.m_Capacity, 30);
    OBL_CHECK_EQ(v.m_MaxSpeed, 2.5f);
    OBL_CHECK_EQ(v.m_Towed, std::string("trailer"));
    OBL_CHECK_EQ(v.m_Force, 10.0f);
    OBL_CHECK_EQ(v.m_Mass, 400.0f);
    OBL_CHECK_EQ(v.m_MoveType, static_cast<uint8_t>(2));
    OBL_CHECK_EQ(v.m_AnimSet, std::string("ferry_anims"));
    OBL_CHECK_EQ(v.m_NumStaff, 2);
    OBL_CHECK(v.m_Staff == (std::vector<int>{1, 0}));
    OBL_CHECK_EQ(v.m_CreationPoint[0], 12.5f);
    OBL_CHECK_EQ(v.m_CreationPoint[1], -4.0f);
    wpa::AddVehicleData(*f, kEnv, 3, "MOVETYPE", "POINTS");
    OBL_CHECK_EQ(f->m_vehicles[3].m_MoveType, static_cast<uint8_t>(1));
}

OBL_TEST_CASE("world_params_add.vehicle_errors")
{
    Fixture f;
    wpa::AddVehicleData(*f, kEnv, 0, "MOVETYPE", "FLY");
    wpa::AddVehicleData(*f, kEnv, 0, "STAFF", "NOBODY");
    wpa::AddVehicleData(*f, kEnv, 0, "WHEELS", "4");
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(3));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Unrecognized vehicle movement \"FLY\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[1], std::string("Unrecognized staff type \"NOBODY\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[2], std::string("Unrecognized vehicle field \"WHEELS\" in the gameplay data"));
    OBL_CHECK_EQ(f->m_vehicles[0].m_NumStaff, 0); // a failed STAFF does not count
    OBL_CHECK(f->m_vehicles[0].m_Staff.empty());
}

OBL_TEST_CASE("world_params_add.scenery_fields")
{
    Fixture f;
    wpa::AddSceneryData(*f, kEnv, 5, "NAME", "Bin");
    wpa::AddSceneryData(*f, kEnv, 5, "SPRITE", "bin.spr");
    wpa::AddSceneryData(*f, kEnv, 5, "ANIM", "bin.anm");
    wpa::AddSceneryData(*f, kEnv, 5, "GRIDWIDTH", "2");
    wpa::AddSceneryData(*f, kEnv, 5, "GRIDHEIGHT", "3");
    wpa::AddSceneryData(*f, kEnv, 5, "COST2BUILD", "150");
    wpa::AddSceneryData(*f, kEnv, 5, "BUILDON", "LAND");
    wpa::AddSceneryData(*f, kEnv, 5, "BUILDON", "BEACH");
    wpa::AddSceneryData(*f, kEnv, 5, "BUILDON", "SHALLOW_WATER");
    wpa::AddSceneryData(*f, kEnv, 5, "BUILDON", "DEEP_SEA");
    wpa::AddSceneryData(*f, kEnv, 5, "BUILDON", "PATH");
    wpa::AddSceneryData(*f, kEnv, 5, "BUILDON", "WATER"); // scenery has no WATER mask: ignored
    wpa::AddSceneryData(*f, kEnv, 5, "COLLIDE", "7");
    wpa::AddSceneryData(*f, kEnv, 5, "CAPACITY", "4");
    wpa::AddSceneryData(*f, kEnv, 5, "MENU", "MENU_BIN");
    wpa::AddSceneryData(*f, kEnv, 5, "ICON", "9");
    wpa::AddSceneryData(*f, kEnv, 5, "RATING", "0.5");
    wpa::AddSceneryData(*f, kEnv, 5, "CLEAN", "1");
    wpa::AddSceneryData(*f, kEnv, 5, "REPAIR", "0");
    wpa::AddSceneryData(*f, kEnv, 5, "BURN", "2");
    wpa::AddSceneryData(*f, kEnv, 5, "USERREMOVE", "1");
    wpa::AddSceneryData(*f, kEnv, 5, "VEGFLAG", "1");
    wpa::AddSceneryData(*f, kEnv, 5, "PATHFLAG", "5");
    wpa::AddSceneryData(*f, kEnv, 5, "WATER", "3");
    wpa::AddSceneryData(*f, kEnv, 5, "UNLISTED", "3"); // ignored
    OBL_CHECK(g_fatals.messages.empty());
    const auto& s = f->m_scenery[5];
    OBL_CHECK_EQ(s.m_Name, std::string("Bin"));
    OBL_CHECK_EQ(s.m_Sprite, std::string("bin.spr"));
    OBL_CHECK_EQ(s.m_Anim, std::string("bin.anm"));
    OBL_CHECK_EQ(s.m_GridWidth, 2);
    OBL_CHECK_EQ(s.m_GridHeight, 3);
    OBL_CHECK_EQ(s.m_Cost2Build, 150);
    OBL_CHECK_EQ(s.m_BuildOn, 1u | 2u | 4u | 8u | 0x80u);
    OBL_CHECK_EQ(s.m_collide, static_cast<uint8_t>(1));
    OBL_CHECK_EQ(s.m_Capacity, 4);
    OBL_CHECK_EQ(s.m_Menu, std::string("MENU_BIN"));
    OBL_CHECK_EQ(s.m_Icon, 9);
    OBL_CHECK_EQ(s.m_Rating, 0.5f);
    OBL_CHECK_EQ(s.m_clean, static_cast<uint8_t>(1));
    OBL_CHECK_EQ(s.m_repair, static_cast<uint8_t>(0));
    OBL_CHECK_EQ(s.m_water, static_cast<uint8_t>(1));
    OBL_CHECK_EQ(s.m_PathFlag, 5);
    OBL_CHECK_EQ(s.m_burn, static_cast<uint8_t>(1));
    OBL_CHECK_EQ(s.m_userRemove, static_cast<uint8_t>(1));
    OBL_CHECK_EQ(s.m_vegFlag, static_cast<uint8_t>(1));
}

OBL_TEST_CASE("world_params_add.scenery_categories")
{
    Fixture f;
    static const char* const kCategories[] = {"FURNITURE", "PATHS", "PLANTS", "ORNAMENTS", "TREES", "SHRUBS", "BILLBOARD", "OTHER"};
    for (int i = 0; i < 8; ++i) {
        wpa::AddSceneryData(*f, kEnv, 0, "CATEGORY", kCategories[i]);
        OBL_CHECK_EQ(f->m_scenery[0].m_Category, i);
    }
    OBL_CHECK(g_fatals.messages.empty());
    wpa::AddSceneryData(*f, kEnv, 0, "CATEGORY", "TOYS");
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(1));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Unrecognized scenery category"));
}

OBL_TEST_CASE("world_params_add.guest_scalar_fields_land_in_the_record")
{
    Fixture f;
    const struct {
        const char* keyword;
        const char* value;
    } fields[] = {
            {"NAME", "Surfer"},
            {"SPRITE", "surf.spr"},
            {"MONEY", "250.5"},
            {"SWIM_ENERGY", "3"},
            {"SWIM_DIST", "40"},
            {"ATTRACTIVE", "0.75"},
            {"DEF_SPRITE", "6"},
            {"DRUNKTYPE", "3"},
            {"ATTRACTIVE_PREF", "0.5"},
            {"COLOUR", "2"},
            {"NIGHTS_MIN", "4"},
            {"NIGHTS_MAX", "9"},
            {"LITTER", "0.125"},
            {"DRUNKDECAY", "1.5"},
            {"FIGHTDECAY", "2.5"},
            {"FLIRTDECAY", "3.5"},
            {"VANDALISEDECAY", "4.5"},
            {"SUNBURN_TIME", "6"},
            {"ILL_DECAY", "7"},
            {"START_BLADDER", "8"},
            {"START_FIGHT", "9"},
            {"START_FLIRT", "10"},
            {"START_DRUNK", "11"},
            {"START_ILL", "12"},
            {"START_SUNBURN", "13"},
            {"START_VANDALISE", "14"},
    };
    for (const auto& field : fields) wpa::AddGuestData(*f, kEnv, 2, field.keyword, field.value);
    const tables::GuestDef& d = f->m_guests[2];
    OBL_CHECK_EQ(d.m_Name, std::string("Surfer"));
    OBL_CHECK_EQ(d.m_Sprite, std::string("surf.spr"));
    OBL_CHECK_EQ(d.m_Money, 250.5f);
    OBL_CHECK_EQ(d.m_SwimEnergy, 3.0f);
    OBL_CHECK_EQ(d.m_SwimDist, 40.0f);
    OBL_CHECK_EQ(d.m_Attractive, 0.75f);
    OBL_CHECK_EQ(d.m_DefSprite, 6);
    OBL_CHECK_EQ(d.m_DrunkType, static_cast<uint8_t>(3));
    OBL_CHECK_EQ(d.m_AttractivePref, 0.5f);
    OBL_CHECK_EQ(d.m_Colour, static_cast<uint8_t>(2));
    OBL_CHECK_EQ(d.m_NightsMin, static_cast<uint8_t>(4));
    OBL_CHECK_EQ(d.m_NightsMax, static_cast<uint8_t>(9));
    OBL_CHECK_EQ(d.m_Litter, 0.125f);
    OBL_CHECK_EQ(d.m_DrunkDecay, 1.5f);
    OBL_CHECK_EQ(d.m_FightDecay, 2.5f);
    OBL_CHECK_EQ(d.m_FlirtDecay, 3.5f);
    OBL_CHECK_EQ(d.m_VandaliseDecay, 4.5f);
    OBL_CHECK_EQ(d.m_SunburnTime, 6.0f);
    OBL_CHECK_EQ(d.m_IllDecay, 7.0f);
    OBL_CHECK_EQ(d.m_StartBladder, 8.0f);
    OBL_CHECK_EQ(d.m_StartFight, 9.0f);
    OBL_CHECK_EQ(d.m_StartFlirt, 10.0f);
    OBL_CHECK_EQ(d.m_StartDrunk, 11.0f);
    OBL_CHECK_EQ(d.m_StartIll, 12.0f);
    OBL_CHECK_EQ(d.m_StartSunburn, 13.0f);
    OBL_CHECK_EQ(d.m_StartVandalise, 14.0f);
    OBL_CHECK(g_fatals.messages.empty());
}

OBL_TEST_CASE("world_params_add.guest_limits_gender_and_drunk_type")
{
    Fixture f;
    static const char* const kLimits[] = {"ACCOMODATION", "ALCOHOL", "AMUSEMENT", "FIRSTAID", "HEADACHE", "MEALS", "PARTYFUN", "SHOWER",
            "SNACKS", "SOFTDRINKS", "SOUVENIRS", "SPORTSFUN", "SUNBATHE", "SWIMFUN", "TOILET", "WATERSPORTSFUN", "STROLL", "LEAVE",
            "VANDALISE", "FIGHT", "FLIRT", "UMBRELLAS", "SUNTAN", "HARDSEX", "SLEEP", "SCARED"};
    for (int i = 0; i < 26; ++i) {
        const std::string keyword = std::string("MAX_") + kLimits[i];
        wpa::AddGuestData(*f, kEnv, 0, keyword.c_str(), std::to_string(100 + i).c_str());
        OBL_CHECK_EQ(f->m_guests[0].m_Max[i], 100 + i); // MAX_ACCOMODATION is the first desire, MAX_SCARED the last
    }
    wpa::AddGuestData(*f, kEnv, 0, "GENDER", "F");
    OBL_CHECK_EQ(f->m_guests[0].m_Gender, 1);
    wpa::AddGuestData(*f, kEnv, 0, "GENDER", "M");
    OBL_CHECK_EQ(f->m_guests[0].m_Gender, 0);
    static const char* const kDrunk[] = {"TIRED", "HORNY", "FIGHT", "VANDALISE"};
    for (int i = 0; i < 4; ++i) {
        wpa::AddGuestData(*f, kEnv, 0, "DRUNK_TYPE", kDrunk[i]);
        OBL_CHECK_EQ(f->m_guests[0].m_DrunkType, static_cast<uint8_t>(i + 1));
    }
    OBL_CHECK(g_fatals.messages.empty());
    wpa::AddGuestData(*f, kEnv, 0, "GENDER", "X");
    wpa::AddGuestData(*f, kEnv, 0, "DRUNK_TYPE", "SLEEPY");
    wpa::AddGuestData(*f, kEnv, 0, "NOTAKEYWORD", "1"); // ignored
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(2));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Guest gender must be M or F, got \"X\""));
    OBL_CHECK_EQ(g_fatals.messages[1], std::string("Guest gender must be M or F, got \"SLEEPY\""));
}

OBL_TEST_CASE("world_params_add.guest_float_ranges_land_in_the_pair_lists")
{
    Fixture f;
    using PairList = std::vector<std::array<float, 2>> tables::GuestDef::*;
    struct Range {
        const char* keyword;
        PairList list;
        size_t index;
    };
    const Range ranges[] = {
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
            {"BEAUTY_START", &tables::GuestDef::m_OpinionStart, 0},
            {"CROWDING_START", &tables::GuestDef::m_OpinionStart, 1},
            {"TOTTY_START", &tables::GuestDef::m_OpinionStart, 2},
            {"PRICE_START", &tables::GuestDef::m_OpinionStart, 3},
            {"VARIETY_START", &tables::GuestDef::m_OpinionStart, 4},
            {"CLEANLINESS_START", &tables::GuestDef::m_OpinionStart, 5},
            {"SAFETY_START", &tables::GuestDef::m_OpinionStart, 6},
            {"BEAUTY_EFFECT", &tables::GuestDef::m_OpinionEffect, 0},
            {"CROWDING_EFFECT", &tables::GuestDef::m_OpinionEffect, 1},
            {"TOTTY_EFFECT", &tables::GuestDef::m_OpinionEffect, 2},
            {"PRICE_EFFECT", &tables::GuestDef::m_OpinionEffect, 3},
            {"VARIETY_EFFECT", &tables::GuestDef::m_OpinionEffect, 4},
            {"CLEANLINESS_EFFECT", &tables::GuestDef::m_OpinionEffect, 5},
            {"SAFETY_EFFECT", &tables::GuestDef::m_OpinionEffect, 6},
            {"BEAUTYTOL", &tables::GuestDef::m_OpinionTol, 0},
            {"CROWDINGTOL", &tables::GuestDef::m_OpinionTol, 1},
            {"TOTTYTOL", &tables::GuestDef::m_OpinionTol, 2},
            {"PRICETOL", &tables::GuestDef::m_OpinionTol, 3},
            {"VARIETYTOL", &tables::GuestDef::m_OpinionTol, 4},
            {"CLEANLINESSTOL", &tables::GuestDef::m_OpinionTol, 5},
            {"SAFETYTOL", &tables::GuestDef::m_OpinionTol, 6},
    };
    for (const Range& range : ranges) {
        wpa::AddGuestFloatRange(*f, kEnv, 1, range.keyword, "1.5", "-2.25");
        const std::array<float, 2>& pair = (f->m_guests[1].*range.list)[range.index];
        OBL_CHECK_EQ(pair[0], 1.5f);
        OBL_CHECK_EQ(pair[1], -2.25f);
    }
    wpa::AddGuestFloatRange(*f, kEnv, 1, "SLEEPTIME", "1.5", "-2.25");
    OBL_CHECK_EQ(f->m_guests[1].m_SleepTime[0], 1.5f);
    OBL_CHECK_EQ(f->m_guests[1].m_SleepTime[1], -2.25f);
    wpa::AddGuestFloatRange(*f, kEnv, 1, "WAKETIME", "1.5", "-2.25");
    OBL_CHECK_EQ(f->m_guests[1].m_WakeTime[0], 1.5f);
    OBL_CHECK_EQ(f->m_guests[1].m_WakeTime[1], -2.25f);
    OBL_CHECK(g_fatals.messages.empty());
    wpa::AddGuestFloatRange(*f, kEnv, 1, "HUNGERPREF", "1", "2");
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(1));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Unrecognized guest range field \"HUNGERPREF\" in the gameplay data"));
}

OBL_TEST_CASE("world_params_add.table_scalars_strings_and_heap_arrays")
{
    Fixture f;
    wpa::AddTableData(*f, kEnv, 2, "NAME", "Round");
    wpa::AddTableData(*f, kEnv, 2, "TABLE_SPRITE", "table.spr");
    wpa::AddTableData(*f, kEnv, 2, "CHAIR_SPRITE", "chair.spr");
    wpa::AddTableData(*f, kEnv, 2, "GRIDWIDTH", "2");
    wpa::AddTableData(*f, kEnv, 2, "GRIDHEIGHT", "3");
    wpa::AddTableData(*f, kEnv, 2, "CAPACITY", "4");
    wpa::AddTableData(*f, kEnv, 2, "SIT_DOWN_ANIM", "sit.anm");
    wpa::AddTableData(*f, kEnv, 2, "GET_UP_ANIM", "up.anm");
    wpa::AddTableData(*f, kEnv, 2, "SERVE_TABLE", "5");
    wpa::AddTableData(*f, kEnv, 2, "XOFFSET_TABLE", "-3");
    wpa::AddTableData(*f, kEnv, 2, "YOFFSET_TABLE", "8");
    wpa::AddTableData(*f, kEnv, 2, "HAS_WAITER", "1");
    wpa::AddTableData(*f, kEnv, 2, "WAITER_SPRITE", "w.spr");
    wpa::AddTableData(*f, kEnv, 2, "WAITER_WALK_ANIM", "w.walk");
    wpa::AddTableData(*f, kEnv, 2, "WAITER_SERVE_ANIM", "w.serve");
    wpa::AddTableData(*f, kEnv, 2, "CALL_WAITER_ANIM", "w.call");
    wpa::AddTableData(*f, kEnv, 2, "WAIT_WAITER_ANIM", "w.wait");
    OBL_CHECK(g_fatals.messages.empty());
    const auto& t = f->m_tables[2];
    OBL_CHECK_EQ(t.m_Name, std::string("Round"));
    OBL_CHECK_EQ(t.m_TableSprite, std::string("table.spr"));
    OBL_CHECK_EQ(t.m_ChairSprite, std::string("chair.spr"));
    OBL_CHECK_EQ(t.m_GridWidth, 2);
    OBL_CHECK_EQ(t.m_GridHeight, 3);
    OBL_CHECK_EQ(t.m_NumChairs, 4u);
    OBL_CHECK_EQ(t.m_SitDownAnim, std::string("sit.anm"));
    OBL_CHECK_EQ(t.m_GetUpAnim, std::string("up.anm"));
    OBL_CHECK_EQ(t.m_serveTable, static_cast<uint8_t>(1));
    OBL_CHECK_EQ(t.m_XOffsetTable, -3);
    OBL_CHECK_EQ(t.m_YOffsetTable, 8);
    OBL_CHECK_EQ(t.m_hasPlates, static_cast<uint8_t>(1));
    OBL_CHECK_EQ(t.m_waiterSprite, std::string("w.spr"));
    OBL_CHECK_EQ(t.m_waiterWalkAnim, std::string("w.walk"));
    OBL_CHECK_EQ(t.m_waiterServeAnim, std::string("w.serve"));
    OBL_CHECK_EQ(t.m_callWaiterAnim, std::string("w.call"));
    OBL_CHECK_EQ(t.m_waitWaiterAnim, std::string("w.wait"));
    // CAPACITY sizes the per-chair direction and walk-list vectors.
    OBL_CHECK_EQ(t.m_chairDirections.size(), 4u);
    OBL_CHECK_EQ(t.m_chairWalkPoints.size(), 4u);
}

OBL_TEST_CASE("world_params_add.table_point_lists_use_running_counts")
{
    Fixture f;
    wpa::AddTableData(*f, kEnv, 0, "CAPACITY", "2");
    wpa::AddTableData(*f, kEnv, 0, "NUM_WALK_PTS", "2");
    wpa::AddTableData(*f, kEnv, 0, "WALK_PNT_X", "1.5");
    wpa::AddTableData(*f, kEnv, 0, "WALK_PNT_Y", "2.5");
    wpa::AddTableData(*f, kEnv, 0, "WALK_PNT_X", "3.5");
    wpa::AddTableData(*f, kEnv, 0, "WALK_PNT_Y", "4.5");
    wpa::AddTableData(*f, kEnv, 0, "NUM_CHAIRPOS_PTS", "2");
    wpa::AddTableData(*f, kEnv, 0, "CHAIRPOS_X", "5");
    wpa::AddTableData(*f, kEnv, 0, "CHAIRPOS_Y", "6");
    wpa::AddTableData(*f, kEnv, 0, "CHAIRPOS_X", "7");
    wpa::AddTableData(*f, kEnv, 0, "CHAIRPOS_Y", "8");
    wpa::AddTableData(*f, kEnv, 0, "CHAIR_DIRECTION", "NE");
    wpa::AddTableData(*f, kEnv, 0, "CHAIR_DIRECTION", "NW");
    OBL_CHECK(g_fatals.messages.empty());
    const auto& t = f->m_tables[0];
    OBL_CHECK_EQ(t.m_NumWalkPts, 2);
    OBL_CHECK_EQ(t.m_NumChairPos, 2);
    OBL_CHECK_EQ(t.m_NumChairDirs, 2);
    OBL_CHECK_EQ(t.m_walkPoints[0][0], 1.5f);
    OBL_CHECK_EQ(t.m_walkPoints[0][1], 2.5f);
    OBL_CHECK_EQ(t.m_walkPoints[1][0], 3.5f);
    OBL_CHECK_EQ(t.m_walkPoints[1][1], 4.5f);
    OBL_CHECK_EQ(t.m_chairPositions[0][0], 5.0f);
    OBL_CHECK_EQ(t.m_chairPositions[1][1], 8.0f);
    OBL_CHECK_EQ(t.m_chairDirections[0], 1);
    OBL_CHECK_EQ(t.m_chairDirections[1], 7);
    static const char* const kDirections[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    Fixture g;
    wpa::AddTableData(*g, kEnv, 0, "CAPACITY", "8");
    for (const char* direction : kDirections) wpa::AddTableData(*g, kEnv, 0, "CHAIR_DIRECTION", direction);
    for (int i = 0; i < 8; ++i) OBL_CHECK_EQ(g->m_tables[0].m_chairDirections[i], i);
    OBL_CHECK(g_fatals.messages.empty());
    wpa::AddTableData(*g, kEnv, 0, "CHAIR_DIRECTION", "UP");
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(1));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Invalid direction in table - UP"));
}

OBL_TEST_CASE("world_params_add.table_chair_walk_lists_and_anims")
{
    Fixture f;
    wpa::AddTableData(*f, kEnv, 0, "CAPACITY", "2");
    wpa::AddTableData(*f, kEnv, 0, "CHAIR_NUM_WALKPNT", "2");
    wpa::AddTableData(*f, kEnv, 0, "CHAIR_WALKPNT", "11");
    wpa::AddTableData(*f, kEnv, 0, "CHAIR_WALKPNT", "12");
    wpa::AddTableData(*f, kEnv, 0, "CHAIR_NUM_WALKPNT", "1");
    wpa::AddTableData(*f, kEnv, 0, "CHAIR_WALKPNT", "21");
    const auto& t = f->m_tables[0];
    OBL_CHECK_EQ(t.m_NumChairWalkLists, 2);
    OBL_CHECK_EQ(t.m_ChairWalkIdx, 2); // the fill position of the current list
    OBL_CHECK(t.m_chairWalkPoints[0] == std::vector<int>({2, 11, 12}));
    OBL_CHECK(t.m_chairWalkPoints[1] == std::vector<int>({1, 21}));
    wpa::AddTableData(*f, kEnv, 0, "NUM_SAT_DOWN_ANIM", "2");
    wpa::AddTableData(*f, kEnv, 0, "SAT_DOWN_ANIM", "sat0");
    wpa::AddTableData(*f, kEnv, 0, "SAT_DOWN_ANIM", "sat1");
    OBL_CHECK(t.m_satDownAnims == std::vector<std::string>({"sat0", "sat1"}));
    OBL_CHECK_EQ(t.m_NumSatDownAnims, 2);
    wpa::AddTableData(*f, kEnv, 0, "WAITER_SERVE_POINT_X", "1");
    wpa::AddTableData(*f, kEnv, 0, "WAITER_SERVE_POINT_Y", "2");
    wpa::AddTableData(*f, kEnv, 0, "WAITER_SERVE_POINT_X", "3");
    wpa::AddTableData(*f, kEnv, 0, "WAITER_SERVE_POINT_Y", "4");
    OBL_CHECK_EQ(t.m_servePoints[0][0], 1.0f);
    OBL_CHECK_EQ(t.m_servePoints[0][1], 2.0f);
    OBL_CHECK_EQ(t.m_servePoints[1][0], 3.0f);
    OBL_CHECK_EQ(t.m_servePoints[1][1], 4.0f);
    OBL_CHECK_EQ(t.m_NumServePoints, 2);
    wpa::AddTableData(*f, kEnv, 0, "LEGS", "4");
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(1));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Unrecognized table field \"LEGS\" in the gameplay data"));
}

namespace {

// A world with the definitions the building keywords look up.
void FillLookups(Fixture& f)
{
    auto key = [](auto& destination, int id, const char* text) {
        wpa::detail::Copy(destination, text);
        (void)id;
    };
    f->m_openingHours.resize(2);
    f->m_numOpeningHours = 2;
    for (int i = 0; i < 2; ++i) {
        f->m_openingHours[i].m_id = 10 + i;
        key(f->m_openingHours[i].m_key, i, i == 0 ? "HOURS_A" : "HOURS_B");
    }
    f->m_commodityFamilies.resize(2);
    f->m_numCommodityFamilies = 2;
    for (int i = 0; i < 2; ++i) {
        f->m_commodityFamilies[i].m_id = 20 + i;
        key(f->m_commodityFamilies[i].m_key, i, i == 0 ? "FAM_A" : "FAM_B");
    }
    f->m_commodities.resize(3);
    f->m_numCommodities = 3;
    for (int i = 0; i < 3; ++i) {
        f->m_commodities[i].m_id = 30 + i;
        key(f->m_commodities[i].m_key, i, i == 0 ? "COM_A" : i == 1 ? "COM_B" : "COM_C");
    }
    f->m_staff.resize(2);
    f->m_numStaff = 2;
    for (int i = 0; i < 2; ++i) {
        f->m_staff[i].m_id = 40 + i;
        key(f->m_staff[i].m_key, i, i == 0 ? "STAFF_A" : "STAFF_B");
    }
    f->m_vehicles.resize(2);
    f->m_numVehicles = 2;
    for (int i = 0; i < 2; ++i) {
        f->m_vehicles[i].m_id = 50 + i;
        key(f->m_vehicles[i].m_key, i, i == 0 ? "VEH_A" : "VEH_B");
    }
    f->m_tables.resize(2);
    f->m_numTables = 2;
    for (int i = 0; i < 2; ++i) {
        f->m_tables[i].m_id = 60 + i;
        key(f->m_tables[i].m_key, i, i == 0 ? "TAB_A" : "TAB_B");
    }
    f->m_events.resize(2);
    f->m_numEvents = 2;
    for (int i = 0; i < 2; ++i) {
        f->m_events[i].m_id = 70 + i;
        key(f->m_events[i].m_key, i, i == 0 ? "EVT_A" : "EVT_B");
    }
}

} // namespace

OBL_TEST_CASE("world_params_add.building_scalars_and_strings_land_in_the_fields")
{
    Fixture f;
    FillLookups(f);
    char buffer[64];
    auto add = [&](const char* keyword, const char* value) {
        std::snprintf(buffer, sizeof(buffer), "%s", value);
        wpa::AddBuildingData(*f, kEnv, 4, keyword, buffer);
    };
    add("NAME", "Hotel");
    add("SPRITE", "hotel.spr");
    add("ICON", "3");
    add("CATEGORY", "Leisure");
    add("COST2BUILD", "1000");
    add("REFUNDVALUE", "400");
    add("GRIDWIDTH", "5");
    add("GRIDHEIGHT", "6");
    add("GUESTCAPACITY", "20");
    add("BUILDTIME", "7");
    add("DEMOLISHTIME", "8");
    add("OPENINGHOURS", "HOURS_B");
    add("MAXINSTANCES", "2");
    add("ATTRACTOR", "FAM_B");
    add("MAX_QUEUE", "9");
    add("MAXSTAFF", "3");
    add("TREE", "HOTEL_TREE");
    add("MENU", "HOTEL_MENU");
    add("XSERVEOFFSET", "-5");
    add("YSERVEOFFSET", "6");
    add("STAFF_XOFFSET", "7");
    add("STAFF_YOFFSET", "-8");
    add("XENTRANCEOFFSET", "9");
    add("YENTRANCEOFFSET", "-10");
    add("BUILDON", "LAND");
    add("BUILDON", "WATER");
    add("CONSTRUCT", "custom_construct");
    add("POWER", "-20");
    add("AVFAILTIME", "2.5");
    add("FAIL_PERCENT", "11");
    add("FAIL_AGE", "12");
    add("MALEFILENAME", "male.spr");
    add("FEMALEFILENAME", "female.spr");
    add("MAXBUILDERS", "4");
    OBL_CHECK(g_fatals.messages.empty());
    OBL_CHECK_EQ(std::string(f->m_buildings[4].m_Name), std::string("Hotel"));
    OBL_CHECK_EQ(std::string(f->m_buildings[4].m_Sprite), std::string("hotel.spr"));
    OBL_CHECK_EQ(f->m_buildings[4].m_Icon, 3);
    OBL_CHECK_EQ(f->m_buildings[4].m_Category, 2);
    OBL_CHECK_EQ(f->m_buildings[4].m_Cost2Build, 1000);
    OBL_CHECK_EQ(f->m_buildings[4].m_RefundValue, 400);
    OBL_CHECK_EQ(f->m_buildings[4].m_GridWidth, 5);
    OBL_CHECK_EQ(f->m_buildings[4].m_GridHeight, 6);
    OBL_CHECK_EQ(f->m_buildings[4].m_GuestCapacity, 20);
    OBL_CHECK_EQ(f->m_buildings[4].m_BuildTime, 7);
    OBL_CHECK_EQ(f->m_buildings[4].m_DemolishTime, 8);
    OBL_CHECK_EQ(f->m_buildings[4].m_OpeningHours, 11); // the opening-hours id, not the index
    OBL_CHECK_EQ(f->m_buildings[4].m_MaxInstances, 2);
    OBL_CHECK_EQ(f->m_buildings[4].m_Attractor, 21);
    OBL_CHECK_EQ(f->m_buildings[4].m_MaxQueue, 9);
    OBL_CHECK_EQ(f->m_buildings[4].m_MaxStaff, 3);
    OBL_CHECK_EQ(std::string(f->m_buildings[4].m_Tree), std::string("HOTEL_TREE"));
    OBL_CHECK_EQ(std::string(f->m_buildings[4].m_Menu), std::string("HOTEL_MENU"));
    OBL_CHECK_EQ(f->m_buildings[4].m_ServeOffset[0], static_cast<int16_t>(-5));
    OBL_CHECK_EQ(f->m_buildings[4].m_ServeOffset[1], static_cast<int16_t>(6));
    OBL_CHECK_EQ(f->m_buildings[4].m_StaffOffset[0], static_cast<int16_t>(7));
    OBL_CHECK_EQ(f->m_buildings[4].m_StaffOffset[1], static_cast<int16_t>(-8));
    OBL_CHECK_EQ(f->m_buildings[4].m_EntranceOffset[0], static_cast<int16_t>(9));
    OBL_CHECK_EQ(f->m_buildings[4].m_EntranceOffset[1], static_cast<int16_t>(-10));
    OBL_CHECK_EQ(f->m_buildings[4].m_BuildOn, 1u | 0xcu);
    OBL_CHECK_EQ(std::string(f->m_buildings[4].m_Construct), std::string("custom_construct"));
    OBL_CHECK_EQ(f->m_buildings[4].m_customConstruct, static_cast<uint8_t>(1));
    OBL_CHECK_EQ(f->m_buildings[4].m_Power, -20);
    OBL_CHECK_EQ(f->m_buildings[4].m_AvFailTime, 2.5f);
    OBL_CHECK_EQ(f->m_buildings[4].m_FailPercent, 11);
    OBL_CHECK_EQ(f->m_buildings[4].m_FailAge, 12);
    OBL_CHECK_EQ(std::string(f->m_buildings[4].m_MaleFileName), std::string("male.spr"));
    OBL_CHECK_EQ(std::string(f->m_buildings[4].m_FemaleFileName), std::string("female.spr"));
    OBL_CHECK_EQ(f->m_buildings[4].m_MaxBuilders, 4.0f);
    add("CATEGORY", "Hotel");
    OBL_CHECK_EQ(f->m_buildings[4].m_Category, 0);
    add("CATEGORY", "Food");
    OBL_CHECK_EQ(f->m_buildings[4].m_Category, 1);
    add("CATEGORY", "Other");
    OBL_CHECK_EQ(f->m_buildings[4].m_Category, 3);
    add("CATEGORY", "Mystery"); // ignored, no FatalError
    OBL_CHECK_EQ(f->m_buildings[4].m_Category, 3);
    OBL_CHECK(g_fatals.messages.empty());
}

OBL_TEST_CASE("world_params_add.building_resets_the_default_beer_on_every_line")
{
    Fixture f;
    char value[] = "1";
    f->m_buildings.resize(5);
    f->m_buildings[4].m_DefaultBeer = 7;
    wpa::AddBuildingData(*f, kEnv, 4, "ICON", value);
    OBL_CHECK_EQ(f->m_buildings[4].m_DefaultBeer, 0);
}

OBL_TEST_CASE("world_params_add.building_buildon_masks_and_errors")
{
    Fixture f;
    char value[32];
    auto add = [&](const char* keyword, const char* text) {
        std::snprintf(value, sizeof(value), "%s", text);
        wpa::AddBuildingData(*f, kEnv, 4, keyword, value);
    };
    add("BUILDON", "SHALLOW_WATER");
    add("BUILDON", "DEEP_SEA");
    add("BUILDON", "BEACH");
    OBL_CHECK_EQ(f->m_buildings[4].m_BuildOn, 4u | 8u | 2u);
    OBL_CHECK(g_fatals.messages.empty());
    add("BUILDON", "PATH"); // a building has no PATH
    add("OPENINGHOURS", "HOURS_X");
    add("ATTRACTOR", "FAM_X");
    add("STAFF", "NOBODY");
    add("VEHICLE", "NOTHING");
    add("TABLE", "NOWHERE");
    add("EVENT", "NOSHOW");
    std::vector<char> commodity = Quoted({"COM_X", "1", "2", "3", "4", "5", "6"});
    wpa::AddBuildingData(*f, kEnv, 4, "COMMODITY", commodity.data());
    add("ROOF", "red");
    OBL_CHECK_EQ(g_fatals.messages.size(), static_cast<size_t>(9));
    OBL_CHECK_EQ(g_fatals.messages[0], std::string("Unrecognized building ground type \"PATH\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[1], std::string("Unrecognized opening hours \"HOURS_X\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[2], std::string("Unrecognized commodity family \"FAM_X\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[3], std::string("Unrecognized staff type \"NOBODY\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[4], std::string("Unrecognized vehicle type \"NOTHING\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[5], std::string("Unrecognized table type \"NOWHERE\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[6], std::string("Unrecognized event \"NOSHOW\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[7], std::string("Unrecognized commodity \"COM_X\" in the gameplay data"));
    OBL_CHECK_EQ(g_fatals.messages[8], std::string("Unrecognized building field \"ROOF\" in the gameplay data"));
}

OBL_TEST_CASE("world_params_add.building_lists_with_running_counts")
{
    Fixture f;
    FillLookups(f);
    char value[32];
    auto add = [&](const char* keyword, const char* text) {
        std::snprintf(value, sizeof(value), "%s", text);
        wpa::AddBuildingData(*f, kEnv, 4, keyword, value);
    };
    add("STAFF", "STAFF_B");
    add("STAFF", "STAFF_A");
    OBL_CHECK_EQ(f->m_buildings[4].m_NumStaff, 2);
    OBL_CHECK_EQ(f->m_buildings[4].m_Staff[0], 41); // StaffDef ids
    OBL_CHECK_EQ(f->m_buildings[4].m_Staff[1], 40);
    add("VEHICLE", "VEH_B");
    OBL_CHECK_EQ(f->m_buildings[4].m_NumVehicles, 1);
    OBL_CHECK_EQ(f->m_buildings[4].m_Vehicle[0], 51);
    add("VEHICLE_DIR", "3");
    add("VEHICLE_GRID_XOFFSET", "4");
    add("VEHICLE_GRID_YOFFSET", "5"); // closes place 0
    add("VEHICLE_DIR", "6");
    add("VEHICLE_GRID_XOFFSET", "7");
    add("VEHICLE_GRID_YOFFSET", "8");
    OBL_CHECK_EQ(f->m_buildings[4].m_NumVehiclePlaces, 2);
    OBL_CHECK_EQ(f->m_buildings[4].m_VehicleDir[0], 3);
    OBL_CHECK_EQ(f->m_buildings[4].m_VehicleDir[1], 6);
    OBL_CHECK_EQ(f->m_buildings[4].m_VehicleGridOffset[0][0], 4);
    OBL_CHECK_EQ(f->m_buildings[4].m_VehicleGridOffset[0][1], 5);
    OBL_CHECK_EQ(f->m_buildings[4].m_VehicleGridOffset[1][0], 7);
    OBL_CHECK_EQ(f->m_buildings[4].m_VehicleGridOffset[1][1], 8);
    add("TABLE", "TAB_B");
    add("TABLE_POSX", "1.5");
    add("TABLE_POSY", "2.5"); // closes table 0
    add("TABLE", "TAB_A");
    add("TABLE_POSX", "3.5");
    add("TABLE_POSY", "4.5");
    OBL_CHECK_EQ(f->m_buildings[4].m_NumTables, 2);
    OBL_CHECK_EQ(f->m_buildings[4].m_Table[0], 61);
    OBL_CHECK_EQ(f->m_buildings[4].m_Table[1], 60);
    OBL_CHECK_EQ(f->m_buildings[4].m_TablePos[0][0], 1.5f);
    OBL_CHECK_EQ(f->m_buildings[4].m_TablePos[0][1], 2.5f);
    OBL_CHECK_EQ(f->m_buildings[4].m_TablePos[1][0], 3.5f);
    OBL_CHECK_EQ(f->m_buildings[4].m_TablePos[1][1], 4.5f);
    OBL_CHECK(g_fatals.messages.empty());
}

OBL_TEST_CASE("world_params_add.building_event_id_lands_at_the_global_event_count")
{
    Fixture f;
    FillLookups(f);
    char show[] = "EVT_B";
    wpa::AddBuildingData(*f, kEnv, 4, "EVENT", show);
    OBL_CHECK_EQ(f->m_buildings[4].m_Event[2], 71); // index 2 = the world's event count, not the building's
    OBL_CHECK_EQ(f->m_buildings[4].m_Event[0], 0);
    OBL_CHECK_EQ(f->m_buildings[4].m_NumEvents, 1); // the building's own count still grows
    // With five events in the world the index is 5 = m_NumEvents itself: it ends as id + 1 and m_Event stays empty.
    Fixture g;
    FillLookups(g);
    g->m_events.resize(5);
    g->m_numEvents = 5;
    for (int i = 2; i < 5; ++i) {
        g->m_events[i].m_id = i;
        g->m_events[i].m_key = "EVT_" + std::to_string(i);
    }
    char stage[] = "EVT_2";
    wpa::AddBuildingData(*g, kEnv, 4, "EVENT", stage);
    OBL_CHECK_EQ(g->m_buildings[4].m_NumEvents, 3);
    for (int i = 0; i < 5; ++i) OBL_CHECK_EQ(g->m_buildings[4].m_Event[i], 0);
    OBL_CHECK(g_fatals.messages.empty());
}

OBL_TEST_CASE("world_params_add.building_event_past_the_slots_only_counts")
{
    // The copies with a sixth world event load: its index lies past m_NumEvents, so only the building's count grows.
    Fixture f;
    FillLookups(f);
    f->m_events.resize(6);
    f->m_numEvents = 6;
    f->m_events[5].m_id = 5;
    f->m_events[5].m_key = "EVT_5";
    char show[] = "EVT_5";
    wpa::AddBuildingData(*f, kEnv, 4, "EVENT", show);
    OBL_CHECK(g_fatals.messages.empty());
    OBL_CHECK_EQ(f->m_buildings[4].m_NumEvents, 1);
    for (int i = 0; i < 5; ++i) OBL_CHECK_EQ(f->m_buildings[4].m_Event[i], 0);
}

OBL_TEST_CASE("world_params_add.building_commodity_splits_the_value_string_in_place")
{
    Fixture f;
    FillLookups(f);
    std::vector<char> line = Quoted({"COM_B", "0.5", "0.25", "1", "125", "2", "85"});
    wpa::AddBuildingData(*f, kEnv, 4, "COMMODITY", line.data());
    OBL_CHECK(g_fatals.messages.empty());
    OBL_CHECK_EQ(f->m_buildings[4].m_NumCommodities, 1);
    OBL_CHECK_EQ(f->m_buildings[4].m_Commodity[0], 31); // CommodityDef id
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityAB[0][0], 0.5f);
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityAB[0][1], 0.25f);
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityC[0], 1.0f); // c
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityCost[0], 125.0f); // d, the initial price
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityE[0], 2.0f); // e
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityF[0], 85.0f); // f, the cost
    for (char c : line) OBL_CHECK(c != '"'); // every opening quote became NUL
    std::vector<char> second = Quoted({"COM_C", "1", "2", "3", "4", "5", "6"});
    wpa::AddBuildingData(*f, kEnv, 4, "COMMODITY", second.data());
    OBL_CHECK_EQ(f->m_buildings[4].m_NumCommodities, 2);
    OBL_CHECK_EQ(f->m_buildings[4].m_Commodity[1], 32);
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityAB[1][0], 1.0f);
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityAB[1][1], 2.0f);
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityC[1], 3.0f);
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityCost[1], 4.0f);
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityE[1], 5.0f);
    OBL_CHECK_EQ(f->m_buildings[4].m_CommodityF[1], 6.0f);
}

OBL_TEST_CASE("world_params_add.building_animsprite_interleaves_the_coordinates")
{
    Fixture f;
    std::vector<char> line = Quoted({"anim.spr", "10", "20", "30", "40", "50", "60", "70", "80", "1"});
    wpa::AddBuildingData(*f, kEnv, 4, "ANIMSPRITE", line.data());
    std::vector<char> next = Quoted({"anim2.spr", "1", "2", "3", "4", "5", "6", "7", "8", "0"});
    wpa::AddBuildingData(*f, kEnv, 4, "ANIMSPRITE", next.data());
    OBL_CHECK(g_fatals.messages.empty());
    const auto& building = f->m_buildings[4];
    OBL_CHECK_EQ(building.m_NumAnimSprites, 2);
    // File order x0 y0 x1 y1 x2 y2 x3 y3 lands as x0 x1 x2 x3 then y0 y1 y2 y3.
    const auto& first = building.m_AnimSprites[0];
    OBL_CHECK_EQ(std::string(first.m_Sprite), std::string("anim.spr"));
    OBL_CHECK_EQ(first.m_X[0], 10);
    OBL_CHECK_EQ(first.m_X[1], 30);
    OBL_CHECK_EQ(first.m_X[2], 50);
    OBL_CHECK_EQ(first.m_X[3], 70);
    OBL_CHECK_EQ(first.m_Y[0], 20);
    OBL_CHECK_EQ(first.m_Y[1], 40);
    OBL_CHECK_EQ(first.m_Y[2], 60);
    OBL_CHECK_EQ(first.m_Y[3], 80);
    OBL_CHECK_EQ(first.m_animated, static_cast<uint8_t>(1));
    const auto& second = building.m_AnimSprites[1];
    OBL_CHECK_EQ(std::string(second.m_Sprite), std::string("anim2.spr"));
    OBL_CHECK_EQ(second.m_X[0], 1);
    OBL_CHECK_EQ(second.m_Y[3], 8);
    OBL_CHECK_EQ(second.m_animated, static_cast<uint8_t>(0));
    for (char c : line) OBL_CHECK(c != '"');
}

OBL_TEST_CASE("world_params_add.building_walkpoints_and_baseplates_allocate_by_count")
{
    Fixture f;
    char value[16];
    auto add = [&](const char* keyword, const char* text) {
        std::snprintf(value, sizeof(value), "%s", text);
        wpa::AddBuildingData(*f, kEnv, 4, keyword, value);
    };
    add("NUM_WALKPOINTS", "2");
    add("WALKPOINTX", "1.5");
    add("WALKPOINTY", "2.5");
    add("WALKPOINTX", "3.5");
    add("WALKPOINTY", "4.5");
    OBL_CHECK_EQ(f->m_buildings[4].m_NumWalkPoints, 2);
    const auto& walk = f->m_buildings[4].m_walkPoints;
    OBL_CHECK_EQ(walk[0][0], 1.5f);
    OBL_CHECK_EQ(walk[0][1], 2.5f);
    OBL_CHECK_EQ(walk[1][0], 3.5f);
    OBL_CHECK_EQ(walk[1][1], 4.5f);
    add("NUM_BASEPLATES", "3");
    add("BASEPLATETYPE", "7");
    add("BASEPLATETYPE", "8");
    OBL_CHECK_EQ(f->m_buildings[4].m_NumBasePlates, 2);
    const auto& plates = f->m_buildings[4].m_basePlateTypes;
    OBL_CHECK_EQ(plates[0], 7);
    OBL_CHECK_EQ(plates[1], 8);
    // A zero count resets the cursor and empties the list (the vector keeps no allocation of its own).
    add("NUM_WALKPOINTS", "0");
    OBL_CHECK_EQ(f->m_buildings[4].m_NumWalkPoints, 0);
    OBL_CHECK(f->m_buildings[4].m_walkPoints.empty());
}
