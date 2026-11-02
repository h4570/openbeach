#include "importer/src/readers/save/save_body.h"
#include "formats/save_document.h"
#include "formats/save_records.h"
#include "importer/src/readers/save/save_writer.h"
#include "formats/schemas.h"
#include "old_save_builder.h"
#include "unit_test.h"

#include "core/json_schema.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using nlohmann::json;
using obl::convert::OldSaveBody;
using obl::formats::CheckOblSaveDocument;
using obl::test::Save;

obl::convert::SaveOrigin Origin()
{
    obl::convert::SaveOrigin origin;
    origin.file = "save1.dat";
    origin.packHash = std::string(64, '0');
    return origin;
}

const json kIndex{{"buildings", json::array({"hut"})}, {"events", json::array({"singing"})}, {"tables", json::array({"cafetable1"})}};

// A converted synthetic save with a scenery object, a building `hut`, a guest and a table, all managers filled. The
// builder's ids are not those of one consistent world, so the fixture makes them so: the next free id past the guests,
// the show's actor the guest, the repair job the building and the table owned by the building.
json Document()
{
    const Save save = Save::Make(1, {}, 4, "x", true, {2, 3, 5});
    OldSaveBody body;
    nlohmann::ordered_json document;
    std::string error;
    if (!obl::convert::ReadOldSaveBody(save.data, body, error) || !obl::convert::OldSaveDocument(body, kIndex, Origin(), document, error))
        std::fprintf(stderr, "%s\n", error.c_str());
    json d = json::parse(document.dump());
    d["managers"]["objects"]["next_id"] = 5000;
    json* building = nullptr;
    json* guest = nullptr;
    json* table = nullptr;
    for (json& o : d["objects"]) {
        if (o["kind"] == "building") building = &o;
        if (o["kind"] == "guest") guest = &o;
        if (o["kind"] == "table") table = &o;
    }
    if (!building || !guest || !table) return d;
    json& b = (*building)["building"];
    b["queue"]["owner"] = b["events"]["owner"] = (*building)["id"];
    b["queue"]["guests"][0] = b["events"]["slots"][0]["actors"][0] = (*guest)["id"];
    (*table)["table"]["owner"] = (*building)["id"];
    d["managers"]["construction"]["jobs"][0]["building"] = (*building)["id"];
    for (json& builder : d["managers"]["construction"]["builders"]) builder["staff"] = -1;
    return d;
}

json& ObjectOf(json& d, const char* kind)
{
    for (json& o : d["objects"])
        if (o["kind"] == kind) return o;
    static json none;
    return none;
}

bool Refused(const json& d, std::string_view what, uint32_t gridSide = 2)
{
    const auto checks = obl::formats::CheckOblSaveDocument(d, Origin(), gridSide);
    for (const std::string& refusal : checks.refusals)
        if (refusal.find(what) != std::string::npos) return true;
    std::fprintf(stderr, "no refusal \"%.*s\"; got %zu:\n", int(what.size()), what.data(), checks.refusals.size());
    for (const std::string& refusal : checks.refusals) std::fprintf(stderr, "  %s\n", refusal.c_str());
    return false;
}

bool LoadRepoSchemas(obl::json_schema::Registry& registry)
{
    std::string error;
    const bool ok = obl::formats::LoadSchemaDirectory(fs::path(OBL_REPO_DIR) / "data" / "schemas", registry, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    return ok;
}
} // namespace

OBL_TEST_CASE("convert.save_checks_accept_a_consistent_document")
{
    const json d = Document();
    OBL_REQUIRE(d["objects"].size() == 4);
    const auto checks = obl::formats::CheckOblSaveDocument(d, Origin(), 2);
    for (const std::string& refusal : checks.refusals) std::fprintf(stderr, "%s\n", refusal.c_str());
    OBL_CHECK(checks.refusals.empty());
    OBL_CHECK(checks.warnings.empty());

    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    OldSaveBody body;
    std::vector<std::string> warnings;
    std::string error;
    const bool loaded = obl::convert::LoadOblSaveDocument(d, kIndex, Origin(), schemas, body, warnings, error, 2);
    if (!loaded) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(loaded);
    OBL_CHECK(obl::convert::CheckOldSaveBody(body).empty());
}

