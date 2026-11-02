#include "game/tables/catalog_load.h"
#include "catalog_schemas.h"
#include "core/code_page.h"
#include "core/json_schema.h"
#include "core/pack_path.h"
#include "formats/cell_state.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace obl::catalog {
namespace {
using nlohmann::json;
using tables::WorldParams;

struct Kind {
    const char* name;
    const char* format;
    size_t capacity;
};
constexpr Kind kKinds[] = {{"commodity_families", "obl.commodity_family", WorldParams::kMaxCommodityFamilies},
        {"commodities", "obl.commodity", WorldParams::kMaxCommodities},
        {"opening_hours", "obl.opening_hours", WorldParams::kMaxOpeningHours}, {"desires", "obl.desire", WorldParams::kMaxDesires},
        {"desire_slots", "obl.desire_slot", WorldParams::kDesireSlots}, {"staff", "obl.staff", WorldParams::kMaxStaff},
        {"events", "obl.event", WorldParams::kMaxEvents}, {"beers", "obl.beer", WorldParams::kMaxBeers},
        {"fines", "obl.fine", WorldParams::kMaxFines}, {"vehicles", "obl.vehicle", WorldParams::kMaxVehicles},
        {"tables", "obl.table", WorldParams::kMaxTables}, {"scenery", "obl.scenery", WorldParams::kMaxScenery},
        {"buildings", "obl.building", WorldParams::kMaxBuildings}};

class Reader {
public:
    explicit Reader(const std::filesystem::path& pack) : m_root(pack), m_params(std::make_unique<WorldParams>()) {}

