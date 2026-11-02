// catalog/ writer (converter/catalog.md). Every engine-read value is written, defaults included, so a document holds what
// the game loads; empty strings and empty lists are left out (the schemas give them no meaning beyond "none").

#include "importer/src/writers/catalog_writer.h"

#include "importer/src/source/pe_file.h"
#include "formats/cell_state.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"

#include "importer/src/readers/building_behavior_keys.h"
#include "core/ascii_case.h"
#include "core/code_page.h"
#include "core/json_float.h"
#include "core/json_schema.h"

#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>

namespace obl::convert {

using nlohmann::ordered_json;

ordered_json FloatNumber(float value)
{
    return json_float::Number(value);
}

namespace {

struct DesireSlotRow {
    std::string key, scriptName;
};

constexpr int kVersion = 2;

// The guest desire slots in id order, with the word the world scripts use for a slot's Weather modifier.
const std::vector<DesireSlotRow> kDesireSlots = {{"accommodation", ""}, {"alcohol", "Alcohol"}, {"amusement", "Amusement"},
        {"first_aid", ""}, {"headache", "HeadAche"}, {"meals", "Meals"}, {"party_fun", "PartyFun"}, {"shower", ""}, {"snacks", "Snacks"},
        {"soft_drinks", "SoftDrinks"}, {"souvenirs", ""}, {"sports_fun", "SportsFun"}, {"sunbathe", "Sunbathing"},
        {"swim_fun", "SwimmingFun"}, {"toilet", ""}, {"water_sports_fun", "WaterSportsFun"}, {"stroll", "StrollAround"}, {"leave", ""},
        {"vandalise", "Vandalise"}, {"fight", "Fight"}, {"flirt", "Flirt"}, {"umbrellas", "Umbrellas"}, {"suntan", "SunTanLotion"},
        {"intimacy", "HardSex"}, {"sleep", ""}, {"scared", ""}};

class Writer {
public:
    Writer(const BuildingList& list, const std::vector<ExeFootprint>& footprints, std::vector<CatalogFile>& files, std::string& error)
        : m_list(list), m_footprints(footprints), m_files(files), m_error(error)
    {}

