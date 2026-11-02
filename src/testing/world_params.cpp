// worldparams-dump: writes the WorldParams definitions the game has loaded, read only through
// world_params_tables.h, as JSON. tools/check_worldparams_dump.py parses BuildingList.ble and the guest list the
// way the world parameter parser does and compares every field, so a wrong field fails the test.
// Values are raw (ids, masks, enum numbers); the checker resolves keys to ids itself.

#include "core/char16.h"
#include "world_params.h"
#include "game/world/world_params_queries.h"

#include "core/log.h"
#include "test_util.h"
#include "core/wide_path.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace obl::test {
namespace {

using namespace world_data;
using namespace tables;

std::filesystem::path g_file;

// Minimal JSON writer: objects/arrays with comma handling. Strings are the game's single-byte text; bytes >= 0x80
// are written as \u00XX (the checker re-encodes the cp1252 file text to compare byte for byte).
class Json {
public:
    Json& Key(const char* key)
    {
        Separator();
        String(key);
        m_out << ':';
        m_pendingValue = true;
        return *this;
    }
    Json& Begin(char bracket)
    {
        Separator();
        m_out << bracket;
        m_first = true;
        return *this;
    }
    Json& End(char bracket)
    {
        m_out << bracket;
        m_first = false;
        return *this;
    }
    Json& Int(long long value)
    {
        Separator();
        m_out << value;
        return *this;
    }
    Json& Float(float value)
    {
        Separator();
        char text[32];
        std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
        m_out << text;
        return *this;
    }
    Json& Text(const char* text, size_t capacity)
    {
        Separator();
        String(std::string(text, strnlen(text, capacity)).c_str());
        return *this;
    }
    template<size_t N> Json& Text(const char (&text)[N]) { return Text(text, N); }
    Json& Text(const std::string& text) { return Text(text.c_str(), text.size()); }
    std::string Str() const { return m_out.str(); }

private:
    void Separator()
    {
        if (m_pendingValue) {
            m_pendingValue = false;
            return;
        }
        if (!m_first) m_out << ',';
        m_first = false;
    }
    void String(const char* text)
    {
        m_out << '"';
        for (const unsigned char* c = reinterpret_cast<const unsigned char*>(text); *c; ++c) {
            if (*c == '"' || *c == '\\')
                m_out << '\\' << *c;
            else if (*c < 0x20 || *c >= 0x80) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", *c);
                m_out << escaped;
            } else
                m_out << *c;
        }
        m_out << '"';
    }

    std::ostringstream m_out;
    bool m_first = true;
    bool m_pendingValue = false;
};

template<class T> void Ints(Json& json, const char* key, const T* values, int count)
{
    json.Key(key).Begin('[');
    for (int i = 0; i < count; ++i) json.Int(values[i]);
    json.End(']');
}

void FloatPairs(Json& json, const char* key, const std::array<float, 2>* pairs, int count)
{
    json.Key(key).Begin('[');
    for (int i = 0; i < count; ++i) json.Begin('[').Float(pairs[i][0]).Float(pairs[i][1]).End(']');
    json.End(']');
}

// Counts outside the fixed arrays mean a corrupt table (or a wrong count offset): stop instead of reading past them.
bool CountOk(const char* table, int count, int max)
{
    if (count >= 0 && count <= max) return true;
    OBL_LOG("autotest: FAILED worldparams-dump %s count %d outside 0..%d", table, count, max);
    return false;
}