    void Run(WorldParams& out)
    {
        json_schema::Registry schemas;
        std::string error;
        for (const char* schema : kSchemas)
            if (!schemas.Add(json::parse(schema), error)) throw std::runtime_error(error);
        const auto indexFile = pack_path::Resolve(m_root, "catalog/index.json");
        m_current = indexFile.string();
        const json index = Read(indexFile, "obl.catalog_index", schemas);
        for (const Kind& kind : kKinds) {
            const auto& keys = index.at(kind.name);
            if (keys.size() > kind.capacity) throw std::runtime_error(std::string(kind.name) + ": engine capacity exceeded");
            auto& docs = m_docs[kind.name];
            for (const auto& key : keys) {
                const auto file =
                        pack_path::Resolve(m_root, std::filesystem::path("catalog") / kind.name / (key.get<std::string>() + ".json"));
                json doc = Read(file, kind.format, schemas);
                if (doc.at("key") != key) throw std::runtime_error(file.string() + ": key differs from index");
                docs.push_back(std::move(doc));
            }
        }
        Populate();
        out = std::move(*m_params);
    }

private:
    static json Read(const std::filesystem::path& path, const char* format, const json_schema::Registry& schemas)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("cannot open " + path.string());
        json doc = json::parse(file, nullptr, false);
        if (doc.is_discarded()) throw std::runtime_error(path.string() + ": invalid JSON");
        const auto errors = schemas.Validate(format, doc);
        if (!errors.empty()) throw std::runtime_error(path.string() + ": " + errors.front());
        return doc;
    }
    int Ref(const char* kind, const json& value) const
    {
        const auto& docs = m_docs.at(kind);
        for (size_t i = 0; i < docs.size(); ++i)
            if (docs[i].at("key") == value) return static_cast<int>(i);
        throw std::runtime_error(std::string("missing ") + kind + " reference " + value.dump());
    }
    static std::string Text(const json& doc, const std::string& path)
    {
        const json::json_pointer pointer(path);
        if (!doc.contains(pointer)) return {};
        const auto text = doc.at(pointer).get<std::string>();
        const auto narrow = code_page::Narrow(code_page::Utf8ToUtf16(text));
        if (code_page::Utf16ToUtf8(code_page::Widen(narrow)) != text)
            throw std::runtime_error(path + ": text is not representable in the engine code page");
        return narrow;
    }
    static void String(std::string& out, const json& doc, const std::string& path) { out = Text(doc, path); }
    // A string the engine kept in a fixed buffer of `capacity` bytes (NUL included): the load refuses a longer text.
    static void String(std::string& out, const json& doc, const std::string& path, size_t capacity)
    {
        out = Text(doc, path);
        if (out.size() >= capacity) throw std::runtime_error(path + ": engine string capacity exceeded");
    }
    template<size_t N> static void String(char (&out)[N], const json& doc, const std::string& path)
    {
        const auto text = Text(doc, path);
        if (text.size() >= N) throw std::runtime_error(path + ": engine string capacity exceeded");
        std::memcpy(out, text.c_str(), text.size() + 1);
    }
    template<class T> static void Number(T& out, const json& value)
    {
        const double v = value.get<double>();
        if (v < static_cast<double>(std::numeric_limits<T>::lowest()) || v > static_cast<double>(std::numeric_limits<T>::max()))
            throw std::runtime_error("number outside engine range: " + value.dump());
        out = value.get<T>();
    }
    template<class T> static void Pair(T (&out)[2], const json& value)
    {
        Number(out[0], value.at(0));
        Number(out[1], value.at(1));
    }
    template<class T> static void Pair(std::array<T, 2>& out, const json& value)
    {
        Number(out[0], value.at(0));
        Number(out[1], value.at(1));
    }
    static std::vector<std::array<float, 2>> Points(const json& list)
    {
        std::vector<std::array<float, 2>> result(list.size());
        for (size_t i = 0; i < list.size(); ++i) Pair(result[i], list[i]);
        return result;
    }
    template<size_t N> void Refs(int (&out)[N], int& count, const char* kind, const json& list)
    {
        if (list.size() > N) throw std::runtime_error(std::string(kind) + ": engine slot capacity exceeded");
        count = static_cast<int>(list.size());
        for (size_t i = 0; i < list.size(); ++i) out[i] = Ref(kind, list[i]);
    }
    // The same for the vector-backed id lists (the vehicles and events of a building); the vector's size is the engine's slot count.
    void Refs(std::vector<int>& out, int& count, const char* kind, const json& list)
    {
        if (list.size() > out.size()) throw std::runtime_error(std::string(kind) + ": engine slot capacity exceeded");
        count = static_cast<int>(list.size());
        for (size_t i = 0; i < list.size(); ++i) out[i] = Ref(kind, list[i]);
    }
    static int Enum(const json& value, std::initializer_list<const char*> names)
    {
        int i = 0;
        for (const char* name : names) {
            if (value == name) return i;
            ++i;
        }
        throw std::runtime_error("unrecognized enum value " + value.dump());
    }
    static uint32_t Terrain(const json& list)
    {
        uint32_t mask = 0;
        for (const auto& value : list) {
            const int id = Enum(value, {"land", "beach", "shallow_water", "deep_sea", "path"});
            mask |= id == 4 ? 128U : 1U << id;
        }
        return mask;
    }
    template<class Table, class F> void Records(const char* kind, Table& table, int& count, F fill)
    {
        const auto& docs = m_docs.at(kind);
        count = static_cast<int>(docs.size());
        if constexpr (requires { table.resize(docs.size()); }) table.resize(docs.size());
        for (size_t i = 0; i < docs.size(); ++i) {
            m_current = pack_path::Resolve(
                    m_root, std::filesystem::path("catalog") / kind / (docs[i]["key"].template get<std::string>() + ".json"))
                                .string();
            table[i].m_id = static_cast<int>(i);
            String(table[i].m_key, docs[i], "/key");
            fill(table[i], docs[i]);
        }
    }
    void Populate();
    void Table(tables::TableDef& t, const json& d);
    void Building(tables::BuildingDef& b, const json& d);
    void Footprint(tables::BuildingDef& b, const json& d);
    const std::filesystem::path m_root;
    std::unique_ptr<WorldParams> m_params;
    std::map<std::string, std::vector<json>> m_docs;

public:
    std::string m_current;
};

