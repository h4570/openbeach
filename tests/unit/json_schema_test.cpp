#include "core/json_schema.h"
#include "unit_test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

// The validator subset (src/core/json_schema.h) and the schemas of data/schemas/ against the hand-made samples of
// tests/schema_samples/ (docs/architecture/data-formats/schemas.md).

using nlohmann::json;
using obl::json_schema::Registry;

namespace {

namespace fs = std::filesystem;

const fs::path kSchemaDir = fs::path(OBL_REPO_DIR) / "data" / "schemas";
const fs::path kSampleDir = fs::path(OBL_REPO_DIR) / "tests" / "schema_samples";

json ReadJson(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return json::parse(text.str(), nullptr, /*allow_exceptions=*/false);
}

// Files of `dir` with the given suffix, by name, so failures come in a stable order.
std::vector<fs::path> FilesEndingWith(const fs::path& dir, const std::string& suffix)
{
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Every schema of data/schemas/; `ids` maps each file stem to its $id.
bool LoadSchemas(Registry& registry, std::map<std::string, std::string>* ids = nullptr)
{
    bool ok = true;
    for (const auto& path : FilesEndingWith(kSchemaDir, ".schema.json")) {
        json schema = ReadJson(path);
        const std::string name = path.filename().string();
        const std::string stem = name.substr(0, name.size() - std::string(".schema.json").size());
        if (schema.is_discarded()) {
            obl::unit::ReportFailure(__FILE__, __LINE__, name + ": not valid JSON");
            ok = false;
            continue;
        }
        const std::string id = schema.value("$id", "");
        std::string error;
        if (!registry.Add(std::move(schema), error)) {
            obl::unit::ReportFailure(__FILE__, __LINE__, name + ": " + error);
            ok = false;
            continue;
        }
        if (ids) (*ids)[stem] = id;
    }
    return ok;
}

std::string Join(const std::vector<std::string>& errors)
{
    std::string text;
    for (const auto& e : errors) text += "\n    " + e;
    return text;
}

Registry Inline(std::initializer_list<const char*> schemas)
{
    Registry registry;
    for (const char* text : schemas) {
        std::string error;
        if (!registry.Add(json::parse(text), error)) obl::unit::ReportFailure(__FILE__, __LINE__, error);
    }
    return registry;
}

bool Valid(const Registry& registry, const char* instance, const std::string& id = "t")
{
    return registry.Validate(id, json::parse(instance)).empty();
}

} // namespace

OBL_TEST_CASE("schema.validator_types_and_objects")
{
    Registry r = Inline({R"({"$id": "t", "type": "object", "properties": {"a": {"type": "integer"}, "b": {"type": ["string", "null"]}},
        "required": ["a"], "additionalProperties": false})"});
    OBL_CHECK(Valid(r, R"({"a": 1})"));
    OBL_CHECK(Valid(r, R"({"a": 2.0, "b": null})")); // 2.0 is an integer in JSON Schema
    OBL_CHECK(!Valid(r, R"({"a": 2.5})"));
    OBL_CHECK(!Valid(r, R"({"b": "x"})"));
    OBL_CHECK(!Valid(r, R"({"a": 1, "c": 0})"));
    OBL_CHECK(!Valid(r, R"({"a": 1, "b": 3})"));
    OBL_CHECK(!Valid(r, R"([1])"));
    const auto errors = r.Validate("t", json::parse(R"({"a": 1, "c": 0})"));
    OBL_REQUIRE(errors.size() == 1);
    OBL_CHECK_EQ(errors[0], std::string("/c: unrecognized property"));
}

OBL_TEST_CASE("schema.validator_arrays_and_bounds")
{
    Registry r = Inline({R"({"$id": "t", "type": "array", "prefixItems": [{"type": "string"}], "items": {"type": "number", "minimum": 0,
        "exclusiveMaximum": 10}, "minItems": 2, "maxItems": 3, "uniqueItems": true})"});
    OBL_CHECK(Valid(r, R"(["x", 0, 9.5])"));
    OBL_CHECK(!Valid(r, R"(["x"])"));
    OBL_CHECK(!Valid(r, R"(["x", 1, 2, 3])"));
    OBL_CHECK(!Valid(r, R"(["x", 10])"));
    OBL_CHECK(!Valid(r, R"(["x", -1])"));
    OBL_CHECK(!Valid(r, R"([1, 2])"));
    OBL_CHECK(!Valid(r, R"(["x", 2, 2])"));
}