    bool Run()
    {
        ordered_json index = Header("obl.catalog_index");
        bool ok = Keys(index, "commodity_families", m_list.families) && Keys(index, "commodities", m_list.commodities) &&
                Keys(index, "opening_hours", m_list.openingHours) && Keys(index, "desires", m_list.desires) &&
                Keys(index, "desire_slots", kDesireSlots) && Keys(index, "staff", m_list.staff) && Keys(index, "events", m_list.events) &&
                Keys(index, "beers", m_list.beers) && Keys(index, "fines", m_list.fines) && Keys(index, "vehicles", m_list.vehicles) &&
                Keys(index, "tables", m_list.tables) && Keys(index, "scenery", m_list.scenery) &&
                Keys(index, "buildings", m_list.buildings);
        if (!ok) return false;
        m_footprintOf.assign(m_list.buildings.size(), nullptr);
        for (const ExeFootprint& footprint : m_footprints) {
            const int id = FindBuilding(footprint.building);
            if (id < 0) return Fail("footprint of an unrecognized building: " + footprint.building);
            if (m_footprintOf[static_cast<size_t>(id)]) return Fail("second footprint of " + footprint.building);
            m_footprintOf[static_cast<size_t>(id)] = &footprint;
        }
        m_files.push_back({"catalog/index.json", "obl.catalog_index", std::move(index)});
        for (size_t i = 0; i < m_list.families.size(); ++i) {
            ordered_json d = Entry("obl.commodity_family", "commodity_families", i);
            d["name"] = Utf8(m_list.families[i].name);
            Add("commodity_families", i, "obl.commodity_family", std::move(d));
        }
        for (size_t i = 0; i < m_list.commodities.size(); ++i) {
            ordered_json d = Entry("obl.commodity", "commodities", i);
            d["name"] = Utf8(m_list.commodities[i].name);
            d["family"] = Ref("commodity_families", m_list.commodities[i].family);
            Add("commodities", i, "obl.commodity", std::move(d));
        }
        for (size_t i = 0; i < m_list.openingHours.size(); ++i) {
            ordered_json d = Entry("obl.opening_hours", "opening_hours", i);
            d["open"] = FloatNumber(m_list.openingHours[i].open);
            d["close"] = FloatNumber(m_list.openingHours[i].close);
            Add("opening_hours", i, "obl.opening_hours", std::move(d));
        }
        for (size_t i = 0; i < m_list.desires.size(); ++i) {
            ordered_json d = Entry("obl.desire", "desires", i);
            d["name"] = Utf8(m_list.desires[i].name);
            d["families"] = Refs("commodity_families", m_list.desires[i].families);
            Add("desires", i, "obl.desire", std::move(d));
        }
        for (size_t i = 0; i < kDesireSlots.size(); ++i) {
            ordered_json d = Entry("obl.desire_slot", "desire_slots", i);
            OptionalText(d, "script_name", kDesireSlots[i].scriptName);
            Add("desire_slots", i, "obl.desire_slot", std::move(d));
        }
        for (size_t i = 0; i < m_list.staff.size() && ok; ++i) ok = Staff(i);
        for (size_t i = 0; i < m_list.events.size(); ++i) {
            const BlEvent& e = m_list.events[i];
            ordered_json d = Entry("obl.event", "events", i);
            d["cost"] = FloatNumber(e.cost);
            d["price"] = FloatNumber(e.price);
            d["intensity"] = Pair(e.intensity);
            Add("events", i, "obl.event", std::move(d));
        }
        for (size_t i = 0; i < m_list.beers.size(); ++i) Beer(i);
        for (size_t i = 0; i < m_list.fines.size(); ++i) {
            ordered_json d = Entry("obl.fine", "fines", i);
            d["cost"] = FloatNumber(m_list.fines[i].cost);
            Add("fines", i, "obl.fine", std::move(d));
        }
        for (size_t i = 0; i < m_list.vehicles.size() && ok; ++i) ok = Vehicle(i);
        for (size_t i = 0; i < m_list.tables.size() && ok; ++i) ok = Table(i);
        for (size_t i = 0; i < m_list.scenery.size() && ok; ++i) ok = Scenery(i);
        for (size_t i = 0; i < m_list.buildings.size() && ok; ++i) ok = Building(i);
        return ok;
    }

private:
    static std::string Utf8(const std::string& text) { return code_page::Utf16ToUtf8(code_page::Widen(text)); }

    static ordered_json Header(const char* format)
    {
        ordered_json d;
        d["format"] = format;
        d["version"] = kVersion;
        return d;
    }

    template<class Record> bool Keys(ordered_json& index, const char* kind, const std::vector<Record>& records)
    {
        std::vector<std::string>& keys = m_keys[kind];
        std::set<std::string> seen;
        for (const Record& r : records) {
            std::string key;
            if (!ContentKey(r.key, std::string(kind) + " key", key)) return false;
            if (!seen.insert(key).second) return Fail(std::string(kind) + " keys differ only in case: " + r.key);
            keys.push_back(key);
        }
        index[kind] = keys;
        return true;
    }