void BuildingFields(Json& json, const BuildingDef& d)
{
    json.Key("NAME").Text(d.m_Name).Key("SPRITE").Text(d.m_Sprite).Key("ICON").Int(d.m_Icon);
    json.Key("CATEGORY").Int(d.m_Category).Key("COST2BUILD").Int(d.m_Cost2Build).Key("REFUNDVALUE").Int(d.m_RefundValue);
    json.Key("GRIDWIDTH").Int(d.m_GridWidth).Key("GRIDHEIGHT").Int(d.m_GridHeight);
    json.Key("GUESTCAPACITY").Int(d.m_GuestCapacity).Key("BUILDTIME").Int(d.m_BuildTime);
    json.Key("DEMOLISHTIME").Int(d.m_DemolishTime).Key("OPENINGHOURS").Int(d.m_OpeningHours);
    json.Key("MAXINSTANCES").Int(d.m_MaxInstances).Key("ATTRACTOR").Int(d.m_Attractor).Key("MAX_QUEUE").Int(d.m_MaxQueue);
    json.Key("COMMODITY").Begin('[');
    for (int i = 0; i < d.m_NumCommodities && i < 5; ++i)
        json.Begin('[')
                .Int(d.m_Commodity[i])
                .Float(d.m_CommodityAB[i][0])
                .Float(d.m_CommodityAB[i][1])
                .Float(d.m_CommodityC[i])
                .Float(d.m_CommodityCost[i])
                .Float(d.m_CommodityE[i])
                .Float(d.m_CommodityF[i])
                .End(']');
    json.End(']');
    json.Key("MAXSTAFF").Int(d.m_MaxStaff);
    Ints(json, "STAFF", d.m_Staff, std::min(d.m_NumStaff, 10));
    Ints(json, "VEHICLE", d.m_Vehicle.data(), std::min(d.m_NumVehicles, 5));
    json.Key("VEHICLE_PLACES").Int(d.m_NumVehiclePlaces);
    json.Key("VEHICLE_GRID").Begin('[');
    for (const auto& offset : d.m_VehicleGridOffset) json.Begin('[').Int(offset[0]).Int(offset[1]).End(']');
    json.End(']');
    Ints(json, "VEHICLE_DIR", d.m_VehicleDir.data(), 5);
    Ints(json, "SERVEOFFSET", d.m_ServeOffset, 2);
    Ints(json, "STAFF_OFFSET", d.m_StaffOffset, 2);
    Ints(json, "ENTRANCEOFFSET", d.m_EntranceOffset, 2);
    json.Key("BUILDON").Int(d.m_BuildOn).Key("TREE").Text(d.m_Tree).Key("MENU").Text(d.m_Menu);
    json.Key("ANIMSPRITE").Begin('[');
    for (int i = 0; i < d.m_NumAnimSprites && i < 8; ++i) json.Text(d.m_AnimSprites[i].m_Sprite);
    json.End(']');
    json.Key("ANIMSPRITE_DATA").Begin('[');
    for (int i = 0; i < d.m_NumAnimSprites && i < 8; ++i) {
        const auto& sprite = d.m_AnimSprites[i];
        json.Begin('[');
        for (int c = 0; c < 4; ++c) json.Int(sprite.m_X[c]).Int(sprite.m_Y[c]);
        json.Int(sprite.m_animated).End(']');
    }
    json.End(']');
    Ints(json, "EVENT", d.m_Event.data(), std::min(d.m_NumEvents, 5));
    json.Key("CUSTOM_CONSTRUCT").Int(d.m_customConstruct).Key("CONSTRUCT").Text(d.m_Construct);
    json.Key("POWER").Int(d.m_Power).Key("AVFAILTIME").Float(d.m_AvFailTime);
    json.Key("FAIL_PERCENT").Int(d.m_FailPercent).Key("FAIL_AGE").Int(d.m_FailAge);
    json.Key("MALEFILENAME").Text(d.m_MaleFileName).Key("FEMALEFILENAME").Text(d.m_FemaleFileName);
    json.Key("WALKPOINTS").Begin('[');
    for (int i = 0; i < d.m_NumWalkPoints; ++i) json.Begin('[').Float(d.m_walkPoints[i][0]).Float(d.m_walkPoints[i][1]).End(']');
    json.End(']');
    json.Key("NUM_BASEPLATES").Int(d.m_NumBasePlates);
    Ints(json, "BASEPLATETYPE", d.m_basePlateTypes.data(), d.m_basePlateTypes.empty() ? 0 : d.m_NumBasePlates);
    json.Key("MAXBUILDERS").Float(d.m_MaxBuilders);
    json.Key("TABLE").Begin('[');
    for (int i = 0; i < d.m_NumTables && i < 10; ++i)
        json.Begin('[').Int(d.m_Table[i]).Float(d.m_TablePos[i][0]).Float(d.m_TablePos[i][1]).End(']');
    json.End(']');
    json.Key("FOOTPRINT").Int(d.m_footprint != nullptr).Key("DEFAULT_BEER").Int(d.m_DefaultBeer);
}