void Reader::Populate()
{
    auto& p = *m_params;
    Records("commodity_families", p.m_commodityFamilies, p.m_numCommodityFamilies,
            [&](auto& r, const auto& d) { String(r.m_name, d, "/name"); });
    Records("commodities", p.m_commodities, p.m_numCommodities, [&](auto& r, const auto& d) {
        String(r.m_name, d, "/name");
        r.m_family = Ref("commodity_families", d.at("family"));
    });
    Records("opening_hours", p.m_openingHours, p.m_numOpeningHours, [&](auto& r, const auto& d) {
        Number(r.m_open, d.at("open"));
        Number(r.m_close, d.at("close"));
    });
    Records("desires", p.m_desires, p.m_numDesires, [&](auto& r, const auto& d) {
        String(r.m_name, d, "/name");
        const auto& families = d.at("families");
        r.m_numFamilies = static_cast<int>(families.size());
        for (const auto& family : families) r.m_families.push_back(Ref("commodity_families", family));
    });
    if (m_docs.at("desire_slots").size() != WorldParams::kDesireSlots)
        throw std::runtime_error("desire_slots: every guest desire slot must be listed");
    int desireSlots = 0;
    Records("desire_slots", p.m_desireSlots, desireSlots, [&](auto& r, const auto& d) { String(r.m_scriptName, d, "/script_name"); });
    Records("staff", p.m_staff, p.m_numStaff, [&](auto& r, const auto& d) {
        String(r.m_Name, d, "/name");
        String(r.m_Sprite, d, "/sprite");
        String(r.m_ObjectType, d, "/script");
        String(r.m_Menu, d, "/menu");
        r.m_female = d.at("gender") == "f";
        Number(r.m_Wage, d.at("wage"));
        Number(r.m_RecommendedWage, d.at("recommended_wage"));
        Number(r.m_scale, d.at("scale"));
        Number(r.m_expectationGrowth, d.at("expectation_growth"));
        Number(r.m_initialHappiness, d.at("initial_happiness"));
        Number(r.m_StartHour, d.at("hours").at(0));
        Number(r.m_EndHour, d.at("hours").at(1));
    });
    Records("events", p.m_events, p.m_numEvents, [&](auto& r, const auto& d) {
        Number(r.m_cost, d.at("cost"));
        Number(r.m_price, d.at("price"));
        Pair(r.m_intensity, d.at("intensity"));
    });
    Records("beers", p.m_beers, p.m_numBeers, [&](auto& r, const auto& d) {
        String(r.m_Brand, d, "/brand");
        String(r.m_Cocktail, d, "/cocktail");
        Number(r.m_BaseCost, d.at("base_cost"));
        Number(r.m_StrCost, d.at("strength_cost"));
        Number(r.m_AlcStr, d.at("alcohol_strength"));
        Number(r.m_Price, d.at("price"));
        r.m_Effect = Enum(d.at("effect"), {"fight", "flirt", "vandalise", "happy", "sad"});
        r.m_scriptFlag = 1;
    });
    Records("fines", p.m_fines, p.m_numFines, [&](auto& r, const auto& d) { Number(r.m_cost, d.at("cost")); });
    Records("vehicles", p.m_vehicles, p.m_numVehicles, [&](auto& r, const auto& d) {
        String(r.m_Name, d, "/name");
        String(r.m_Sprite, d, "/sprite");
        String(r.m_Towed, d, "/towed");
        String(r.m_AnimSet, d, "/anim_set");
        Number(r.m_GridWidth, d.at("size").at(0));
        Number(r.m_GridHeight, d.at("size").at(1));
        Number(r.m_Capacity, d.at("capacity"));
        Number(r.m_MaxSpeed, d.at("max_speed"));
        Number(r.m_Force, d.at("force"));
        Number(r.m_Mass, d.at("mass"));
        r.m_MoveType = static_cast<uint8_t>(1 + Enum(d.at("move_type"), {"points", "boundary"}));
        const auto staff = d.value("staff", json::array());
        r.m_NumStaff = static_cast<int>(staff.size());
        for (const auto& member : staff) r.m_Staff.push_back(Ref("staff", member));
        Pair(r.m_CreationPoint, d.at("creation_point"));
    });
    Records("tables", p.m_tables, p.m_numTables, [&](auto& r, const auto& d) { Table(r, d); });
    Records("scenery", p.m_scenery, p.m_numScenery, [&](auto& r, const auto& d) {
        String(r.m_Name, d, "/name");
        String(r.m_Sprite, d, "/sprite");
        String(r.m_Anim, d, "/anim");
        String(r.m_Menu, d, "/menu");
        r.m_Category = Enum(d.at("category"), {"furniture", "paths", "plants", "ornaments", "trees", "shrubs", "billboard", "other"});
        Number(r.m_GridWidth, d.at("size").at(0));
        Number(r.m_GridHeight, d.at("size").at(1));
        Number(r.m_Cost2Build, d.at("cost"));
        Number(r.m_Icon, d.at("icon"));
        Number(r.m_Capacity, d.at("capacity"));
        Number(r.m_Rating, d.at("rating"));
        Number(r.m_PathFlag, d.at("path_flag"));
        r.m_BuildOn = Terrain(d.at("build_on"));
        r.m_collide = d.at("collide").template get<bool>();
        r.m_clean = d.at("clean").template get<bool>();
        r.m_repair = d.at("repair").template get<bool>();
        r.m_burn = d.at("burn").template get<bool>();
        r.m_userRemove = d.at("user_remove").template get<bool>();
        r.m_vegFlag = d.at("vegetation").template get<bool>();
        r.m_water = d.at("water").template get<bool>();
    });
    Records("buildings", p.m_buildings, p.m_numBuildings, [&](auto& r, const auto& d) { Building(r, d); });
}