OBL_TEST_CASE("convert.save_checks_refuse_other_packs_and_warn_on_another_build")
{
    json d = Document();
    d["header"]["packs"][0]["id"] = "other-pack";
    OBL_CHECK(Refused(d, "the save needs pack other-pack, which is not installed"));
    OBL_CHECK(Refused(d, "does not name the installed pack beachlife-2002"));
    d = Document();
    d["header"]["packs"].push_back({{"id", "hd-mod"}, {"content_version", "1.0.0"}, {"pack_hash", std::string(64, '1')}});
    OBL_CHECK(Refused(d, "needs pack hd-mod"));

    d = Document();
    d["header"]["packs"][0]["pack_hash"] = std::string(64, 'a');
    auto checks = obl::formats::CheckOblSaveDocument(d, Origin(), 2);
    OBL_CHECK(checks.refusals.empty());
    OBL_REQUIRE(checks.warnings.size() == 1);
    OBL_CHECK(checks.warnings[0].find("another pack.json") != std::string::npos);
    d["header"]["packs"][0]["content_version"] = "0.9.0";
    checks = obl::formats::CheckOblSaveDocument(d, Origin(), 2);
    OBL_CHECK(checks.refusals.empty());
    OBL_REQUIRE(checks.warnings.size() == 1);
    OBL_CHECK(checks.warnings[0].find("saved with beachlife-2002 0.9.0, the installed pack is 1.0.0") != std::string::npos);
}

OBL_TEST_CASE("convert.save_checks_refuse_a_map_of_another_size")
{
    json d = Document();
    OBL_CHECK(Refused(d, "map.cells: 2 x 2, the game's grid is 128 x 128", obl::formats::kGameGridSide));
    d["map"]["cells"] = {2, 3};
    OBL_CHECK(Refused(d, "map.cells: 2 x 3 is not a square grid"));
    d["map"]["cells"] = {0, 0};
    OBL_CHECK(Refused(d, "map.cells: 0 x 0 is not a square grid", 0));
    d = Document();
    d["map"]["heights"].erase(0);
    OBL_CHECK(Refused(d, "map.heights: 3 entries for 2 x 2 cells"));
    d = Document();
    d["map"]["cell_state"]["grass"].push_back({1, 0});
    OBL_CHECK(Refused(d, "map.cell_state.grass: 5 cells for 2 x 2 cells"));
    d = Document();
    d["map"].erase("areas_open");
    OBL_CHECK(Refused(d, "map.areas_open: missing"));
}

OBL_TEST_CASE("convert.save_checks_refuse_duplicate_out_of_range_and_misordered_ids")
{
    json d = Document();
    ObjectOf(d, "table")["id"] = ObjectOf(d, "scenery")["id"];
    OBL_CHECK(Refused(d, "objects: duplicate id 1"));
    d = Document();
    d["managers"]["objects"]["next_id"] = 1000; // the guest is 1000
    OBL_CHECK(Refused(d, "objects: guest 1000: id outside 0..1000"));
    d = Document();
    ObjectOf(d, "scenery")["id"] = -5;
    OBL_CHECK(Refused(d, "id outside"));
    d = Document();
    std::swap(d["objects"][0], d["objects"][1]); // the guest before the building
    OBL_CHECK(Refused(d, "objects: building 3000 after the guests"));
    d = Document();
    json building = ObjectOf(d, "building");
    for (int i = 0; i < 500; ++i) {
        building["id"] = 4000 + i;
        building["building"]["queue"]["owner"] = building["building"]["events"]["owner"] = 4000 + i;
        d["objects"].insert(d["objects"].begin(), building);
    }
    OBL_CHECK(Refused(d, "objects: more than 500 buildings"));
}

OBL_TEST_CASE("convert.save_checks_refuse_broken_references")
{
    const auto refused = [](const char* what, auto&& change) {
        json d = Document();
        change(d);
        return Refused(d, what);
    };
    OBL_CHECK(refused("objects[id 3000].building.queue.owner: 1 is not the building's own id",
            [](json& d) { ObjectOf(d, "building")["building"]["queue"]["owner"] = 1; }));
    OBL_CHECK(refused("objects[id 3000].building.events.owner: 7 is not the building's own id",
            [](json& d) { ObjectOf(d, "building")["building"]["events"]["owner"] = 7; }));
    OBL_CHECK(refused("objects[id 3000].building.queue.guests: object 1 is a scenery, not a guest",
            [](json& d) { ObjectOf(d, "building")["building"]["queue"]["guests"][0] = 1; }));
    OBL_CHECK(refused("objects[id 3000].building.events.slots[].actors: no object 42",
            [](json& d) { ObjectOf(d, "building")["building"]["events"]["slots"][0]["actors"][0] = 42; }));
    OBL_CHECK(
            refused("table.owner: object 1000 is a guest, not a building", [](json& d) { ObjectOf(d, "table")["table"]["owner"] = 1000; }));
    OBL_CHECK(refused("managers.construction.jobs[].building: object 1000 is a guest, not a building",
            [](json& d) { d["managers"]["construction"]["jobs"][0]["building"] = 1000; }));
    OBL_CHECK(refused("managers.construction.builders[].staff: object 3000 is a building, not a builder",
            [](json& d) { d["managers"]["construction"]["builders"][0]["staff"] = 3000; }));
    // -1 is "none" everywhere.
    json d = Document();
    ObjectOf(d, "table")["table"]["owner"] = -1;
    ObjectOf(d, "building")["building"]["queue"]["guests"][0] = -1;
    OBL_CHECK(obl::formats::CheckOblSaveDocument(d, Origin(), 2).refusals.empty());
    // A table and repair/cleaning jobs may outlive their removed object and keep its id.
    ObjectOf(d, "table")["table"]["owner"] = 2999;
    d["managers"]["repair"]["building_jobs"] =
            json::array({{{"object", 77}, {"calls", 1}, {"distance", 0}, {"time_called", 0}, {"urgent", false}, {"type", 0}}});
    d["managers"]["cleaning"]["active_jobs"] = json::array({{{"object", 78}, {"cleaning", false}, {"urgent", false}}});
    OBL_CHECK(obl::formats::CheckOblSaveDocument(d, Origin(), 2).refusals.empty());
}