void GuestFields(Json& json, const tables::GuestDef& d)
{
    static const char* const kPrefs[] = {"ALCOHOL", "AMUSEMENT", "MEALS", "PARTYFUN", "SNACKS", "SOFTDRINKS", "SOUVENIRS", "SPORTSFUN",
            "SUNBATHE", "SWIMMING", "WATERFUN", "STROLL", "SLEEP"};
    static const char* const kOpinions[] = {"BEAUTY", "CROWDING", "TOTTY", "PRICE", "VARIETY", "CLEANLINESS", "SAFETY"};
    json.Key("NAME").Text(d.m_Name).Key("SPRITE").Text(d.m_Sprite).Key("GENDER").Int(d.m_Gender);
    json.Key("MONEY").Float(d.m_Money).Key("SWIM_ENERGY").Float(d.m_SwimEnergy).Key("SWIM_DIST").Float(d.m_SwimDist);
    json.Key("ATTRACTIVE").Float(d.m_Attractive).Key("DEF_SPRITE").Int(d.m_DefSprite).Key("DRUNKTYPE").Int(d.m_DrunkType);
    json.Key("ATTRACTIVE_PREF").Float(d.m_AttractivePref).Key("COLOUR").Int(d.m_Colour);
    json.Key("NIGHTS_MIN").Int(d.m_NightsMin).Key("NIGHTS_MAX").Int(d.m_NightsMax).Key("LITTER").Float(d.m_Litter);
    json.Key("DRUNKDECAY").Float(d.m_DrunkDecay).Key("FIGHTDECAY").Float(d.m_FightDecay);
    json.Key("FLIRTDECAY").Float(d.m_FlirtDecay).Key("VANDALISEDECAY").Float(d.m_VandaliseDecay);
    for (int i = 0; i < 13; ++i) FloatPairs(json, (std::string(kPrefs[i]) + "PREF").c_str(), &d.m_Pref[i], 1);
    for (int i = 0; i < 7; ++i) {
        FloatPairs(json, (std::string(kOpinions[i]) + "_START").c_str(), &d.m_OpinionStart[i], 1);
        FloatPairs(json, (std::string(kOpinions[i]) + "_EFFECT").c_str(), &d.m_OpinionEffect[i], 1);
        FloatPairs(json, (std::string(kOpinions[i]) + "TOL").c_str(), &d.m_OpinionTol[i], 1);
    }
    FloatPairs(json, "SLEEPTIME", &d.m_SleepTime, 1);
    FloatPairs(json, "WAKETIME", &d.m_WakeTime, 1);
    Ints(json, "MAX", d.m_Max.data(), 26);
    json.Key("SUNBURN_TIME").Float(d.m_SunburnTime).Key("ILL_DECAY").Float(d.m_IllDecay);
    json.Key("START_BLADDER").Float(d.m_StartBladder).Key("START_FIGHT").Float(d.m_StartFight);
    json.Key("START_FLIRT").Float(d.m_StartFlirt).Key("START_DRUNK").Float(d.m_StartDrunk);
    json.Key("START_ILL").Float(d.m_StartIll).Key("START_SUNBURN").Float(d.m_StartSunburn);
    json.Key("START_VANDALISE").Float(d.m_StartVandalise);
}

void SceneryFields(Json& json, const SceneryDef& d)
{
    json.Key("NAME").Text(d.m_Name).Key("SPRITE").Text(d.m_Sprite).Key("ANIM").Text(d.m_Anim);
    json.Key("CATEGORY").Int(d.m_Category).Key("GRIDWIDTH").Int(d.m_GridWidth).Key("GRIDHEIGHT").Int(d.m_GridHeight);
    json.Key("COST2BUILD").Int(d.m_Cost2Build).Key("BUILDON").Int(d.m_BuildOn).Key("COLLIDE").Int(d.m_collide);
    json.Key("CAPACITY").Int(d.m_Capacity).Key("MENU").Text(d.m_Menu).Key("ICON").Int(d.m_Icon);
    json.Key("RATING").Float(d.m_Rating).Key("CLEAN").Int(d.m_clean).Key("REPAIR").Int(d.m_repair);
    json.Key("WATER").Int(d.m_water).Key("PATHFLAG").Int(d.m_PathFlag).Key("BURN").Int(d.m_burn);
    json.Key("USERREMOVE").Int(d.m_userRemove).Key("VEGFLAG").Int(d.m_vegFlag);
}