void Reader::Table(tables::TableDef& t, const json& d)
{
    String(t.m_Name, d, "/name");
    String(t.m_TableSprite, d, "/table_sprite");
    String(t.m_ChairSprite, d, "/chair_sprite");
    String(t.m_SitDownAnim, d, "/sit_down_anim");
    String(t.m_GetUpAnim, d, "/get_up_anim");
    Number(t.m_GridWidth, d.at("size").at(0));
    Number(t.m_GridHeight, d.at("size").at(1));
    const auto& walkPoints = d.at("walk_points");
    t.m_NumWalkPts = static_cast<int>(walkPoints.size());
    t.m_walkPoints.resize(walkPoints.size());
    for (size_t i = 0; i < walkPoints.size(); ++i) Pair(t.m_walkPoints[i], walkPoints[i]);
    const auto& chairs = d.at("chairs");
    t.m_NumChairs = static_cast<uint32_t>(chairs.size());
    t.m_NumChairPos = t.m_NumChairDirs = t.m_NumChairWalkLists = static_cast<int>(chairs.size());
    t.m_chairPositions.resize(chairs.size());
    t.m_chairDirections.resize(chairs.size());
    t.m_chairWalkPoints.resize(chairs.size());
    for (size_t i = 0; i < chairs.size(); ++i) {
        const auto& c = chairs[i];
        Pair(t.m_chairPositions[i], c.at("pos"));
        t.m_chairDirections[i] = Enum(c.at("dir"), {"n", "ne", "e", "se", "s", "sw", "w", "nw"});
        const auto& walk = c.at("walk_points");
        auto& list = t.m_chairWalkPoints[i];
        list.assign(walk.size() + 1, 0);
        list[0] = static_cast<int>(walk.size());
        for (size_t j = 0; j < walk.size(); ++j) Number(list[j + 1], walk[j]);
        t.m_ChairWalkIdx = static_cast<int>(walk.size() + 1);
        if (c.contains("serve_point")) {
            if (i != static_cast<size_t>(t.m_NumServePoints)) throw std::runtime_error("chair serve points must form a prefix");
            if (t.m_servePoints.empty()) t.m_servePoints.resize(chairs.size());
            Pair(t.m_servePoints[i], c.at("serve_point"));
            ++t.m_NumServePoints;
        }
    }
    const auto anims = d.value("sat_down_anims", json::array());
    t.m_NumSatDownAnims = static_cast<int>(anims.size());
    t.m_satDownAnims.resize(anims.size());
    for (size_t i = 0; i < anims.size(); ++i) t.m_satDownAnims[i] = Text(d, "/sat_down_anims/" + std::to_string(i));
    t.m_serveTable = d.value("serve_table", false);
    t.m_hasPlates = d.value("has_waiter", false);
    if (d.contains("table_offset")) {
        Number(t.m_XOffsetTable, d["table_offset"][0]);
        Number(t.m_YOffsetTable, d["table_offset"][1]);
    }
    t.m_waiterSprite = Text(d, "/waiter/sprite");
    t.m_waiterWalkAnim = Text(d, "/waiter/walk_anim");
    t.m_waiterServeAnim = Text(d, "/waiter/serve_anim");
    t.m_callWaiterAnim = Text(d, "/waiter/call_anim");
    t.m_waitWaiterAnim = Text(d, "/waiter/wait_anim");
}

