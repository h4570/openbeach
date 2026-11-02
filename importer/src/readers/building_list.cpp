
#include "importer/src/readers/building_list.h"

#include "openbeach/importer/byte_source.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace obl::convert {

namespace {

constexpr int kCommoditySlots = 5;
constexpr int kBuildingStaff = 10;
constexpr int kBuildingVehicles = 5;
constexpr int kVehiclePlaces = 5;
constexpr int kBuildingTables = 10;
constexpr int kAnimSprites = 8;
constexpr int kEventSlots = 5;
constexpr int kDesireFamilies = 24;
constexpr int kVehicleStaff = 2;

int ToInt(const std::string& text)
{
    return std::atoi(text.c_str());
}
float ToFloat(const std::string& text)
{
    return static_cast<float>(std::atof(text.c_str()));
}

template<class Record> int FindKey(const std::vector<Record>& records, const std::string& key)
{
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].key == key) return static_cast<int>(i);
    return -1;
}

// A list the engine fills through a counted pointer: NUM_* sets the capacity, X stores into the pending slot, Y stores and
// advances. Writes past the capacity (heap overrun in the engine) are refused.
template<class Point> struct Pending {
    std::vector<Point>* list = nullptr;
    int capacity = -1; // -1: no NUM_* line yet (a null pointer in the engine)
    Point slot{};
};

class Parser {
public:
    Parser(BuildingList& list, std::string& error) : m_list(list), m_error(error) {}

    bool Run(std::string_view text)
    {
        bool quoted = false;
        size_t start = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '"') continue;
            if (!quoted)
                start = i + 1;
            else
                m_tokens.emplace_back(text.substr(start, i - start));
            quoted = !quoted;
        }
        if (quoted) return Fail("odd number of quotes");
        while (m_next < m_tokens.size())
            if (!Line()) return false;
        return Finish();
    }