OBL_TEST_CASE("convert.save_checks_run_before_the_mapping")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    OldSaveBody body;
    std::vector<std::string> warnings;
    std::string error;
    json d = Document();
    d["objects"][0]["id"] = d["objects"][1]["id"];
    OBL_CHECK(!obl::convert::LoadOblSaveDocument(d, kIndex, Origin(), schemas, body, warnings, error, 2));
    OBL_CHECK(error.find("duplicate id") != std::string::npos);
    OBL_CHECK(body.objects.empty());
    d = Document();
    d["map"].erase("cell_state"); // the schema first: the checks read required keys without testing for them
    OBL_CHECK(!obl::convert::LoadOblSaveDocument(d, kIndex, Origin(), schemas, body, warnings, error, 2));
    OBL_CHECK(error.starts_with("obl.save schema: "));
    d = Document();
    d["header"]["packs"][0]["pack_hash"] = std::string(64, 'b');
    OBL_CHECK(obl::convert::LoadOblSaveDocument(d, kIndex, Origin(), schemas, body, warnings, error, 2));
    OBL_CHECK(warnings.size() == 1);
}

OBL_TEST_CASE("convert.save_validate_needs_no_mapping")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    std::vector<std::string> warnings;
    std::string error;
    json d = Document();
    OBL_CHECK(obl::formats::ValidateOblSaveDocument(d, Origin(), schemas, warnings, error, 2));
    d["objects"][0]["id"] = d["objects"][1]["id"];
    OBL_CHECK(!obl::formats::ValidateOblSaveDocument(d, Origin(), schemas, warnings, error, 2));
    OBL_CHECK(error.find("duplicate id") != std::string::npos);
}

namespace {
const obl::convert::OldField* SectionField(const OldSaveBody& body, std::string_view section, const char* label)
{
    for (const auto& block : body.sections)
        if (block.kind == section) return block.Find(label);
    return nullptr;
}
} // namespace