void Reader::Building(tables::BuildingDef& b, const json& d)
{
    String(b.m_Name, d, "/name", 0x80);
    String(b.m_Sprite, d, "/sprite", 0x100);
    String(b.m_Tree, d, "/requires", 0x40);
    String(b.m_Menu, d, "/menu", 0x20);
    String(b.m_Construct, d, "/construction/anim", 0x33);
    b.m_customConstruct = d.at("construction").contains("anim");
    String(b.m_MaleFileName, d, "/staff_files/male", 0x40);
    String(b.m_FemaleFileName, d, "/staff_files/female", 0x40);
    b.m_Category = Enum(d.at("category"), {"hotel", "food", "leisure", "other"});
    if (!BuildingBehaviorFromName(d.at("behavior").get<std::string>(), b.m_Behavior))
        throw std::runtime_error("unrecognized behavior " + d.at("behavior").dump());
    Number(b.m_Icon, d.at("icon"));
    Number(b.m_Cost2Build, d.at("cost").at("build"));
    Number(b.m_RefundValue, d.at("cost").at("refund"));
    Number(b.m_BuildTime, d.at("construction").at("build_time"));
    Number(b.m_DemolishTime, d.at("construction").at("demolish_time"));
    Number(b.m_MaxBuilders, d.at("construction").at("max_builders"));
    Number(b.m_GridWidth, d.at("size").at(0));
    Number(b.m_GridHeight, d.at("size").at(1));
    b.m_BuildOn = Terrain(d.at("build_on"));
    const auto& points = d.at("points");
    Pair(b.m_ServeOffset, points.at("serve"));
    Pair(b.m_StaffOffset, points.at("staff"));
    Pair(b.m_EntranceOffset, points.at("entrance"));
    if (points.contains("walk")) {
        b.m_NumWalkPoints = static_cast<int>(points["walk"].size());
        b.m_walkPoints = Points(points["walk"]);
    }
    const auto plates = d.value("baseplates", json::array());
    b.m_NumBasePlates = static_cast<int>(plates.size());
    b.m_basePlateTypes.assign(plates.size(), 0);
    for (size_t i = 0; i < plates.size(); ++i) Number(b.m_basePlateTypes[i], plates[i]);
    const auto& op = d.at("operation");
    Number(b.m_GuestCapacity, op.at("guest_capacity"));
    Number(b.m_MaxInstances, op.at("max_instances"));
    Number(b.m_MaxQueue, op.at("max_queue"));
    Number(b.m_Power, op.at("power"));
    b.m_OpeningHours = Ref("opening_hours", op.at("opening_hours"));
    b.m_Attractor = Ref("commodity_families", op.at("attractor"));
    Number(b.m_MaxStaff, op.at("staff").at("max"));
    Refs(b.m_Staff, b.m_NumStaff, "staff", op.at("staff").at("types"));
    const auto commodities = d.value("commodities", json::array());
    if (commodities.size() > std::size(b.m_Commodity)) throw std::runtime_error("too many building commodities");
    b.m_NumCommodities = static_cast<int>(commodities.size());
    for (size_t i = 0; i < commodities.size(); ++i) {
        const auto& c = commodities[i];
        b.m_Commodity[i] = Ref("commodities", c.at("commodity"));
        Pair(b.m_CommodityAB[i], c.at("intensity"));
        Number(b.m_CommodityC[i], c.at("daily_intensity_factor"));
        Number(b.m_CommodityCost[i], c.at("price"));
        Number(b.m_CommodityE[i], c.at("daily_price_factor"));
        Number(b.m_CommodityF[i], c.at("cost"));
    }
    Number(b.m_AvFailTime, d.at("failure").at("average_time"));
    Number(b.m_FailPercent, d.at("failure").at("percent"));
    Number(b.m_FailAge, d.at("failure").at("age"));
    Refs(b.m_Vehicle, b.m_NumVehicles, "vehicles", d.value("vehicles", json::array()));
    Refs(b.m_Event, b.m_NumEvents, "events", d.value("events", json::array()));
    const auto places = d.value("vehicle_places", json::array());
    if (places.size() > std::size(b.m_VehicleDir)) throw std::runtime_error("too many vehicle places");
    b.m_NumVehiclePlaces = static_cast<int>(places.size());
    for (size_t i = 0; i < places.size(); ++i) {
        Number(b.m_VehicleDir[i], places[i].at("dir"));
        Pair(b.m_VehicleGridOffset[i], places[i].at("offset"));
    }
    const auto tables = d.value("tables", json::array());
    if (tables.size() > std::size(b.m_Table)) throw std::runtime_error("too many building tables");
    b.m_NumTables = static_cast<int>(tables.size());
    for (size_t i = 0; i < tables.size(); ++i) {
        b.m_Table[i] = Ref("tables", tables[i].at("table"));
        Pair(b.m_TablePos[i], tables[i].at("pos"));
    }
    const auto anims = d.value("anims", json::array());
    if (anims.size() > static_cast<size_t>(tables::kBuildingAnimSprites)) throw std::runtime_error("too many building animations");
    b.m_NumAnimSprites = static_cast<int>(anims.size());
    b.m_AnimSprites.assign(anims.size(), {});
    for (size_t i = 0; i < anims.size(); ++i) {
        auto& a = b.m_AnimSprites[i];
        String(a.m_Sprite, d, "/anims/" + std::to_string(i) + "/sprite");
        for (size_t j = 0; j < 4; ++j) {
            Number(a.m_X[j], anims[i].at("points").at(j).at(0));
            Number(a.m_Y[j], anims[i].at("points").at(j).at(1));
        }
        a.m_animated = anims[i].at("flag").get<bool>();
    }
    if (d.contains("footprint")) Footprint(b, d.at("footprint"));
}