private:
    bool Fail(const std::string& message)
    {
        m_error = "BuildingList.ble: " + message;
        return false;
    }
    bool Fail(const std::string& message, const std::string& what) { return Fail(message + " - " + what); }

    // The next `count` tokens as the line's values.
    bool Take(size_t count, std::vector<std::string>& values)
    {
        if (m_next + count > m_tokens.size()) return Fail("line ends early", m_keyword + "_" + m_field);
        values.assign(
                m_tokens.begin() + static_cast<std::ptrdiff_t>(m_next), m_tokens.begin() + static_cast<std::ptrdiff_t>(m_next + count));
        m_next += count;
        return true;
    }
    bool Take1(std::string& value)
    {
        std::vector<std::string> values;
        if (!Take(1, values)) return false;
        value = values[0];
        return true;
    }

    template<class Record> bool Lookup(const std::vector<Record>& records, const std::string& key, const char* kind, int& id)
    {
        id = FindKey(records, key);
        if (id < 0) return Fail(std::string(kind) + " not found", key);
        return true;
    }

    template<class Record> bool NewRecord(std::vector<Record>& records)
    {
        if (FindKey(records, m_field) >= 0) return Fail("duplicate key", m_keyword + "_" + m_field);
        records.emplace_back();
        records.back().key = m_field;
        return true;
    }

    bool Line()
    {
        const std::string token = m_tokens[m_next++];
        // The prefix ends at the first '_' unless it is exactly JETTY or SECURITY so far.
        size_t split = std::string::npos;
        for (size_t i = 0; i < token.size(); ++i) {
            if (token[i] != '_') continue;
            const std::string_view prefix(token.data(), i);
            if (prefix == "JETTY" || prefix == "SECURITY") continue;
            split = i;
            break;
        }
        if (split == std::string::npos) return Fail("keyword without '_'", token);
        m_keyword = token.substr(0, split);
        m_field = token.substr(split + 1);
        const std::string& k = m_keyword;
        int id = 0;
        std::string value;
        if (k == "COMFAM") {
            if (!NewRecord(m_list.families) || !Take1(m_list.families.back().name)) return false;
        } else if (k == "COM") {
            std::vector<std::string> v;
            if (!Take(2, v) || !NewRecord(m_list.commodities)) return false;
            m_list.commodities.back().name = v[0];
            return Lookup(m_list.families, v[1], "commodity family", m_list.commodities.back().family);
        } else if (k == "HOURS") {
            std::vector<std::string> v;
            if (!Take(2, v) || !NewRecord(m_list.openingHours)) return false;
            m_list.openingHours.back().open = ToFloat(v[0]);
            m_list.openingHours.back().close = ToFloat(v[1]);
        } else if (k == "BUILD") {
            if (!NewRecord(m_list.buildings)) return false;
            m_walk.emplace_back();
            m_basePlates.emplace_back();
            m_vehiclePlace.emplace_back();
            m_tablePlace.emplace_back();
            m_eventSlots.emplace_back();
        } else if ((id = FindKey(m_list.buildings, k)) >= 0) {
            return BuildingField(id);
        } else if (k == "DESIRE") {
            if (!NewRecord(m_list.desires) || !Take1(m_list.desires.back().name)) return false;
        } else if ((id = FindKey(m_list.desires, k)) >= 0) {
            if (!Take1(value)) return false;
            if (m_field != "COMFAM") return Fail("desire field is not COMFAM", k + "_" + m_field);
            BlDesire& d = m_list.desires[static_cast<size_t>(id)];
            if (d.families.size() >= kDesireFamilies) return Fail("too many families", d.key);
            int family = 0;
            if (!Lookup(m_list.families, value, "commodity family", family)) return false;
            d.families.push_back(family);
        } else if (k == "STAFF") {
            return Staff();
        } else if (k == "VEHICLE") {
            return NewRecord(m_list.vehicles);
        } else if ((id = FindKey(m_list.vehicles, k)) >= 0) {
            return Take1(value) && VehicleField(m_list.vehicles[static_cast<size_t>(id)], value);
        } else if (k == "TABLE") {
            if (!NewRecord(m_list.tables)) return false;
            m_tableState.emplace_back();
        } else if ((id = FindKey(m_list.tables, k)) >= 0) {
            return Take1(value) && TableField(static_cast<size_t>(id), value);
        } else if (k == "EVENT") {
            return NewRecord(m_list.events);
        } else if ((id = FindKey(m_list.events, k)) >= 0) {
            return Take1(value) && EventField(m_list.events[static_cast<size_t>(id)], value);
        } else if (k == "BEER") {
            return NewRecord(m_list.beers);
        } else if ((id = FindKey(m_list.beers, k)) >= 0) {
            return Take1(value) && BeerField(m_list.beers[static_cast<size_t>(id)], value);
        } else if (k == "FINE") {
            return NewRecord(m_list.fines);
        } else if ((id = FindKey(m_list.fines, k)) >= 0) {
            if (!Take1(value)) return false;
            if (m_field != "COST") return Fail("unrecognized fine field", k + "_" + m_field);
            m_list.fines[static_cast<size_t>(id)].cost = ToFloat(value);
        } else if (k == "SCENERY") {
            return NewRecord(m_list.scenery);
        } else if ((id = FindKey(m_list.scenery, k)) >= 0) {
            return Take1(value) && SceneryField(m_list.scenery[static_cast<size_t>(id)], value);
        } else if (k == "GUEST" || k == "SURNAME" || k == "MALENAME" || k == "FEMALENAME") {
            return Fail("guest data is not part of the catalog", token);
        } else {
            return Fail("unrecognized keyword prefix", token);
        }
        return true;
    }

    bool Staff()
    {
        std::vector<std::string> v;
        if (!Take(12, v) || !NewRecord(m_list.staff)) return false;
        BlStaff& s = m_list.staff.back();
        s.name = v[0];
        if (v[1].empty() || (v[1][0] != 'F' && v[1][0] != 'M')) return Fail("sex isn't M or F", s.key);
        s.female = v[1][0] == 'F';
        s.wage = ToInt(v[2]);
        s.sprite = v[3];
        s.script = v[4];
        s.menu = v[5];
        s.recommendedWage = ToInt(v[6]);
        s.scale = ToFloat(v[7]);
        s.expectationGrowth = ToFloat(v[8]);
        s.initialHappiness = ToFloat(v[9]);
        s.startHour = ToInt(v[10]);
        s.endHour = ToInt(v[11]);
        return true;
    }

    bool EventField(BlEvent& e, const std::string& value)
    {
        const std::string& f = m_field;
        if (f == "COST")
            e.cost = ToFloat(value);
        else if (f == "DEF_PRICE")
            e.price = ToFloat(value);
        else if (f == "MALE_INTENSITY")
            e.intensity[0] = ToFloat(value);
        else if (f == "FEMALE_INTENSITY")
            e.intensity[1] = ToFloat(value);
        else
            return Fail("unrecognized event field", e.key + "_" + f);
        return true;
    }

    bool BeerField(BlBeer& b, const std::string& value)
    {
        static const char* const kEffects[] = {"FIGHT", "FLIRT", "VANDAL", "HAPPY", "SAD"};
        const std::string& f = m_field;
        if (f == "BRAND")
            b.brand = value;
        else if (f == "BASECOST")
            b.baseCost = ToFloat(value);
        else if (f == "COCKTAIL")
            b.cocktail = value;
        else if (f == "STRCOST")
            b.strengthCost = ToFloat(value);
        else if (f == "ALCSTR")
            b.alcoholStrength = ToFloat(value);
        else if (f == "PRICE")
            b.price = ToFloat(value);
        else if (f == "EFFECT") {
            for (int i = 0; i < 5; ++i)
                if (value == kEffects[i]) {
                    b.effect = i;
                    return true;
                }
            return Fail("invalid beer effect", value);
        } else
            return Fail("unrecognized beer field", b.key + "_" + f);
        return true;
    }

    bool VehicleField(BlVehicle& v, const std::string& value)
    {
        const std::string& f = m_field;
        if (f == "NAME")
            v.name = value;
        else if (f == "SPRITE")
            v.sprite = value;
        else if (f == "GRIDWIDTH")
            v.width = ToInt(value);
        else if (f == "GRIDHEIGHT")
            v.height = ToInt(value);
        else if (f == "CAPACITY")
            v.capacity = ToInt(value);
        else if (f == "TERRAIN") {
        } else if (f == "MAX_SPEED")
            v.maxSpeed = ToFloat(value);
        else if (f == "TOWED")
            v.towed = value;
        else if (f == "FORCE")
            v.force = ToFloat(value);
        else if (f == "MASS")
            v.mass = ToFloat(value);
        else if (f == "MOVETYPE") {
            if (value == "POINTS")
                v.moveType = 1;
            else if (value == "BOUNDARY")
                v.moveType = 2;
            else
                return Fail("invalid movement type", value);
        } else if (f == "ANIMSET")
            v.animSet = value;
        else if (f == "STAFF") {
            if (v.staff.size() >= kVehicleStaff) return Fail("too many staff", v.key);
            int staff = 0;
            if (!Lookup(m_list.staff, value, "staff", staff)) return false;
            v.staff.push_back(staff);
        } else if (f == "CREATION_PNTX")
            v.creationPoint[0] = ToFloat(value);
        else if (f == "CREATION_PNTY")
            v.creationPoint[1] = ToFloat(value);
        else
            return Fail("unrecognized vehicle field", v.key + "_" + f);
        return true;
    }

    bool SceneryField(BlScenery& s, const std::string& value)
    {
        static const char* const kCategories[] = {"FURNITURE", "PATHS", "PLANTS", "ORNAMENTS", "TREES", "SHRUBS", "BILLBOARD", "OTHER"};
        const std::string& f = m_field;
        auto flag = [&](bool& target) { target = ToInt(value) != 0; };
        if (f == "NAME")
            s.name = value;
        else if (f == "SPRITE")
            s.sprite = value;
        else if (f == "ANIM")
            s.anim = value;
        else if (f == "GRIDWIDTH")
            s.width = ToInt(value);
        else if (f == "GRIDHEIGHT")
            s.height = ToInt(value);
        else if (f == "COST2BUILD")
            s.cost = ToInt(value);
        else if (f == "BUILDON") {
            if (value == "SHALLOW_WATER")
                s.buildOn |= 4;
            else if (value == "LAND")
                s.buildOn |= 1;
            else if (value == "DEEP_SEA")
                s.buildOn |= 8;
            else if (value == "BEACH")
                s.buildOn |= 2;
            else if (value == "PATH")
                s.buildOn |= 0x80;
            else
                return Fail("invalid scenery BUILDON", value); // the engine ignores it
        } else if (f == "COLLIDE")
            flag(s.collide);
        else if (f == "CAPACITY")
            s.capacity = ToInt(value);
        else if (f == "MENU")
            s.menu = value;
        else if (f == "ICON")
            s.icon = ToInt(value);
        else if (f == "CATEGORY") {
            for (int i = 0; i < 8; ++i)
                if (value == kCategories[i]) {
                    s.category = i;
                    return true;
                }
            return Fail("unrecognized scenery category", value);
        } else if (f == "RATING")
            s.rating = ToFloat(value);
        else if (f == "CLEAN")
            flag(s.clean);
        else if (f == "REPAIR")
            flag(s.repair);
        else if (f == "BURN")
            flag(s.burn);
        else if (f == "USERREMOVE")
            flag(s.userRemove);
        else if (f == "VEGFLAG")
            flag(s.vegetation);
        else if (f == "PATHFLAG")
            s.pathFlag = ToInt(value);
        else if (f == "WATER")
            flag(s.water);
        else
            return Fail("unrecognized scenery field", s.key + "_" + f); // the engine ignores it
        return true;
    }

    // The table lists of TableDef: each allocated by a count line, filled by the lines after it.
    struct TableState {
        int walkCapacity = -1, chairPosCapacity = -1, satDownCapacity = -1;
        int chairs = -1; // CAPACITY: chair directions and walk lists
        std::array<float, 2> walkSlot{}, chairPosSlot{}, serveSlot{};
        std::vector<std::array<float, 2>> chairPos, servePoints;
        std::vector<int> dirs;
        std::vector<std::vector<int>> walkLists;
        std::vector<int> walkListSizes;
    };

    bool TableField(size_t index, const std::string& value)
    {
        static const char* const kDirections[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        BlTable& t = m_list.tables[index];
        TableState& s = m_tableState[index];
        const std::string& f = m_field;
        auto full = [&](size_t size, int capacity) {
            return capacity < 0 || size >= static_cast<size_t>(capacity) ? Fail("list past its count", t.key + "_" + f) : true;
        };
        if (f == "NAME")
            t.name = value;
        else if (f == "TABLE_SPRITE")
            t.tableSprite = value;
        else if (f == "CHAIR_SPRITE")
            t.chairSprite = value;
        else if (f == "GRIDWIDTH")
            t.width = ToInt(value);
        else if (f == "GRIDHEIGHT")
            t.height = ToInt(value);
        else if (f == "CAPACITY") {
            if (s.chairs >= 0) return Fail("second CAPACITY", t.key);
            s.chairs = ToInt(value);
        } else if (f == "NUM_WALK_PTS") {
            // The walk point count is not reset: a second allocation would lose the first points' memory but keep counting.
            if (s.walkCapacity >= 0) return Fail("second NUM_WALK_PTS", t.key);
            s.walkCapacity = ToInt(value);
        } else if (f == "WALK_PNT_X") {
            if (!full(t.walkPoints.size(), s.walkCapacity)) return false;
            s.walkSlot[0] = ToFloat(value);
        } else if (f == "WALK_PNT_Y") {
            if (!full(t.walkPoints.size(), s.walkCapacity)) return false;
            s.walkSlot[1] = ToFloat(value);
            t.walkPoints.push_back(s.walkSlot);
        } else if (f == "NUM_CHAIRPOS_PTS") {
            if (s.chairPosCapacity >= 0) return Fail("second NUM_CHAIRPOS_PTS", t.key);
            s.chairPosCapacity = ToInt(value);
        } else if (f == "CHAIRPOS_X") {
            if (!full(s.chairPos.size(), s.chairPosCapacity)) return false;
            s.chairPosSlot[0] = ToFloat(value);
        } else if (f == "CHAIRPOS_Y") {
            if (!full(s.chairPos.size(), s.chairPosCapacity)) return false;
            s.chairPosSlot[1] = ToFloat(value);
            s.chairPos.push_back(s.chairPosSlot);
        } else if (f == "CHAIR_DIRECTION") {
            if (!full(s.dirs.size(), s.chairs)) return false;
            for (int i = 0; i < 8; ++i)
                if (value == kDirections[i]) {
                    s.dirs.push_back(i);
                    return true;
                }
            return Fail("invalid direction in table", value);
        } else if (f == "CHAIR_NUM_WALKPNT") {
            if (!full(s.walkLists.size(), s.chairs)) return false;
            s.walkLists.emplace_back();
            s.walkListSizes.push_back(ToInt(value));
        } else if (f == "CHAIR_WALKPNT") {
            if (s.walkLists.empty()) return Fail("CHAIR_WALKPNT before CHAIR_NUM_WALKPNT", t.key);
            if (!full(s.walkLists.back().size(), s.walkListSizes.back())) return false;
            s.walkLists.back().push_back(ToInt(value));
        } else if (f == "SIT_DOWN_ANIM")
            t.sitDownAnim = value;
        else if (f == "NUM_SAT_DOWN_ANIM") {
            if (s.satDownCapacity >= 0) return Fail("second NUM_SAT_DOWN_ANIM", t.key);
            s.satDownCapacity = ToInt(value);
        } else if (f == "SAT_DOWN_ANIM") {
            if (!full(t.satDownAnims.size(), s.satDownCapacity)) return false;
            t.satDownAnims.push_back(value);
        } else if (f == "GET_UP_ANIM")
            t.getUpAnim = value;
        else if (f == "SERVE_TABLE")
            t.serveTable = ToInt(value) != 0;
        else if (f == "XOFFSET_TABLE")
            t.offsetX = ToInt(value);
        else if (f == "YOFFSET_TABLE")
            t.offsetY = ToInt(value);
        else if (f == "HAS_WAITER")
            t.hasWaiter = ToInt(value) != 0;
        else if (f == "WAITER_SPRITE")
            t.waiterSprite = value;
        else if (f == "WAITER_WALK_ANIM")
            t.waiterWalkAnim = value;
        else if (f == "WAITER_SERVE_ANIM")
            t.waiterServeAnim = value;
        else if (f == "CALL_WAITER_ANIM")
            t.callWaiterAnim = value;
        else if (f == "WAIT_WAITER_ANIM")
            t.waitWaiterAnim = value;
        else if (f == "WAITER_SERVE_POINT_X") {
            if (!full(s.servePoints.size(), s.chairs)) return false;
            s.serveSlot[0] = ToFloat(value);
        } else if (f == "WAITER_SERVE_POINT_Y") {
            if (!full(s.servePoints.size(), s.chairs)) return false;
            s.serveSlot[1] = ToFloat(value);
            s.servePoints.push_back(s.serveSlot);
        } else
            return Fail("unrecognized table field", t.key + "_" + f);
        return true;
    }

    bool BuildingField(int id)
    {
        BlBuilding& b = m_list.buildings[static_cast<size_t>(id)];
        const std::string& f = m_field;
        std::vector<std::string> v;
        // ANIMSPRITE and COMMODITY read further tokens of the same line.
        if (f == "ANIMSPRITE") {
            if (!Take(10, v)) return false;
            if (b.anims.size() >= kAnimSprites) return Fail("too many ANIMSPRITE lines", b.key);
            BlAnimSprite a;
            a.sprite = v[0];
            for (size_t i = 0; i < 4; ++i) a.points[i] = {ToInt(v[1 + 2 * i]), ToInt(v[2 + 2 * i])};
            a.flag = ToInt(v[9]) != 0;
            b.anims.push_back(a);
            return true;
        }
        if (f == "COMMODITY") {
            if (!Take(7, v)) return false;
            if (b.commodities.size() >= kCommoditySlots) return Fail("too many COMMODITY lines", b.key);
            BlBuildingCommodity c;
            if (!Lookup(m_list.commodities, v[0], "commodity", c.commodity)) return false;
            c.intensityMen = ToFloat(v[1]);
            c.intensityWomen = ToFloat(v[2]);
            c.dailyIntensityFactor = ToFloat(v[3]);
            c.price = ToFloat(v[4]);
            c.dailyPriceFactor = ToFloat(v[5]);
            c.cost = ToFloat(v[6]);
            b.commodities.push_back(c);
            return true;
        }
        std::string value;
        if (!Take1(value)) return false;
        const size_t index = static_cast<size_t>(id);
        auto overflow = [&](size_t size, int capacity) {
            return size >= static_cast<size_t>(capacity) ? Fail("list past its capacity", b.key + "_" + f) : true;
        };
        if (f == "NAME")
            b.name = value;
        else if (f == "SPRITE")
            b.sprite = value;
        else if (f == "ICON")
            b.icon = ToInt(value);
        else if (f == "COST2BUILD")
            b.cost = ToInt(value);
        else if (f == "REFUNDVALUE")
            b.refund = ToInt(value);
        else if (f == "GRIDWIDTH")
            b.width = ToInt(value);
        else if (f == "GRIDHEIGHT")
            b.height = ToInt(value);
        else if (f == "XSERVEOFFSET")
            b.serve[0] = static_cast<int16_t>(ToInt(value));
        else if (f == "YSERVEOFFSET")
            b.serve[1] = static_cast<int16_t>(ToInt(value));
        else if (f == "STAFF_XOFFSET")
            b.staffOffset[0] = static_cast<int16_t>(ToInt(value));
        else if (f == "STAFF_YOFFSET")
            b.staffOffset[1] = static_cast<int16_t>(ToInt(value));
        else if (f == "XENTRANCEOFFSET")
            b.entrance[0] = static_cast<int16_t>(ToInt(value));
        else if (f == "YENTRANCEOFFSET")
            b.entrance[1] = static_cast<int16_t>(ToInt(value));
        else if (f == "GUESTCAPACITY")
            b.guestCapacity = ToInt(value);
        else if (f == "BUILDTIME")
            b.buildTime = ToInt(value);
        else if (f == "OPENINGHOURS")
            return Lookup(m_list.openingHours, value, "opening hours", b.openingHours);
        else if (f == "MAXINSTANCES")
            b.maxInstances = ToInt(value);
        else if (f == "ATTRACTOR")
            return Lookup(m_list.families, value, "commodity family", b.attractor);
        else if (f == "TREE")
            b.tree = value;
        else if (f == "MENU")
            b.menu = value;
        else if (f == "MAXSTAFF")
            b.maxStaff = ToInt(value);
        else if (f == "STAFF") {
            int staff = 0;
            if (!overflow(b.staff.size(), kBuildingStaff) || !Lookup(m_list.staff, value, "staff", staff)) return false;
            b.staff.push_back(staff);
        } else if (f == "VEHICLE") {
            int vehicle = 0;
            if (!overflow(b.vehicles.size(), kBuildingVehicles) || !Lookup(m_list.vehicles, value, "vehicle", vehicle)) return false;
            b.vehicles.push_back(vehicle);
        } else if (f == "VEHICLE_DIR" || f == "VEHICLE_GRID_XOFFSET" || f == "VEHICLE_GRID_YOFFSET") {
            if (!overflow(b.vehiclePlaces.size(), kVehiclePlaces)) return false;
            BlVehiclePlace& slot = m_vehiclePlace[index];
            if (f == "VEHICLE_DIR")
                slot.dir = ToInt(value);
            else if (f == "VEHICLE_GRID_XOFFSET")
                slot.x = ToInt(value);
            else {
                slot.y = ToInt(value);
                b.vehiclePlaces.push_back(slot);
            }
        } else if (f == "BUILDON") {
            if (value == "SHALLOW_WATER")
                b.buildOn |= 4;
            else if (value == "LAND")
                b.buildOn |= 1;
            else if (value == "DEEP_SEA")
                b.buildOn |= 8;
            else if (value == "BEACH")
                b.buildOn |= 2;
            else if (value == "WATER")
                b.buildOn |= 0xc;
            else
                return Fail("invalid BUILDON type", value);
        } else if (f == "TABLE" || f == "TABLE_POSX" || f == "TABLE_POSY") {
            if (!overflow(b.tables.size(), kBuildingTables)) return false;
            BlTablePlace& slot = m_tablePlace[index];
            if (f == "TABLE") return Lookup(m_list.tables, value, "table", slot.table);
            if (f == "TABLE_POSX")
                slot.x = ToFloat(value);
            else {
                slot.y = ToFloat(value);
                b.tables.push_back(slot);
            }
        } else if (f == "EVENT") {
            EventSlots& slots = m_eventSlots[index];
            const int at = static_cast<int>(m_list.events.size());
            int event = 0;
            if (!Lookup(m_list.events, value, "event", event)) return false;
            if (at < kEventSlots)
                slots.ids[static_cast<size_t>(at)] = event;
            else if (at == kEventSlots)
                slots.count = event;
            else
                return Fail("EVENT index past the building record", b.key);
            ++slots.count;
        } else if (f == "CONSTRUCT") {
            b.construct = value;
            b.customConstruct = true;
        } else if (f == "POWER")
            b.power = ToInt(value);
        else if (f == "NUM_WALKPOINTS") {
            b.walkPoints.clear();
            m_walk[index] = ToInt(value);
        } else if (f == "WALKPOINTX" || f == "WALKPOINTY") {
            if (m_walk[index] <= 0) return Fail("WALKPOINT without NUM_WALKPOINTS", b.key);
            if (!overflow(b.walkPoints.size(), m_walk[index])) return false;
            if (f == "WALKPOINTX")
                m_walkSlot[0] = ToFloat(value);
            else {
                m_walkSlot[1] = ToFloat(value);
                b.walkPoints.push_back(m_walkSlot);
            }
        } else if (f == "AVFAILTIME")
            b.avFailTime = ToFloat(value);
        else if (f == "FAIL_PERCENT")
            b.failPercent = ToInt(value);
        else if (f == "FAIL_AGE")
            b.failAge = ToInt(value);
        else if (f == "DEMOLISHTIME")
            b.demolishTime = ToInt(value);
        else if (f == "MAX_QUEUE")
            b.maxQueue = ToInt(value);
        else if (f == "CATEGORY") {
            static const char* const kCategories[] = {"Hotel", "Food", "Leisure", "Other"};
            for (int i = 0; i < 4; ++i)
                if (value == kCategories[i]) {
                    b.category = i;
                    return true;
                }
            return Fail("unrecognized building category", value); // the engine keeps the old value
        } else if (f == "NUM_BASEPLATES") {
            b.basePlates.clear();
            m_basePlates[index] = ToInt(value);
        } else if (f == "BASEPLATETYPE") {
            if (m_basePlates[index] <= 0) return Fail("BASEPLATETYPE without NUM_BASEPLATES", b.key);
            if (!overflow(b.basePlates.size(), m_basePlates[index])) return false;
            b.basePlates.push_back(ToInt(value));
        } else if (f == "MAXBUILDERS")
            b.maxBuilders = ToInt(value);
        else if (f == "FEMALEFILENAME")
            b.femaleFile = value;
        else if (f == "MALEFILENAME")
            b.maleFile = value;
        else
            return Fail("unrecognized building field", b.key + "_" + f);
        return true;
    }

    bool Finish()
    {
        for (size_t i = 0; i < m_list.buildings.size(); ++i) {
            const EventSlots& slots = m_eventSlots[i];
            if (slots.count > kEventSlots) return Fail("building event count past its slots", m_list.buildings[i].key);
            m_list.buildings[i].events.assign(slots.ids.begin(), slots.ids.begin() + slots.count);
        }
        for (size_t i = 0; i < m_list.tables.size(); ++i) {
            BlTable& t = m_list.tables[i];
            const TableState& s = m_tableState[i];
            const size_t chairs = static_cast<size_t>(s.chairs < 0 ? 0 : s.chairs);
            if (s.chairPos.size() != chairs || s.dirs.size() != chairs || s.walkLists.size() != chairs)
                return Fail("chair counts disagree", t.key);
            for (size_t c = 0; c < chairs; ++c) {
                if (s.walkLists[c].size() != static_cast<size_t>(s.walkListSizes[c])) return Fail("chair walk list incomplete", t.key);
                BlChair chair;
                chair.pos = s.chairPos[c];
                chair.dir = s.dirs[c];
                chair.walkPoints = s.walkLists[c];
                if (c < s.servePoints.size()) chair.servePoint = s.servePoints[c];
                t.chairs.push_back(chair);
            }
        }
        return true;
    }

    struct EventSlots {
        std::array<int, kEventSlots> ids{};
        int count = 0;
    };

    BuildingList& m_list;
    std::string& m_error;
    std::vector<std::string> m_tokens;
    size_t m_next = 0;
    std::string m_keyword, m_field;
    // Per building: walk point and base plate capacities, the pending vehicle place and table, the event slots.
    std::vector<int> m_walk, m_basePlates;
    std::array<float, 2> m_walkSlot{};
    std::vector<BlVehiclePlace> m_vehiclePlace;
    std::vector<BlTablePlace> m_tablePlace;
    std::vector<EventSlots> m_eventSlots;
    std::vector<TableState> m_tableState;
};

} // namespace

bool ParseBuildingList(std::string_view text, BuildingList& list, std::string& error)
{
    list = {};
    return Parser(list, error).Run(text);
}

bool ReadBuildingList(const ByteSource& source, BuildingList& list, std::string& error)
{
    const auto path = FindFile(source, "Data/Script/BuildingList.ble");
    if (!path) {
        error = "Data/Script/BuildingList.ble not found in " + source.Describe("");
        return false;
    }
    const auto bytes = source.ReadAll(*path);
    if (!bytes) {
        error = "cannot read " + source.Describe(*path);
        return false;
    }
    std::string text(bytes->begin(), bytes->end());
    for (char& c : text) c = static_cast<char>(~static_cast<unsigned char>(c));
    return ParseBuildingList(text, list, error);
}

} // namespace obl::convert