OBL_TEST_CASE("schema.validator_strings_enums_and_maps")
{
    Registry r = Inline({R"({"$id": "t", "type": "object", "propertyNames": {"pattern": "^[a-z]+$"},
        "patternProperties": {"^x": {"const": 1}}, "additionalProperties": {"enum": ["a", "b"]},
        "properties": {"name": {"type": "string", "minLength": 2, "maxLength": 3}}})"});
    // Lengths count code points, not bytes: U+017C is two bytes in UTF-8.
    OBL_CHECK(r.Validate("t",
                       json{{"xa", 1}, {"y", "a"},
                               {"name",
                                       "\xC5\xBC"
                                       "ab"}})
                    .empty());
    OBL_CHECK(!Valid(r, R"({"xa": 2})"));
    OBL_CHECK(!Valid(r, R"({"y": "c"})"));
    OBL_CHECK(!Valid(r, R"({"Y": "a"})"));
    OBL_CHECK(!Valid(r, R"({"name": "a"})"));
    OBL_CHECK(!Valid(r, R"({"name": "abcd"})"));
}

OBL_TEST_CASE("schema.validator_combinators_and_refs")
{
    Registry r = Inline({R"({"$id": "lib", "$defs": {"even": {"enum": [0, 2, 4]}, "small": {"maximum": 2}}})",
            R"({"$id": "t", "$defs": {"local": {"$ref": "lib#/$defs/even"}},
                "oneOf": [{"$ref": "#/$defs/local"}, {"$ref": "lib#/$defs/small"}]})",
            R"({"$id": "any", "anyOf": [{"type": "string"}, {"$ref": "lib#/$defs/even"}]})"});
    OBL_CHECK(Valid(r, R"("s")", "any"));
    OBL_CHECK(!Valid(r, "1", "any"));
    OBL_CHECK(Valid(r, "4")); // even only
    OBL_CHECK(Valid(r, "1")); // small only
    OBL_CHECK(!Valid(r, "2")); // both: oneOf fails
    OBL_CHECK(!Valid(r, "3")); // neither
    OBL_CHECK(r.CheckRefs().empty());

    Registry broken = Inline({R"({"$id": "b", "$ref": "missing#/$defs/x"})"});
    const auto unresolved = broken.CheckRefs();
    OBL_REQUIRE(unresolved.size() == 1);
    OBL_CHECK_EQ(unresolved[0], std::string("b: missing#/$defs/x"));
    OBL_CHECK(!Valid(broken, "1", "b"));
}

OBL_TEST_CASE("schema.validator_refuses_unsupported_schemas")
{
    Registry r;
    std::string error;
    OBL_CHECK(!r.Add(json::parse(R"({"type": "object"})"), error)); // no $id
    OBL_CHECK(!r.Add(json::parse(R"({"$id": "a", "if": {"type": "string"}})"), error));
    OBL_CHECK_EQ(error, std::string("a#/if: keyword not supported by the validator"));
    OBL_CHECK(!r.Add(json::parse(R"({"$id": "a", "properties": {"x": {"format": "date"}}})"), error));
    OBL_CHECK_EQ(error, std::string("a#/properties/x/format: keyword not supported by the validator"));
    OBL_CHECK(!r.Add(json::parse(R"({"$id": "a", "type": "float"})"), error));
    OBL_CHECK(!r.Add(json::parse(R"({"$id": "a", "pattern": "("})"), error));
    OBL_CHECK(!r.Add(json::parse(R"({"$id": "a", "minItems": -1})"), error));
    OBL_CHECK(r.Add(json::parse(R"({"$id": "a", "title": "ok", "type": "string"})"), error));
    OBL_CHECK(!r.Add(json::parse(R"({"$id": "a"})"), error)); // duplicate
}

OBL_TEST_CASE("schema.data_schemas_are_consistent")
{
    Registry registry;
    std::map<std::string, std::string> ids;
    OBL_REQUIRE(LoadSchemas(registry, &ids));
    OBL_REQUIRE(!ids.empty());
    const auto unresolved = registry.CheckRefs();
    if (!unresolved.empty()) obl::unit::ReportFailure(__FILE__, __LINE__, "unresolved $ref:" + Join(unresolved));
    for (const auto& [stem, id] : ids) {
        // <kind>.schema.json has $id obl.<kind> and, unless it is the shared definitions, describes files whose "format" is
        // that id and whose "version" is a fixed number.
        OBL_CHECK_EQ(id, "obl." + stem);
        if (stem == "common") continue;
        const json& schema = *registry.Find(id);
        OBL_CHECK_EQ(schema.value("type", ""), std::string("object"));
        OBL_CHECK(schema.value("additionalProperties", true) == false);
        OBL_CHECK(schema["properties"]["format"] == json({{"const", id}}));
        OBL_CHECK(schema["properties"]["version"].contains("const"));
        const json& required = schema["required"];
        OBL_CHECK(required.size() >= 2 && required[0] == "format" && required[1] == "version");
    }
}

OBL_TEST_CASE("schema.samples_validate")
{
    Registry registry;
    std::map<std::string, std::string> ids;
    OBL_REQUIRE(LoadSchemas(registry, &ids));
    std::map<std::string, int> samplesPerFormat;
    for (const auto& path : FilesEndingWith(kSampleDir, ".json")) {
        const std::string name = path.filename().string();
        const json sample = ReadJson(path);
        if (sample.is_discarded() || !sample.is_object() || !sample.contains("format")) {
            obl::unit::ReportFailure(__FILE__, __LINE__, name + ": not a JSON object with \"format\"");
            continue;
        }
        // <kind>.json or <kind>.<variant>.json holds a document of format obl.<kind>.
        const std::string format = sample["format"].get<std::string>();
        OBL_CHECK_EQ(format, "obl." + name.substr(0, name.find('.')));
        ++samplesPerFormat[format];
        const auto errors = registry.Validate(format, sample);
        if (!errors.empty()) obl::unit::ReportFailure(__FILE__, __LINE__, name + ":" + Join(errors));

        // Every schema is strict at the top level: an unrecognized key and a missing version are refused.
        json extra = sample;
        extra["unrecognized_key"] = 1;
        OBL_CHECK(!registry.Validate(format, extra).empty());
        json noVersion = sample;
        noVersion.erase("version");
        OBL_CHECK(!registry.Validate(format, noVersion).empty());
    }
    for (const auto& [stem, id] : ids)
        if (stem != "common" && samplesPerFormat[id] == 0)
            obl::unit::ReportFailure(__FILE__, __LINE__, "no sample in tests/schema_samples/ for " + id);
}

OBL_TEST_CASE("schema.pack_rules")
{
    Registry registry;
    OBL_REQUIRE(LoadSchemas(registry));
    json pack = ReadJson(kSampleDir / "pack.json");
    OBL_REQUIRE(registry.Validate("obl.pack", pack).empty());
    json bad = pack;
    bad["files"]["../escape.json"] = {{"sha256", std::string(64, 'a')}, {"tier", 1}};
    OBL_CHECK(!registry.Validate("obl.pack", bad).empty());
    bad = pack;
    bad["files"]["Sprites/Upper.png"] = {{"sha256", std::string(64, 'a')}, {"tier", 1}};
    OBL_CHECK(!registry.Validate("obl.pack", bad).empty());
    bad = pack;
    bad["files"]["a.json"] = {{"sha256", std::string(63, 'a')}, {"tier", 3}};
    OBL_CHECK_EQ(registry.Validate("obl.pack", bad).size(), size_t{2});
    json converted = pack;
    converted["provenance"] = "converted";
    converted["import"] = {{"reference", std::string(64, '0')}};
    converted["converter"] = "obl-convert 1";
    OBL_CHECK(registry.Validate("obl.pack", converted).empty());
    // The v1 contract is gone: build names, an unshaped reference and an older version are refused.
    json refused = converted;
    refused["source"] = {{"exe", "securom-2002-08"}, {"data", "uk-2002-08"}, {"sha256", json::object()}};
    OBL_CHECK(!registry.Validate("obl.pack", refused).empty());
    refused = converted;
    refused["import"]["reference"] = "drm-free-2002-10";
    OBL_CHECK(!registry.Validate("obl.pack", refused).empty());
    refused = converted;
    refused["import"]["exe"] = "securom-2002-08";
    OBL_CHECK(!registry.Validate("obl.pack", refused).empty());
    refused = converted;
    refused["version"] = 1;
    OBL_CHECK(!registry.Validate("obl.pack", refused).empty());
}

namespace {

// Errors of `sample` (a file of tests/schema_samples/) after `edit`.
template<class Edit> std::vector<std::string> ErrorsAfter(const Registry& registry, const char* sample, Edit edit)
{
    json doc = ReadJson(kSampleDir / sample);
    edit(doc);
    return registry.Validate(doc["format"].get<std::string>(), doc);
}

} // namespace

OBL_TEST_CASE("schema.gui_provenance_rules")
{
    Registry r;
    OBL_REQUIRE(LoadSchemas(r));
    for (const char* sample : {"credits.json", "object_browser.json"}) {
        OBL_CHECK(ErrorsAfter(r, sample, [](json& d) { d["provenance"] = "converted"; }).empty());
        OBL_CHECK(!ErrorsAfter(r, sample, [](json& d) { d["provenance"] = "imported"; }).empty());
        OBL_CHECK(!ErrorsAfter(r, sample, [](json& d) { d["version"] = 1; }).empty());
    }
}

OBL_TEST_CASE("schema.asset_rules")
{
    Registry r;
    OBL_REQUIRE(LoadSchemas(r));
    // Indexed sprites need palettes; true-color sprites must not have palettes or a tone group.
    OBL_CHECK(!ErrorsAfter(r, "sprite.json", [](json& d) { d.erase("palettes"); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sprite.building.json", [](json& d) { d["palettes"] = "x.png"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sprite.building.json", [](json& d) { d["tone_group"] = 1; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sprite.json", [](json& d) { d["animations"][0]["plane"] = "normal"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sprite.json", [](json& d) { d["animations"][0]["size"] = {0, 4}; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sprite.json", [](json& d) { d["animations"][0]["sheets"] = json::array(); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sprite.json", [](json& d) { d["animations"][0]["rows"] = 0; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sprite.building.json", [](json& d) { d["depth_extents"] = {1, 2, 3}; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sprite.building.json", [](json& d) { d["depth_extents"][0] = 256; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sprite.autotile.json", [](json& d) { d["autotile"]["masks"].erase(0); }).empty());
    // Glyph keys are lowercase hex code points; kerning entries are exact triples.
    OBL_CHECK(!ErrorsAfter(r, "font.json", [](json& d) { d["glyphs"]["017C"] = {0, 0, 1, 1}; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "font.json", [](json& d) { d["kerning"][0].push_back(1); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "text.json", [](json& d) { d["language"] = "Polish"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sound_script.json", [](json& d) { d["events"]["Bad Name"] = d["events"]["my_own_event"]; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sound_script.json", [](json& d) { d["groups"]["footsteps"]["limit"] = -1; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sound_script.json", [](json& d) { d["sprite_sounds"]["sample_walker"]["walk"]["group"] = 3; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "sound_script.json", [](json& d) {
        d["sprite_sounds"]["sample_walker"]["walk"]["sounds"] = json::array();
    }).empty());
}

OBL_TEST_CASE("schema.game_data_rules")
{
    Registry r;
    OBL_REQUIRE(LoadSchemas(r));
    OBL_CHECK(!ErrorsAfter(r, "game_parameters.json", [](json& d) { d["parameters"]["nested"] = json::object(); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "game_parameters.json", [](json& d) { d["parameters"]["MaxGuests"] = 1; }).empty());
    // Ids are positions, so a key may appear only once per kind.
    OBL_CHECK(!ErrorsAfter(r, "catalog_index.json", [](json& d) { d["scenery"].push_back("palm"); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "catalog_index.json", [](json& d) { d.erase("fines"); }).empty());
    // Buildings stand on ground types only; scenery may also go on paths.
    OBL_CHECK(!ErrorsAfter(r, "building.json", [](json& d) { d["build_on"].push_back("path"); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "building.json", [](json& d) { d["footprint"]["layers"].erase("work"); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "building.json", [](json& d) { d["anims"][0]["points"].erase(0); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "building.json", [](json& d) { d["commodities"][0].erase("cost"); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "building.json", [](json& d) { d["failure"]["percent"] = 101; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "table.json", [](json& d) { d["chairs"][0]["dir"] = "north"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "beer.json", [](json& d) { d["effect"] = "vandal"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "guest_set.json", [](json& d) { d["types"][0]["preferences"]["swimfun"] = {0, 1}; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "guest_set.json", [](json& d) { d["types"][0]["nights"] = {1, 300}; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "gui_layouts.json", [](json& d) { d["layouts"]["in_game"] = {{"pl", "in_game_pl.gui"}}; }).empty());
}

OBL_TEST_CASE("schema.save_rules")
{
    Registry r;
    OBL_REQUIRE(LoadSchemas(r));
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d.erase("header"); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["header"]["packs"] = json::array(); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["header"]["packs"][0]["pack_hash"] = "abc"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["world"]["profit_history"] = {"f32:7FC00000"}; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["world"]["game_time"] = "12.5"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["world"]["game_time"] = 1e39; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["objects"][0]["type"] = 3; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["objects"][0]["pos"] = {1, 2}; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["objects"][0]["pos"] = {1, 2, 3, 4}; }).empty());
    OBL_CHECK(ErrorsAfter(r, "save.json", [](json& d) { d["objects"][0]["pos"][2] = "f32:80000000"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["objects"][0]["id"] = -1; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["map"]["cell_state"]["terrain"][0][1] = 16; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["map"]["heights"][0] = 65536; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["map"]["areas_open"][0] = 1; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["map"].erase("heights"); }).empty());
    OBL_CHECK(ErrorsAfter(r, "save.json", [](json& d) { d["objects"][0].erase("dir"); }).empty()); // only buildings have a rotation
    OBL_CHECK(!ErrorsAfter(r, "save.json", [](json& d) { d["managers"]["weather"]["raw_bytes"] = "00"; }).empty());
    OBL_CHECK(ErrorsAfter(r, "save.json", [](json& d) {
        d["header"]["converted_from"] = {{"format", "beachlife-1.6"}, {"file", "save3.dat"}};
        d["world"]["game_time"] = "f32:80000000";
        d["map"]["cell_state"]["neighbours"][0][1] = 255;
    }).empty());
}

OBL_TEST_CASE("schema.map_scenario_rules")
{
    Registry r;
    OBL_REQUIRE(LoadSchemas(r));
    OBL_CHECK(!ErrorsAfter(r, "map.json", [](json& d) { d["terrain_types"][0]["type"] = "lava"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "map.json", [](json& d) { d["terrain_types"][0]["index"] = 256; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "map.json", [](json& d) { d.erase("depth"); }).empty());
    OBL_CHECK(ErrorsAfter(r, "map.json", [](json& d) { d.erase("height"); }).empty()); // a flat map has no height image
    OBL_CHECK(!ErrorsAfter(r, "scenario.json", [](json& d) { d["kind"] = "tutorial"; }).empty());
    OBL_CHECK(ErrorsAfter(r, "scenario.json", [](json& d) { d["available_buildings"].push_back("snack_hut"); }).empty());
    OBL_CHECK(
            ErrorsAfter(r, "scenario.json", [](json& d) { d["pictures"]["large"] = {{"sprite", "sample_sheet"}, {"frame", 2}}; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "scenario.json", [](json& d) {
        d["pictures"]["large"] = {{"sprite", "sample_sheet"}, {"frame", -1}};
    }).empty());
    OBL_CHECK(!ErrorsAfter(r, "scenario.json", [](json& d) { d["status_texts"]["mood"] = "SAMPLE"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "scenario.json", [](json& d) { d["script"] = "C:\\scenario\\world.wsc"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "campaign.json", [](json& d) { d["campaign"] = json::array(); }).empty());
    OBL_CHECK(!ErrorsAfter(r, "campaign.json", [](json& d) { d["unlock"] = "random"; }).empty());
    OBL_CHECK(!ErrorsAfter(r, "campaign.json", [](json& d) { d["sandbox_unlock"] = "random"; }).empty());
    OBL_CHECK(ErrorsAfter(r, "campaign.json", [](json& d) {
        d["initially_available"] = {"sample_isle", "sample_reef"};
        d["always_available"] = {"sample_isle"};
        d["sandbox_unlock"] = "campaign_completed_or_always";
    }).empty());
}