// The placement grid (tables::BuildingFootprint): the engine reads only the (height + 2) x (width + 2) area of each layer; the
// converter writes nothing else, so the rest of the grid stays zero.
void Reader::Footprint(tables::BuildingDef& b, const json& d)
{
    static const char* const kLayers[] = {"placement", "work", "depth", "body"};
    constexpr size_t kColumns = 25, kLayerRows = 25, kLayerCount = 4;
    const size_t width = d.at("size").at(0).get<size_t>();
    const size_t height = d.at("size").at(1).get<size_t>();
    if (width < 1 || height < 1 || width + 2 > kColumns || height + 2 > kLayerRows) throw std::runtime_error("footprint size");
    auto grid = std::make_shared<tables::BuildingFootprint>();
    grid->m_width = static_cast<int>(width);
    grid->m_height = static_cast<int>(height);
    for (size_t layer = 0; layer < kLayerCount; ++layer) {
        const json& rows = d.at("layers").at(kLayers[layer]);
        if (rows.size() != height + 2) throw std::runtime_error("footprint rows");
        for (size_t row = 0; row < rows.size(); ++row) {
            const json& cells = rows.at(row);
            if (cells.size() != width + 2) throw std::runtime_error("footprint columns");
            for (size_t column = 0; column < cells.size(); ++column)
                grid->m_layers[layer][row][column] =
                        static_cast<int32_t>(layer < 2 ? formats::CellWordFromRecord(cells.at(column)) : cells.at(column).get<uint32_t>());
        }
    }
    b.m_footprint = std::move(grid);
}
} // namespace

bool LoadPack(const std::filesystem::path& pack, WorldParams& out, std::string& error)
{
    error.clear();
    Reader reader(pack);
    try {
        reader.Run(out);
        return true;
    } catch (const std::exception& failure) {
        error = (reader.m_current.empty() ? (pack / "catalog").string() : reader.m_current) + ": " + failure.what();
        return false;
    }
}
} // namespace obl::catalog