void StaffFields(Json& json, const StaffDef& d)
{
    json.Key("name").Text(d.m_Name).Key("sex").Int(d.m_female).Key("wage").Int(d.m_Wage).Key("sprite").Text(d.m_Sprite);
    json.Key("object").Text(d.m_ObjectType).Key("menu").Text(d.m_Menu).Key("recommended_wage").Int(d.m_RecommendedWage);
    json.Key("scale").Float(d.m_scale).Key("expectation").Float(d.m_expectationGrowth);
    json.Key("initial_happiness").Float(d.m_initialHappiness).Key("start_hour").Int(d.m_StartHour).Key("end_hour").Int(d.m_EndHour);
}

void VehicleFields(Json& json, const tables::VehicleDef& d)
{
    json.Key("NAME").Text(d.m_Name).Key("SPRITE").Text(d.m_Sprite).Key("GRIDWIDTH").Int(d.m_GridWidth);
    json.Key("GRIDHEIGHT").Int(d.m_GridHeight).Key("CAPACITY").Int(d.m_Capacity).Key("FORCE").Float(d.m_Force);
    json.Key("MAX_SPEED").Float(d.m_MaxSpeed).Key("MASS").Float(d.m_Mass).Key("MOVETYPE").Int(d.m_MoveType);
    json.Key("TOWED").Text(d.m_Towed).Key("ANIMSET").Text(d.m_AnimSet).Key("NUM_STAFF").Int(d.m_NumStaff);
    Ints(json, "STAFF", d.m_Staff.data(), std::min(d.m_NumStaff, 2));
    json.Key("CREATION_PNTX").Float(d.m_CreationPoint[0]).Key("CREATION_PNTY").Float(d.m_CreationPoint[1]);
}

void EventFields(Json& json, const EventDef& d)
{
    json.Key("DEF_PRICE").Float(d.m_price).Key("COST").Float(d.m_cost);
    json.Key("MALE_INTENSITY").Float(d.m_intensity[0]).Key("FEMALE_INTENSITY").Float(d.m_intensity[1]);
}

void BeerFields(Json& json, const BeerDef& d)
{
    json.Key("BRAND").Text(d.m_Brand).Key("COCKTAIL").Text(d.m_Cocktail).Key("BASECOST").Float(d.m_BaseCost);
    json.Key("STRCOST").Float(d.m_StrCost).Key("ALCSTR").Float(d.m_AlcStr).Key("PRICE").Float(d.m_Price);
    json.Key("EFFECT").Int(d.m_Effect); // the effect is rewritten by the scenario script (BeerActive<X>): not dumped
}

// A heap string the table keeps by pointer: the text, "" for a null pointer (the file never gave the line).
void HeapText(Json& json, const char* key, const char* text)
{
    json.Key(key).Text(text ? text : "", text ? 0x100 : 1);
}