    bool ContentKey(const std::string& text, const std::string& what, std::string& key)
    {
        key.clear();
        for (char c : text) {
            const char lower = ascii::Lower(c);
            if (!((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '_'))
                return Fail(what + " is not a content key: \"" + text + "\"");
            key += lower;
        }
        if (key.empty()) return Fail(what + " is empty");
        return true;
    }

    // An optional key-valued field (sprite, animation): left out when empty.
    bool OptionalKey(ordered_json& d, const char* field, const std::string& text, const std::string& owner, const std::string& prefix = "")
    {
        if (text.empty()) return true;
        std::string key;
        if (!ContentKey(text, owner + " " + field, key)) return false;
        d[field] = key;
        return true;
    }
    static void OptionalText(ordered_json& d, const char* field, const std::string& text)
    {
        if (!text.empty()) d[field] = Utf8(text);
    }

    ordered_json Entry(const char* format, const char* kind, size_t index)
    {
        ordered_json d = Header(format);
        d["key"] = m_keys[kind][index];
        return d;
    }
    void Add(const char* kind, size_t index, const char* schema, ordered_json document)
    {
        m_files.push_back({std::string("catalog/") + kind + "/" + m_keys[kind][index] + ".json", schema, std::move(document)});
    }
    std::string Ref(const char* kind, int id) { return m_keys[kind][static_cast<size_t>(id)]; }
    ordered_json Refs(const char* kind, const std::vector<int>& ids)
    {
        ordered_json list = ordered_json::array();
        for (int id : ids) list.push_back(Ref(kind, id));
        return list;
    }
    static ordered_json Pair(const std::array<float, 2>& value)
    {
        return ordered_json::array({FloatNumber(value[0]), FloatNumber(value[1])});
    }
    template<class T> static ordered_json IntPair(T x, T y) { return ordered_json::array({static_cast<int>(x), static_cast<int>(y)}); }
    static ordered_json Points(const std::vector<std::array<float, 2>>& points)
    {
        ordered_json list = ordered_json::array();
        for (const auto& p : points) list.push_back(Pair(p));
        return list;
    }

    // BUILDON masks: LAND 1, BEACH 2, SHALLOW_WATER 4, DEEP_SEA 8, PATH 0x80 (scenery only).
    static ordered_json Terrain(uint32_t mask)
    {
        ordered_json list = ordered_json::array();
        if (mask & 1) list.push_back("land");
        if (mask & 2) list.push_back("beach");
        if (mask & 4) list.push_back("shallow_water");
        if (mask & 8) list.push_back("deep_sea");
        if (mask & 0x80) list.push_back("path");
        return list;
    }

    bool Staff(size_t i)
    {
        const BlStaff& s = m_list.staff[i];
        ordered_json d = Entry("obl.staff", "staff", i);
        d["name"] = Utf8(s.name);
        d["gender"] = s.female ? "f" : "m";
        std::string sprite;
        if (!ContentKey(s.sprite, s.key + " sprite", sprite)) return false;
        d["sprite"] = sprite;
        d["script"] = Utf8(s.script);
        d["menu"] = Utf8(s.menu);
        d["wage"] = s.wage;
        d["recommended_wage"] = s.recommendedWage;
        d["scale"] = FloatNumber(s.scale);
        d["expectation_growth"] = FloatNumber(s.expectationGrowth);
        d["initial_happiness"] = FloatNumber(s.initialHappiness);
        d["hours"] = IntPair(s.startHour, s.endHour);
        Add("staff", i, "obl.staff", std::move(d));
        return true;
    }

    void Beer(size_t i)
    {
        static const char* const kEffects[] = {"fight", "flirt", "vandalise", "happy", "sad"};
        const BlBeer& b = m_list.beers[i];
        ordered_json d = Entry("obl.beer", "beers", i);
        OptionalText(d, "brand", b.brand);
        OptionalText(d, "cocktail", b.cocktail);
        d["base_cost"] = FloatNumber(b.baseCost);
        d["strength_cost"] = FloatNumber(b.strengthCost);
        d["alcohol_strength"] = FloatNumber(b.alcoholStrength);
        d["price"] = FloatNumber(b.price);
        d["effect"] = kEffects[b.effect];
        Add("beers", i, "obl.beer", std::move(d));
    }

    bool Vehicle(size_t i)
    {
        const BlVehicle& v = m_list.vehicles[i];
        ordered_json d = Entry("obl.vehicle", "vehicles", i);
        d["name"] = Utf8(v.name);
        if (!OptionalKey(d, "sprite", v.sprite, v.key)) return false;
        d["size"] = IntPair(v.width, v.height);
        d["capacity"] = v.capacity;
        d["max_speed"] = FloatNumber(v.maxSpeed);
        d["force"] = FloatNumber(v.force);
        d["mass"] = FloatNumber(v.mass);
        if (v.moveType != 1 && v.moveType != 2) return Fail("vehicle without MOVETYPE: " + v.key);
        d["move_type"] = v.moveType == 1 ? "points" : "boundary";
        OptionalText(d, "towed", v.towed);
        if (!OptionalKey(d, "anim_set", v.animSet, v.key)) return false;
        if (!v.staff.empty()) d["staff"] = Refs("staff", v.staff);
        d["creation_point"] = Pair(v.creationPoint);
        Add("vehicles", i, "obl.vehicle", std::move(d));
        return true;
    }

    bool Table(size_t i)
    {
        static const char* const kDirections[] = {"n", "ne", "e", "se", "s", "sw", "w", "nw"};
        const BlTable& t = m_list.tables[i];
        ordered_json d = Entry("obl.table", "tables", i);
        d["name"] = Utf8(t.name);
        if (!OptionalKey(d, "table_sprite", t.tableSprite, t.key) || !OptionalKey(d, "chair_sprite", t.chairSprite, t.key)) return false;
        d["size"] = IntPair(t.width, t.height);
        d["walk_points"] = Points(t.walkPoints);
        ordered_json chairs = ordered_json::array();
        for (const BlChair& c : t.chairs) {
            ordered_json chair;
            chair["pos"] = Pair(c.pos);
            chair["dir"] = kDirections[c.dir];
            chair["walk_points"] = c.walkPoints;
            if (c.servePoint) chair["serve_point"] = Pair(*c.servePoint);
            chairs.push_back(std::move(chair));
        }
        d["chairs"] = std::move(chairs);
        if (!OptionalKey(d, "sit_down_anim", t.sitDownAnim, t.key)) return false;
        if (!t.satDownAnims.empty()) {
            ordered_json anims = ordered_json::array();
            for (const std::string& anim : t.satDownAnims) {
                std::string key;
                if (!ContentKey(anim, t.key + " sat_down_anims", key)) return false;
                anims.push_back(key);
            }
            d["sat_down_anims"] = std::move(anims);
        }
        if (!OptionalKey(d, "get_up_anim", t.getUpAnim, t.key)) return false;
        d["serve_table"] = t.serveTable;
        d["table_offset"] = IntPair(t.offsetX, t.offsetY);
        d["has_waiter"] = t.hasWaiter;
        ordered_json waiter = ordered_json::object();
        if (!OptionalKey(waiter, "sprite", t.waiterSprite, t.key, "/waiter") ||
                !OptionalKey(waiter, "walk_anim", t.waiterWalkAnim, t.key, "/waiter") ||
                !OptionalKey(waiter, "serve_anim", t.waiterServeAnim, t.key, "/waiter") ||
                !OptionalKey(waiter, "call_anim", t.callWaiterAnim, t.key, "/waiter") ||
                !OptionalKey(waiter, "wait_anim", t.waitWaiterAnim, t.key, "/waiter"))
            return false;
        if (!waiter.empty()) d["waiter"] = std::move(waiter);
        Add("tables", i, "obl.table", std::move(d));
        return true;
    }

    bool Scenery(size_t i)
    {
        static const char* const kCategories[] = {"furniture", "paths", "plants", "ornaments", "trees", "shrubs", "billboard", "other"};
        const BlScenery& s = m_list.scenery[i];
        ordered_json d = Entry("obl.scenery", "scenery", i);
        d["name"] = Utf8(s.name);
        d["category"] = kCategories[s.category];
        if (!OptionalKey(d, "sprite", s.sprite, s.key) || !OptionalKey(d, "anim", s.anim, s.key)) return false;
        d["icon"] = s.icon;
        OptionalText(d, "menu", s.menu);
        d["size"] = IntPair(s.width, s.height);
        d["cost"] = s.cost;
        d["build_on"] = Terrain(s.buildOn);
        d["collide"] = s.collide;
        d["capacity"] = s.capacity;
        d["rating"] = FloatNumber(s.rating);
        d["clean"] = s.clean;
        d["repair"] = s.repair;
        d["burn"] = s.burn;
        d["user_remove"] = s.userRemove;
        d["vegetation"] = s.vegetation;
        d["water"] = s.water;
        d["path_flag"] = s.pathFlag;
        Add("scenery", i, "obl.scenery", std::move(d));
        return true;
    }

    int FindBuilding(const std::string& key) const
    {
        for (size_t i = 0; i < m_list.buildings.size(); ++i)
            if (m_list.buildings[i].key == key) return static_cast<int>(i);
        return -1;
    }

    // The four 25 x 25 layers of a grid cut to the (h + 2) rows of (w + 2) cells the engine reads (CanPlace, ApplyFootprint); every
    // dropped cell must be 0.
    bool Footprint(const BlBuilding& b, const ExeFootprint& grid, ordered_json& out)
    {
        static const char* const kLayers[] = {"placement", "work", "depth", "body"};
        constexpr int kColumns = 25, kLayerRows = 25;
        if (grid.width != b.width || grid.height != b.height || grid.cells.size() != static_cast<size_t>(4 * kLayerRows * kColumns))
            return Fail("footprint size differs from the building: " + b.key);
        const int rows = b.height + 2, columns = b.width + 2;
        ordered_json layers = ordered_json::object();
        for (int layer = 0; layer < 4; ++layer) {
            ordered_json list = ordered_json::array();
            for (int row = 0; row < kLayerRows; ++row) {
                ordered_json cells = ordered_json::array();
                for (int column = 0; column < kColumns; ++column) {
                    const uint32_t value =
                            static_cast<uint32_t>(grid.cells[static_cast<size_t>((layer * kLayerRows + row) * kColumns + column)]);
                    if (row < rows && column < columns)
                        cells.push_back(layer < 2 ? formats::CellWordToRecord(value) : ordered_json(value));
                    else if (value != 0)
                        return Fail("footprint cell outside the area the engine reads: " + b.key);
                }
                if (row < rows) list.push_back(std::move(cells));
            }
            layers[kLayers[layer]] = std::move(list);
        }
        out = {{"size", IntPair(grid.width, grid.height)}, {"layers", std::move(layers)}};
        return true;
    }

    bool Building(size_t i)
    {
        static const char* const kCategories[] = {"hotel", "food", "leisure", "other"};
        const BlBuilding& b = m_list.buildings[i];
        // A building that names none takes entry 0 of each; a list without one has nothing to refer to.
        if (m_list.openingHours.empty() || m_list.families.empty()) return Fail("no opening hours or commodity family for " + b.key);
        ordered_json d = Entry("obl.building", "buildings", i);
        d["version"] = 2;
        d["name"] = Utf8(b.name);
        d["category"] = kCategories[b.category];
        d["behavior"] = BuildingBehaviorName(BehaviorFromKey(b.key.c_str()));
        if (!OptionalKey(d, "sprite", b.sprite, b.key)) return false;
        d["icon"] = b.icon;
        OptionalText(d, "menu", b.menu);
        if (!OptionalKey(d, "requires", b.tree, b.key)) return false;
        d["cost"] = {{"build", b.cost}, {"refund", b.refund}};
        ordered_json construction = {{"build_time", b.buildTime}, {"demolish_time", b.demolishTime}, {"max_builders", b.maxBuilders}};
        if (b.customConstruct && !OptionalKey(construction, "anim", b.construct, b.key, "/construction")) return false;
        d["construction"] = std::move(construction);
        d["size"] = IntPair(b.width, b.height);
        d["build_on"] = Terrain(b.buildOn);
        if (!b.basePlates.empty()) d["baseplates"] = b.basePlates;
        ordered_json points = {{"serve", IntPair(b.serve[0], b.serve[1])}, {"staff", IntPair(b.staffOffset[0], b.staffOffset[1])},
                {"entrance", IntPair(b.entrance[0], b.entrance[1])}};
        if (!b.walkPoints.empty()) points["walk"] = Points(b.walkPoints);
        d["points"] = std::move(points);
        d["operation"] = {{"guest_capacity", b.guestCapacity}, {"opening_hours", Ref("opening_hours", b.openingHours)},
                {"max_instances", b.maxInstances}, {"max_queue", b.maxQueue}, {"attractor", Ref("commodity_families", b.attractor)},
                {"power", b.power}, {"staff", {{"types", Refs("staff", b.staff)}, {"max", b.maxStaff}}}};
        if (!b.commodities.empty()) {
            ordered_json list = ordered_json::array();
            for (const BlBuildingCommodity& c : b.commodities)
                list.push_back({{"commodity", Ref("commodities", c.commodity)},
                        {"intensity", ordered_json::array({FloatNumber(c.intensityMen), FloatNumber(c.intensityWomen)})},
                        {"daily_intensity_factor", FloatNumber(c.dailyIntensityFactor)}, {"price", FloatNumber(c.price)},
                        {"daily_price_factor", FloatNumber(c.dailyPriceFactor)}, {"cost", FloatNumber(c.cost)}});
            d["commodities"] = std::move(list);
        }
        d["failure"] = {{"average_time", FloatNumber(b.avFailTime)}, {"percent", b.failPercent}, {"age", b.failAge}};
        if (!b.vehicles.empty()) d["vehicles"] = Refs("vehicles", b.vehicles);
        if (!b.vehiclePlaces.empty()) {
            ordered_json list = ordered_json::array();
            for (const BlVehiclePlace& p : b.vehiclePlaces) list.push_back({{"dir", p.dir}, {"offset", IntPair(p.x, p.y)}});
            d["vehicle_places"] = std::move(list);
        }
        if (!b.tables.empty()) {
            ordered_json list = ordered_json::array();
            for (const BlTablePlace& p : b.tables) list.push_back({{"table", Ref("tables", p.table)}, {"pos", Pair({p.x, p.y})}});
            d["tables"] = std::move(list);
        }
        if (!b.events.empty()) d["events"] = Refs("events", b.events);
        if (!b.anims.empty()) {
            ordered_json list = ordered_json::array();
            for (const BlAnimSprite& a : b.anims) {
                std::string sprite;
                if (!ContentKey(a.sprite, b.key + " anims sprite", sprite)) return false;
                ordered_json pts = ordered_json::array();
                for (const auto& p : a.points) pts.push_back(IntPair(p[0], p[1]));
                list.push_back({{"sprite", sprite}, {"points", std::move(pts)}, {"flag", a.flag}});
            }
            d["anims"] = std::move(list);
        }
        ordered_json files = ordered_json::object();
        OptionalText(files, "female", b.femaleFile);
        OptionalText(files, "male", b.maleFile);
        if (!files.empty()) d["staff_files"] = std::move(files);
        if (const ExeFootprint* grid = m_footprintOf[i]) {
            ordered_json footprint;
            if (!Footprint(b, *grid, footprint)) return false;
            d["footprint"] = std::move(footprint);
        }
        Add("buildings", i, "obl.building", std::move(d));
        return true;
    }

    bool Fail(const std::string& message)
    {
        m_error = "catalog: " + message;
        return false;
    }

    const BuildingList& m_list;
    const std::vector<ExeFootprint>& m_footprints;
    std::vector<const ExeFootprint*> m_footprintOf; // per building id
    std::vector<CatalogFile>& m_files;
    std::string& m_error;
    std::map<std::string, std::vector<std::string>> m_keys;
};

} // namespace

bool CatalogDocuments(
        const BuildingList& list, const std::vector<ExeFootprint>& footprints, std::vector<CatalogFile>& files, std::string& error)
{
    files.clear();
    return Writer(list, footprints, files, error).Run();
}

bool WriteCatalog(const BuildingList& list, const std::vector<ExeFootprint>& footprints, PackSink& out,
        const json_schema::Registry& schemas, int& written, std::string& error)
{
    written = 0;
    std::vector<CatalogFile> files;
    if (!CatalogDocuments(list, footprints, files, error)) return false;
    for (const CatalogFile& file : files) {
        std::string problems;
        if (!formats::ValidateDocument(schemas, file.schema, file.document, problems)) {
            error = file.path + ": " + problems;
            return false;
        }
    }
    for (const CatalogFile& file : files) {
        if (!out.WriteText(file.path, file.document.dump(2) + '\n', error)) return false;
        ++written;
    }
    return true;
}

bool ConvertCatalog(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& written, std::string& error)
{
    BuildingList list;
    if (!ReadBuildingList(source, list, error)) return false;
    const auto exePath = FindFile(source, "BeachLife.exe");
    if (!exePath) {
        error = "BeachLife.exe not found in " + source.Describe("");
        return false;
    }
    auto exe = PeFile::Parse(source.ReadAll(*exePath).value_or(std::vector<uint8_t>()), error);
    std::set<std::string> buildingKeys;
    for (const BlBuilding& building : list.buildings) buildingKeys.insert(building.key);
    std::vector<ExeFootprint> footprints;
    if (!exe || !FindFootprints(*exe, buildingKeys, footprints, error)) {
        error = source.Describe(*exePath) + ": " + error;
        return false;
    }
    return WriteCatalog(list, footprints, out, schemas, written, error);
}

} // namespace obl::convert