// Every field the schema leaves optional in the sections (world, view, map, managers) may be left out: the bridge maps
// the default instead (save-load-bridge.md, "Defaults").
OBL_TEST_CASE("convert.save_bridge_defaults_missing_optional_fields")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    std::ifstream in(fs::path(OBL_REPO_DIR) / "data" / "schemas" / "save.schema.json", std::ios::binary);
    const json schema = json::parse(in);
    const json full = Document();
    const auto optional = [](const json& node) {
        std::vector<std::string> keys;
        const json required = node.value("required", json::array());
        for (const auto& [key, value] : node["properties"].items())
            if (std::find(required.begin(), required.end(), key) == required.end()) keys.push_back(key);
        return keys;
    };
    std::vector<std::vector<std::string>> paths; // each optional key with the path to it
    for (const std::string& key : optional(schema)) paths.push_back({key});
    for (const char* section : {"world", "view", "map", "managers"})
        for (const std::string& key : optional(schema["properties"][section])) paths.push_back({section, key});
    for (const auto& [manager, node] : schema["properties"]["managers"]["properties"].items())
        if (node.contains("properties"))
            for (const std::string& key : optional(node)) paths.push_back({"managers", manager, key});
    int dropped = 0;
    for (const auto& path : paths) {
        json d = full;
        json* parent = &d;
        for (size_t i = 0; i + 1 < path.size() && parent; ++i) parent = parent->contains(path[i]) ? &(*parent)[path[i]] : nullptr;
        if (!parent || !parent->contains(path.back())) continue;
        parent->erase(path.back());
        ++dropped;
        OldSaveBody body;
        std::vector<std::string> warnings;
        std::string error, name;
        for (const auto& part : path) name += "." + part;
        const bool loaded = obl::convert::LoadOblSaveDocument(d, kIndex, Origin(), schemas, body, warnings, error, 2);
        if (name == ".map.areas_open") { // optional in the schema, but the game cannot load without it
            OBL_CHECK(!loaded);
            OBL_CHECK(error.starts_with("map.areas_open: missing"));
            continue;
        }
        if (!loaded) std::fprintf(stderr, "without %s: %s\n", name.c_str(), error.c_str());
        OBL_CHECK(loaded);
    }
    OBL_CHECK(dropped > 80);

    // The values: zero, and the next free id (required in managers.objects) one past the largest object id without it.
    json d = full;
    d["world"]["day_time"] = 7.5;
    d["managers"].erase("objects");
    OldSaveBody body, defaulted;
    std::string error;
    OBL_REQUIRE(obl::convert::OldSaveBodyFromDocument(d, kIndex, Origin(), body, error));
    d["world"].erase("day_time");
    d.erase("view");
    OBL_REQUIRE(obl::convert::OldSaveBodyFromDocument(d, kIndex, Origin(), defaulted, error));
    OBL_REQUIRE(SectionField(defaulted, "ObjectManager", "m_dwNextFreeObjectID"));
    OBL_CHECK(SectionField(defaulted, "ObjectManager", "m_dwNextFreeObjectID")->record.Bits() == 3001); // the building is 3000
    OBL_REQUIRE(SectionField(body, "World", "m_GameDayTime"));
    OBL_CHECK(SectionField(body, "World", "m_GameDayTime")->record.Bits() == 0x40f00000u);
    OBL_CHECK(SectionField(defaulted, "World", "m_GameDayTime")->record.Bits() == 0);
    OBL_REQUIRE(SectionField(defaulted, "ZEngine", "m_MapX"));
    OBL_CHECK(SectionField(defaulted, "ZEngine", "m_MapX")->record.Bits() == 0);
}

// An object of identity fields alone loads with the schema's defaults, and a dropped field of any kept block takes its own.
OBL_TEST_CASE("convert.save_bridge_defaults_missing_object_state")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    const json full = Document();
    const auto load = [&](const json& d, OldSaveBody& body) {
        std::vector<std::string> warnings;
        std::string error;
        const bool loaded = obl::convert::LoadOblSaveDocument(d, kIndex, Origin(), schemas, body, warnings, error, 2);
        if (!loaded) std::fprintf(stderr, "object defaults: %s\n", error.c_str());
        return loaded;
    };

    json bare = full;
    for (json& object : bare["objects"]) {
        json identity = json::object();
        for (const char* key : {"id", "kind", "type", "class"})
            if (object.contains(key)) identity[key] = object[key];
        object = std::move(identity);
    }
    OldSaveBody body;
    OBL_CHECK(load(bare, body));

    int dropped = 0;
    for (size_t i = 0; i < full["objects"].size(); ++i)
        for (const auto& [block, fields] : full["objects"][i].items()) {
            if (!fields.is_object()) continue;
            for (const auto& [key, value] : fields.items()) {
                if (key == "chairs") continue; // its plates stay: the mapping refuses a table whose counts differ
                json d = full;
                d["objects"][i][block].erase(key);
                OBL_CHECK(load(d, body));
                ++dropped;
            }
        }
    OBL_CHECK(dropped > 300);

    // The non-zero defaults: object references are none.
    json object = {{"id", 1}, {"kind", "guest"}, {"type", "x"}};
    json d = {{"objects", json::array({object})}};
    obl::formats::FillSaveDefaults(d, schemas);
    const json& filled = d["objects"][0];
    OBL_CHECK(filled["moving"]["follow_object"] == -1);
    OBL_CHECK(filled["guest"]["staying_at"] == -1);
    OBL_CHECK(filled["pos"] == json::array({0.0, 0.0, 0.0}));
    OBL_CHECK(filled["sprite"]["frame"] == 0);
    OBL_CHECK(filled["guest"]["inventory"].empty()); // RecordsToSlots fills the list's slots
    OBL_CHECK(!filled.contains("building"));

    json building = {
            {"objects", json::array({{{"id", 7}, {"kind", "building"}, {"type", "x"}, {"building", {{"queue", json::object()}}}}})}};
    obl::formats::FillSaveDefaults(building, schemas);
    OBL_CHECK(building["objects"][0]["building"]["queue"]["owner"] == 7); // the queue is its building's
}