void TableFields(Json& json, const TableDef& d)
{
    json.Key("NAME").Text(d.m_Name).Key("TABLE_SPRITE").Text(d.m_TableSprite).Key("CHAIR_SPRITE").Text(d.m_ChairSprite);
    json.Key("GRIDWIDTH").Int(d.m_GridWidth).Key("GRIDHEIGHT").Int(d.m_GridHeight).Key("CAPACITY").Int(d.m_NumChairs);
    json.Key("SIT_DOWN_ANIM").Text(d.m_SitDownAnim).Key("GET_UP_ANIM").Text(d.m_GetUpAnim);
    json.Key("SERVE_TABLE").Int(d.m_serveTable).Key("XOFFSET_TABLE").Int(d.m_XOffsetTable).Key("YOFFSET_TABLE").Int(d.m_YOffsetTable);
    json.Key("HAS_WAITER").Int(d.m_hasPlates);
    HeapText(json, "WAITER_SPRITE", d.m_waiterSprite.c_str());
    HeapText(json, "WAITER_WALK_ANIM", d.m_waiterWalkAnim.c_str());
    HeapText(json, "WAITER_SERVE_ANIM", d.m_waiterServeAnim.c_str());
    HeapText(json, "CALL_WAITER_ANIM", d.m_callWaiterAnim.c_str());
    HeapText(json, "WAIT_WAITER_ANIM", d.m_waitWaiterAnim.c_str());
    json.Key("WALK_PNT").Begin('[');
    for (int i = 0; i < d.m_NumWalkPts; ++i) json.Begin('[').Float(d.m_walkPoints[i][0]).Float(d.m_walkPoints[i][1]).End(']');
    json.End(']');
    json.Key("CHAIRPOS").Begin('[');
    for (int i = 0; i < d.m_NumChairPos; ++i) json.Begin('[').Float(d.m_chairPositions[i][0]).Float(d.m_chairPositions[i][1]).End(']');
    json.End(']');
    Ints(json, "CHAIR_DIRECTION", d.m_chairDirections.data(), d.m_chairDirections.empty() ? 0 : d.m_NumChairDirs);
    // Every list is [count, items...]; the last one holds as many items as CHAIR_WALKPNT lines have come so far.
    json.Key("CHAIR_WALKPNT").Begin('[');
    for (int i = 0; i < d.m_NumChairWalkLists; ++i) {
        const std::vector<int>& list = d.m_chairWalkPoints[i];
        const int items = i + 1 == d.m_NumChairWalkLists ? d.m_ChairWalkIdx - 1 : list.empty() ? -1 : list[0];
        json.Begin('[');
        for (int n = 0; n <= items; ++n) json.Int(list[n]);
        json.End(']');
    }
    json.End(']');
    json.Key("SAT_DOWN_ANIM").Begin('[');
    for (int i = 0; i < d.m_NumSatDownAnims; ++i) json.Text(d.m_satDownAnims[i]);
    json.End(']');
    json.Key("WAITER_SERVE_POINT").Begin('[');
    for (int i = 0; i < d.m_NumServePoints; ++i) json.Begin('[').Float(d.m_servePoints[i][0]).Float(d.m_servePoints[i][1]).End(']');
    json.End(']');
}

// Every keyed table: ids are the record index and keys are set within the count.
template<class Def> bool Table(Json& json, const char* name, const Def* defs, int count, int max, void (*fields)(Json&, const Def&))
{
    if (!CountOk(name, count, max)) return false;
    json.Key(name).Begin('[');
    for (int i = 0; i < count; ++i) {
        const Def& def = defs[i];
        if (def.m_id != i || !def.m_key[0]) {
            OBL_LOG("autotest: FAILED worldparams-dump %s[%d] id=%d key='%.64s'", name, i, def.m_id,
                    world_params_queries::KeyText(def.m_key));
            return false;
        }
        json.Begin('{').Key("id").Int(def.m_id).Key("key").Text(def.m_key);
        if (fields) fields(json, def);
        json.End('}');
    }
    json.End(']');
    return true;
}

template<class Def>
bool Table(Json& json, const char* name, const std::vector<Def>& defs, int count, int, void (*fields)(Json&, const Def&))
{
    return Table(json, name, defs.data(), count, static_cast<int>(defs.size()), fields);
}

bool Dump(const WorldParams& p, std::string& out)
{
    Json json;
    json.Begin('{');
    bool ok = Table<CommodityFamilyDef>(json, "commodity_families", p.m_commodityFamilies, p.m_numCommodityFamilies,
                      WorldParams::kMaxCommodityFamilies, [](Json& j, const CommodityFamilyDef& d) { j.Key("name").Text(d.m_name); }) &&
            Table<CommodityDef>(json, "commodities", p.m_commodities, p.m_numCommodities, WorldParams::kMaxCommodities,
                    [](Json& j, const CommodityDef& d) { j.Key("name").Text(d.m_name).Key("family").Int(d.m_family); }) &&
            Table<OpeningHoursDef>(json, "opening_hours", p.m_openingHours, p.m_numOpeningHours, WorldParams::kMaxOpeningHours,
                    [](Json& j, const OpeningHoursDef& d) { j.Key("open").Float(d.m_open).Key("close").Float(d.m_close); }) &&
            Table<BuildingDef>(json, "buildings", p.m_buildings, p.m_numBuildings, WorldParams::kMaxBuildings, BuildingFields) &&
            Table<DesireDef>(json, "desires", p.m_desires, p.m_numDesires, WorldParams::kMaxDesires,
                    [](Json& j, const DesireDef& d) {
                        j.Key("name").Text(d.m_name);
                        Ints(j, "COMFAM", d.m_families.data(), d.m_numFamilies);
                    }) &&
            Table<StaffDef>(json, "staff", p.m_staff, p.m_numStaff, WorldParams::kMaxStaff, StaffFields) &&
            Table<tables::GuestDef>(json, "guests", p.m_guests, p.m_numGuests, WorldParams::kMaxGuests, GuestFields) &&
            Table<tables::VehicleDef>(json, "vehicles", p.m_vehicles, p.m_numVehicles, WorldParams::kMaxVehicles, VehicleFields) &&
            Table<EventDef>(json, "events", p.m_events, p.m_numEvents, WorldParams::kMaxEvents, EventFields) &&
            Table<BeerDef>(json, "beers", p.m_beers, p.m_numBeers, WorldParams::kMaxBeers, BeerFields) &&
            Table<FineDef>(json, "fines", p.m_fines, p.m_numFines, WorldParams::kMaxFines, nullptr) &&
            Table<TableDef>(json, "tables", p.m_tables, p.m_numTables, WorldParams::kMaxTables, TableFields) &&
            Table<SceneryDef>(json, "scenery", p.m_scenery, p.m_numScenery, WorldParams::kMaxScenery, SceneryFields);
    if (!ok) return false;
    // The text-key pools share one array: male names from 0, female from 0x8c, surnames from 0x118.
    constexpr int kPoolStart[3] = {0, 0x8c, 0x118};
    static const char* const kPools[3] = {"MALENAME", "FEMALENAME", "SURNAME"};
    json.Key("names").Begin('{');
    for (int pool = 0; pool < 3; ++pool) {
        const int count = p.m_nameCounts[pool];
        if (!CountOk(kPools[pool], count, WorldParams::kMaxNames - kPoolStart[pool])) return false;
        json.Key(kPools[pool]).Begin('[');
        for (int i = 0; i < count; ++i) json.Text(p.m_names[kPoolStart[pool] + i]);
        json.End(']');
    }
    json.End('}');
    json.End('}');
    out = json.Str();
    return true;
}

} // namespace

void InitWorldParamsDump(const Text16& gameDirectory)
{
    g_file = WidePath(gameDirectory) / "worldparams-dump.json";
}

bool ExecuteWorldParamsCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "worldparams-dump") return false;
    const WorldParams* params = game::GetWorldParams();
    std::string text;
    if (!args.empty() || !params) {
        OBL_LOG("autotest: FAILED worldparams-dump needs a loaded world and no arguments");
    } else if (Dump(*params, text)) {
        std::ofstream file(g_file, std::ios::binary | std::ios::trunc);
        file << text;
        if (!file) {
            OBL_LOG("autotest: FAILED worldparams-dump cannot write %s", g_file.string().c_str());
            return true;
        }
        OBL_LOG("autotest: worldparams-dump buildings=%d guests=%d scenery=%d commodities=%d families=%d hours=%d "
                "desires=%d staff=%d vehicles=%d events=%d beers=%d fines=%d tables=%d bytes=%u",
                params->m_numBuildings, params->m_numGuests, params->m_numScenery, params->m_numCommodities, params->m_numCommodityFamilies,
                params->m_numOpeningHours, params->m_numDesires, params->m_numStaff, params->m_numVehicles, params->m_numEvents,
                params->m_numBeers, params->m_numFines, params->m_numTables, static_cast<unsigned>(text.size()));
    }
    return true;
}

} // namespace obl::test
