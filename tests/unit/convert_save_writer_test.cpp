#include "importer/src/writers/catalog_writer.h"
#include "importer/src/writers/pack_manifest.h"
#include "core/sha256.h"
#include "importer/src/source/source.h"
#include "importer/src/writers/text_convert.h"
#include "importer/src/readers/save/save_body.h"
#include "importer/src/readers/save/save_writer.h"
#include "formats/schemas.h"
#include "old_save_builder.h"
#include "unit_test.h"

#include "core/json_schema.h"
#include "temp_dir.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;
using obl::convert::OldSaveBody;
using obl::convert::SaveFloat;
using obl::test::Save;

bool LoadRepoSchemas(obl::json_schema::Registry& registry)
{
    std::string error;
    const bool ok = obl::formats::LoadSchemaDirectory(fs::path(OBL_REPO_DIR) / "data" / "schemas", registry, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    return ok;
}

obl::convert::SaveOrigin Origin()
{
    obl::convert::SaveOrigin origin;
    origin.file = "save1.dat";
    origin.packHash = std::string(64, '0');
    return origin;
}

// `type` overrides the object type bytes after reading, so a scenery-only synthetic save stands for another kind.
bool Document(const Save& save, const json& index, ordered_json& document, std::string& error, uint8_t type = 0)
{
    OldSaveBody body;
    if (!obl::convert::ReadOldSaveBody(save.data, body, error)) return false;
    static uint8_t typeByte;
    typeByte = type;
    if (type)
        for (auto& object : body.objects) {
            object.type = type;
            for (auto& f : object.fields)
                if (f.name == "m_bType") f.record.bytes = std::span<const uint8_t>(&typeByte, 1); // Entity repeats the type
        }
    return obl::convert::OldSaveDocument(body, index, Origin(), document, error);
}
} // namespace

OBL_TEST_CASE("convert.save_float_keeps_bits")
{
    OBL_CHECK(SaveFloat(0x80000000u).dump() == "-0.0");
    OBL_CHECK(SaveFloat(0x7fc00001u) == "f32:7fc00001");
    OBL_CHECK(SaveFloat(0xff800000u) == "f32:ff800000");
    OBL_CHECK(SaveFloat(0x00000001u).dump() == "1e-45"); // the smallest subnormal reads back to the same bits
    OBL_CHECK(SaveFloat(0x41480000u).dump() == "12.5");
}

OBL_TEST_CASE("convert.save_section_labels_are_mapped_or_dropped")
{
    using obl::convert::OldSectionLabelKnown;
    OBL_CHECK(OldSectionLabelKnown("World", "m_fGameTime"));
    OBL_CHECK(OldSectionLabelKnown("World", "m_BoatList"));
    OBL_CHECK(OldSectionLabelKnown("World", "Worldscript state"));
    OBL_CHECK(OldSectionLabelKnown("World", "m_iObjectiveStatus[3]"));
    OBL_CHECK(!OldSectionLabelKnown("World", "m_newField"));
    OBL_CHECK(!OldSectionLabelKnown("World", "m_fGameTime[1]"));
    OBL_CHECK(OldSectionLabelKnown("ObjectManager", "m_dwNextFreeObjectID"));
    OBL_CHECK(OldSectionLabelKnown("Player", "Name"));
    OBL_CHECK(OldSectionLabelKnown("ZEngine", "burnqueue entry[12]"));
    OBL_CHECK(OldSectionLabelKnown("RepairManager", "BuildingJob[12].Type"));
    OBL_CHECK(OldSectionLabelKnown("CleaningManager", "ActiveJob list size"));
    OBL_CHECK(!OldSectionLabelKnown("CleaningManager", "Job[1].Type"));
    OBL_CHECK(OldSectionLabelKnown("EventManager", "EventID[4]"));
    OBL_CHECK(OldSectionLabelKnown("Weather", "m_rStrikeArea.left"));
    OBL_CHECK(OldSectionLabelKnown("ConstructionManager", "Job[2].Hoarding[11].member@0xc"));
    OBL_CHECK(OldSectionLabelKnown("FerryManager", "NowBoat[0].GuestKeyword[19]"));
    OBL_CHECK(!OldSectionLabelKnown("FeedbackManager", "Pending[0].day"));
}

OBL_TEST_CASE("convert.save_document_maps_header_world_grid_and_objects")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    const json index = {{"buildings", json::array()}};
    ordered_json d;
    std::string error;
    const bool ok = Document(Save::Make(2, {}, 4), index, d, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_REQUIRE(ok);

    OBL_CHECK(d.begin().key() == "format");
    OBL_CHECK(std::next(d.begin(), 2).key() == "header");
    OBL_CHECK(d["header"]["scenario"] == "s1");
    OBL_CHECK(d["header"]["title"] == "x"); // a text key: the slot title of the builder
    OBL_CHECK(d["header"]["converted_from"]["file"] == "save1.dat");
    OBL_CHECK(d["world"]["game_time"] == 0);
    OBL_CHECK(d["world"]["objective_status"].size() == 4);
    OBL_CHECK(d["world"]["profit_history"].size() == 128);
    OBL_CHECK(d["world"]["available_buildings"].empty());
    OBL_CHECK(!d["world"].contains("m_BoatList"));
    OBL_CHECK(d["player"]["name"] == "x");
    OBL_CHECK(d["managers"]["objects"]["next_id"] == 3);
    OBL_CHECK(d["view"]["camera"] == ordered_json::array({0, 0}));
    OBL_CHECK(d["view"]["move_destination"] == ordered_json::array({0, 0}));
    OBL_CHECK(d["view"]["burn_queue"].empty());
    OBL_CHECK(d["map"]["cells"] == ordered_json::array({2, 2}));
    OBL_CHECK(d["map"]["heights"].size() == 4);
    OBL_CHECK(d["map"]["areas_open"] == ordered_json::array({false}));
    OBL_REQUIRE(d["objects"].size() == 2);
    OBL_CHECK(d["objects"][1]["id"] == 2);
    OBL_CHECK(d["objects"][1]["kind"] == "scenery");
    OBL_CHECK(d["objects"][1]["type"] == "x"); // the builder names every object "x"
    OBL_CHECK(!d["objects"][1].contains("dir"));
    const bool valid = obl::formats::ValidateDocument(schemas, "obl.save", d, error);
    if (!valid) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(valid);
}

OBL_TEST_CASE("convert.save_document_maps_manager_job_lists")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    ordered_json d;
    std::string error;
    OBL_REQUIRE(Document(Save::Make(7, {2, 7}, 4), json{{"buildings", json::array()}}, d, error));
    const ordered_json& repair = d["managers"]["repair"];
    OBL_REQUIRE(repair["building_jobs"].size() == 2);
    OBL_CHECK(repair["building_jobs"][0]["object"] == 2);
    OBL_CHECK(repair["building_jobs"][1]["object"] == 7);
    OBL_CHECK(repair["building_jobs"][1]["urgent"] == false);
    OBL_CHECK(repair["scenery_jobs"].empty());
    OBL_CHECK(repair["active_jobs"].empty());
    OBL_CHECK(d["managers"]["cleaning"]["jobs"].empty());
    OBL_CHECK(d["managers"]["events"]["current_events"].empty());
    const ordered_json& weather = d["managers"]["weather"];
    OBL_CHECK(weather["desire_modifiers"].size() == 26);
    OBL_CHECK(weather["strike_area"]["right"] == 0);
    OBL_CHECK(weather["strike_point"].size() == 3);
    OBL_CHECK(weather["strike_building"] == 0);
    const bool valid = obl::formats::ValidateDocument(schemas, "obl.save", d, error);
    if (!valid) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(valid);
}

OBL_TEST_CASE("convert.save_document_keys_names_outside_the_catalog")
{
    ordered_json d;
    std::string error;
    OBL_REQUIRE(Document(Save::Make(1, {}, 4, "Palm 4s"), json::object(), d, error)); // a map prop
    OBL_CHECK(d["objects"][0]["type"] == "palm_4s");
    const uint8_t vehicle = 4; // catalog kinds refuse a name that is not a key
    OBL_CHECK(!Document(Save::Make(1, {}, 4, "Palm 4s"), json{{"vehicles", json::array({"palm_4s"})}}, d, error, vehicle));
    OBL_CHECK(error.find("not a content key") != std::string::npos);
}

OBL_TEST_CASE("convert.save_document_refuses_a_field_written_twice")
{
    const Save save = Save::Make(1, {}, 4);
    OldSaveBody body;
    std::string error;
    OBL_REQUIRE(obl::convert::ReadOldSaveBody(save.data, body, error));
    auto& world = body.sections.front();
    OBL_REQUIRE(world.kind == "World");
    world.fields.push_back(*world.Find("m_fGameTime")); // a label the emitter repeats
    ordered_json d;
    OBL_CHECK(!obl::convert::OldSaveDocument(body, json::object(), Origin(), d, error));
    OBL_CHECK(error.find("World.m_fGameTime: written twice") != std::string::npos);
}

OBL_TEST_CASE("convert.save_document_maps_ferry_feedback_message_and_construction_managers")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    ordered_json d;
    std::string error;
    OBL_REQUIRE(Document(Save::Make(1, {}, 4, "x", true), json::object(), d, error));
    const ordered_json& m = d["managers"];
    const ordered_json& boats = m["ferry"]["boats"];
    OBL_REQUIRE(boats.size() == 1);
    OBL_CHECK(boats[0]["guests_by_type"].size() >= 2);
    OBL_CHECK(boats[0]["guests_by_type"][0]["keyword"] == "Party Animal"); // engine names stay verbatim
    OBL_CHECK(boats[0]["guests_by_type"][1]["keyword"] == "x");
    OBL_CHECK(m["ferry"]["next_boat"] == 0);
    OBL_CHECK(m["ferry"]["performances"].empty());
    OBL_CHECK(m["feedback"]["pending"][0]["object"] == 4);
    OBL_CHECK(m["feedback"]["pending"][0]["wide"] == false);
    OBL_CHECK(m["feedback"]["history"][0]["object"] == 5);
    OBL_CHECK(m["feedback"]["history"][0]["text"] == "hi");
    OBL_CHECK(m["messages"]["delayed"].size() == 1);
    // The second list level and the repeated labels (m_fConstDone, m_BuildingReady) of a construction job.
    const ordered_json& job = m["construction"]["jobs"][0];
    OBL_CHECK(job["building"] == 1);
    OBL_REQUIRE(job["hoardings"].size() == 2);
    OBL_CHECK(job["hoardings"][1]["x"] == 7);
    OBL_CHECK(job["hoardings"][1]["y"] == 9);
    OBL_CHECK(job["work_points"].size() == 10);
    OBL_CHECK(job["started"] == true);
    OBL_CHECK(job["finished"] == false);
    OBL_CHECK(job["under_construction"] == true);
    OBL_CHECK(job.contains("construction_progress"));
    OBL_REQUIRE(m["construction"]["builders"].size() == 1);
    OBL_CHECK(m["construction"]["builders"][0]["building"] == 1);
    const bool valid = obl::formats::ValidateDocument(schemas, "obl.save", d, error);
    if (!valid) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(valid);
}

OBL_TEST_CASE("convert.save_document_maps_scenery_and_shared_object_state")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    ordered_json d;
    std::string error;
    OBL_REQUIRE(Document(Save::Make(1, {}, 4, "Palm 4s"), json::object(), d, error));
    const ordered_json& o = d["objects"][0];
    OBL_CHECK(o["type"] == "palm_4s");
    OBL_CHECK(o["created_as"] == "bench"); // the builder's creation name BENCH differs from m_Name
    OBL_CHECK(!o.contains("name")); // spellings are not saved
    OBL_CHECK(o["scenery"]["attached_guests"].size() == 2);
    OBL_CHECK(o["scenery"].contains("reburn_rect"));
    OBL_CHECK(o["static"]["urgent_repairing"] == false); // the second m_calledRepairing
    OBL_CHECK(o["base"]["sprite"] == "x");
    OBL_CHECK(o["sprite"].contains("dir")); // a non-building's facing
    OBL_CHECK(!o.contains("moving"));
    const bool valid = obl::formats::ValidateDocument(schemas, "obl.save", d, error);
    if (!valid) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(valid);
}

OBL_TEST_CASE("convert.save_document_maps_staff_and_shark_state")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    ordered_json d;
    std::string error;
    const json index{{"staff", json::array({"mechanic", "lifeguard"})}};
    const bool ok = Document(Save::Make(1, {}, 4, "x", false, {8, 7, 13}), index, d, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_REQUIRE(ok);
    const ordered_json& mechanic = d["objects"][1];
    OBL_CHECK(mechanic["kind"] == "staff");
    OBL_CHECK(mechanic["mechanic"].contains("watering_pos"));
    OBL_CHECK(mechanic["staff"]["thoughts"].size() <= 3);
    OBL_CHECK(mechanic["moving"].contains("velocity"));
    OBL_CHECK(!mechanic["moving"].contains("path"));
    const ordered_json& lifeguard = d["objects"][2];
    OBL_CHECK(lifeguard["lifeguard"]["attached"]["sprite"] == "Tower Flag"); // a sprite group inside a class group
    OBL_CHECK(lifeguard["lifeguard"]["attached"].contains("frame"));
    OBL_CHECK(lifeguard.contains("staff"));
    const ordered_json& shark = d["objects"][3];
    OBL_CHECK(shark["kind"] == "animal");
    OBL_CHECK(shark["shark"]["swim_rect"].size() == 4);
    const bool valid = obl::formats::ValidateDocument(schemas, "obl.save", d, error);
    if (!valid) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(valid);
}

OBL_TEST_CASE("convert.save_document_refuses_an_unrecognized_class_field")
{
    const Save save = Save::Make(1, {}, 4, "x", false, {13});
    OldSaveBody body;
    std::string error;
    OBL_REQUIRE(obl::convert::ReadOldSaveBody(save.data, body, error));
    OBL_REQUIRE(body.objects.size() == 2);
    auto& shark = body.objects.back();
    shark.fields[std::size_t{shark.sharedFrom} - 1].name = "m_Unsupported"; // the last class field
    ordered_json d;
    OBL_CHECK(!obl::convert::OldSaveDocument(body, json::object(), Origin(), d, error));
    OBL_CHECK(error.find("m_Unsupported") != std::string::npos);
}

OBL_TEST_CASE("convert.save_document_maps_performer_state")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    ordered_json d;
    std::string error;
    const bool ok = Document(Save::Make(1, {}, 4, "x", false, {14, 15, 16, 17, 18, 20}), json::object(), d, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_REQUIRE(ok);
    OBL_CHECK(!d["objects"][1]["performer"].contains("action_point")); // the cabaret has none
    OBL_CHECK(d["objects"][2]["performer"]["attached"]["sprite"] == "Mic");
    OBL_CHECK(d["objects"][4]["performer"].contains("finished")); // labelled m_finished
    OBL_CHECK(d["objects"][5]["performer"].contains("contestant"));
    OBL_CHECK(!d["objects"][6]["performer"].contains("attached"));
    const bool valid = obl::formats::ValidateDocument(schemas, "obl.save", d, error);
    if (!valid) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(valid);
}

OBL_TEST_CASE("convert.save_document_maps_vehicle_and_table_state")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    ordered_json d;
    std::string error;
    const json index{{"vehicles", json::array({"boat"})}, {"tables", json::array({"cafetable1"})}};
    const bool ok = Document(Save::Make(0, {}, 4, "x", false, {4, 5}), index, d, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_REQUIRE(ok);
    const ordered_json& vehicle = d["objects"][0]["vehicle"];
    OBL_CHECK(vehicle["guests"].size() <= 300);
    OBL_CHECK(vehicle["way_points"].size() == 64);
    OBL_REQUIRE(vehicle["sprites"].size() == 1); // a sprite list without a size record
    OBL_CHECK(vehicle["sprites"][0].contains("frame"));
    OBL_CHECK(d["objects"][0].contains("moving"));
    const ordered_json& table = d["objects"][1]["table"];
    OBL_REQUIRE(table["chairs"].size() == 1);
    OBL_CHECK(table["chairs"][0]["sprite"].contains("dir")); // a sprite group inside a list element
    OBL_REQUIRE(table["plates"].size() == 1);
    OBL_CHECK(table["plates"][0].contains("draw"));
    OBL_CHECK(d["objects"][1].contains("static"));
    const bool valid = obl::formats::ValidateDocument(schemas, "obl.save", d, error);
    if (!valid) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(valid);
}

OBL_TEST_CASE("convert.save_document_maps_guest_state")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    ordered_json d;
    std::string error;
    const bool ok = Document(Save::Make(1, {}, 4, "x", false, {3}), json{{"buildings", json::array({"hut"})}}, d, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_REQUIRE(ok);
    const ordered_json& guest = d["objects"][0]; // the guest group comes before the others
    OBL_CHECK(guest["kind"] == "guest");
    OBL_CHECK(guest["guest"]["desires"].size() == 26);
    OBL_CHECK(guest["guest"]["walk_points"].size() == 3);
    OBL_CHECK(guest["guest"]["current_type"] == "hut"); // a catalog building position as its key
    OBL_CHECK(guest["guest"]["best_type"].is_null());
    OBL_CHECK(guest["guest"]["name"] == "x");
    OBL_CHECK(!guest["guest"].contains("attached"));
    const bool valid = obl::formats::ValidateDocument(schemas, "obl.save", d, error);
    if (!valid) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(valid);
    // A building type outside the catalog fails.
    OBL_CHECK(!Document(Save::Make(1, {}, 4, "x", false, {3}), json::object(), d, error));
    OBL_CHECK(error.find("m_CurTypeID: no catalog buildings entry 0") != std::string::npos);
}

OBL_TEST_CASE("convert.save_document_maps_building_state")
{
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    ordered_json d;
    std::string error;
    const json index{{"buildings", json::array({"hut"})}, {"events", json::array({"singing"})}};
    const bool ok = Document(Save::Make(0, {}, 4, "x", false, {2}), index, d, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_REQUIRE(ok);
    const ordered_json& b = d["objects"][0]["building"];
    OBL_CHECK(b["beer"].is_null());
    OBL_CHECK(b["profit_history"].size() <= 64);
    OBL_CHECK(b["sprites"].size() == 1);
    OBL_REQUIRE(b["events"]["slots"].size() == 3);
    OBL_CHECK(b["events"]["slots"][2]["event"] == "singing"); // a catalog event position as its key
    OBL_CHECK(b["events"]["slots"][2]["actors"] == json::array({Save::kBuildingIds})); // a list in a list element
    OBL_CHECK(b["events"]["slots"][2]["guests"].empty());
    OBL_REQUIRE(b["thoughts"].size() == 1);
    OBL_CHECK(b["thoughts"][0]["text"] == "hi");
    OBL_CHECK(!b["thoughts"][0].contains("text_length"));
    OBL_CHECK(b["queue"]["guests"].size() == 1);
    OBL_CHECK(d["objects"][0]["dir"] == "n");
    OBL_CHECK(!d["objects"][0]["sprite"].contains("dir")); // the rotation is the object's dir
    const bool valid = obl::formats::ValidateDocument(schemas, "obl.save", d, error);
    if (!valid) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_CHECK(valid);
}

OBL_TEST_CASE("convert.save_place_nests_objects_and_list_levels")
{
    using obl::convert::PlaceSaveValue;
    ordered_json out;
    const uint32_t first[] = {0}, second[] = {1}, nested[] = {0, 1}, none[] = {0, 0};
    // A dotted prefix before a list: path.steps[].x lands at out.path.steps[i].x.
    OBL_CHECK(PlaceSaveValue(out, "path.steps[].x", first, 1));
    OBL_CHECK(PlaceSaveValue(out, "path.steps[].x", second, 2));
    OBL_CHECK(out["path"]["steps"][1]["x"] == 2);
    OBL_CHECK(out["path"].size() == 1);
    OBL_CHECK(!PlaceSaveValue(out, "path.steps[].x", second, 3)); // written twice
    // Two list levels: the second index picks the inner element.
    OBL_CHECK(PlaceSaveValue(out, "jobs[].hoardings[].x", none, 5));
    OBL_CHECK(PlaceSaveValue(out, "jobs[].hoardings[].x", nested, 6));
    OBL_CHECK(out["jobs"][0]["hoardings"][1]["x"] == 6);
    OBL_CHECK(!PlaceSaveValue(out, "jobs[].hoardings[].x", second, 7)); // no index for the inner list
    OBL_CHECK(PlaceSaveValue(out, "ids[]", first, 9));
    OBL_CHECK(!PlaceSaveValue(out, "ids[]", first, 9));
    const uint32_t gap[] = {2};
    OBL_CHECK(!PlaceSaveValue(out, "ids[]", gap, 9)); // past the end
}

OBL_TEST_CASE("convert.save_document_refuses_more_than_50_builders")
{
    const Save save = Save::Make(1, {}, 4);
    static constexpr uint8_t kCount[] = {51, 0, 0, 0};
    OldSaveBody body;
    std::string error;
    OBL_REQUIRE(obl::convert::ReadOldSaveBody(save.data, body, error));
    for (auto& section : body.sections)
        if (section.kind == "ConstructionManager")
            for (auto& f : section.fields)
                if (f.name == "m_NumberOfBuilders") f.record.bytes = kCount;
    ordered_json d;
    OBL_CHECK(!obl::convert::OldSaveDocument(body, json::object(), Origin(), d, error));
    OBL_CHECK(error.find("m_NumberOfBuilders: 51 over 50") != std::string::npos);
}

OBL_TEST_CASE("convert.save_document_refuses_unrecognized_keys_and_grids")
{
    ordered_json d;
    std::string error;
    Save wrongIds = Save::Make(2, {}, 4);
    const uint8_t idList[] = {static_cast<uint8_t>(obl::convert::SaveTag::IntArray), 0x44, 0x48, 0, 0}; // 18500 entries
    const auto list = std::search(wrongIds.data.begin(), wrongIds.data.end(), std::begin(idList), std::end(idList));
    OBL_REQUIRE(list != wrongIds.data.end());
    list[5 + 4 * 2001] = 9; // the second other object's slot
    OBL_CHECK(!Document(wrongIds, json::object(), d, error));
    OBL_CHECK(error.find("m_dwObjectIDList[2001]") != std::string::npos);
    const uint8_t vehicle = 4;
    OBL_CHECK(Document(Save::Make(0, {}, 4, "x", false, {vehicle}), json{{"vehicles", json::array({"x", "boat"})}}, d, error));
    OBL_CHECK(d["objects"][0]["kind"] == "vehicle");
    OBL_CHECK(!Document(Save::Make(1, {}, 4), json{{"vehicles", json::array({"boat"})}}, d, error, vehicle));
    OBL_CHECK(error.find("x is not in catalog vehicles") != std::string::npos);
    const uint8_t building = 2; // a building needs its rotation from the sprite
    OBL_CHECK(Document(Save::Make(0, {}, 4, "x", false, {building}),
            json{{"buildings", json::array({"hut"})}, {"events", json::array({"x"})}}, d, error));
    OBL_CHECK(d["objects"][0]["dir"] == "n"); // the builder's sprite direction is 0
    OBL_CHECK(!Document(Save::Make(1, {}, 2), json::object(), d, error));
    OBL_CHECK(error.find("not a square grid") != std::string::npos);
}

OBL_TEST_CASE("convert.save_header_takes_pack_language_and_title_text")
{
    std::string error;
    ordered_json d;
    const Save save = Save::Make(1, {}, 4);
    auto convert = [&](const std::string& title, const obl::convert::SaveOrigin& origin) {
        OldSaveBody body;
        OBL_REQUIRE(obl::convert::ReadOldSaveBody(save.data, body, error));
        const std::string text = title + '\0'; // the record holds the terminator
        body.header.title.bytes = std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        const bool ok = obl::convert::OldSaveDocument(body, json::object(), origin, d, error);
        if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
        OBL_REQUIRE(ok);
    };
    obl::convert::SaveOrigin origin = Origin();
    origin.texts = {{"S1_TITLE", "Wyspa \xc4\x85"}};
    convert("S1_TITLE", origin);
    OBL_CHECK(d["header"]["title"] == "S1_TITLE");
    OBL_CHECK(d["header"]["title_text"] == "Wyspa \xc4\x85");
    convert("S2_TITLE", origin); // a key the table lacks keeps the stored text
    OBL_CHECK(d["header"]["title_text"] == "S2_TITLE");
    const std::string narrow = "\xb9";
    convert(narrow, origin); // no key: the stored text in the pack's code page
    OBL_CHECK(!d["header"].contains("title"));
    OBL_CHECK(d["header"]["title_text"] == "\xc2\xb9"); // Windows-1252
    origin.language = "pl";
    convert(narrow, origin);
    OBL_CHECK(d["header"]["title_text"] == "\xc4\x85"); // Windows-1250
}

OBL_TEST_CASE("convert.save_pack_origin_from_pack_json")
{
    const obl::test::TempPath tmp("obl_convert_save_pack_origin");
    const auto& pack = tmp.path;
    fs::create_directories(pack / "text");
    obl::convert::SaveOrigin origin;
    std::string error;
    OBL_CHECK(!obl::formats::ReadSavePackOrigin(pack, origin, error));
    OBL_CHECK(error.find("obl-convert pack") != std::string::npos);
    std::ofstream(pack / "pack.json", std::ios::binary) << R"({"id":"beachlife-2002","content_version":"1.0.0","language":"pl"})";
    OBL_CHECK(!obl::formats::ReadSavePackOrigin(pack, origin, error)); // no text/pl.json
    OBL_CHECK(error.find("pl.json") != std::string::npos);
    std::ofstream(pack / "text" / "pl.json", std::ios::binary)
            << R"({"format":"obl.text","version":1,"language":"pl","entries":[["A","first"],["A","second"]]})";
    OBL_REQUIRE(obl::formats::ReadSavePackOrigin(pack, origin, error));
    OBL_CHECK(origin.packId == "beachlife-2002");
    OBL_CHECK(origin.contentVersion == "1.0.0");
    OBL_CHECK(origin.language == "pl");
    OBL_CHECK(origin.packHash == *obl::sha256::OfFile(pack / "pack.json"));
    OBL_CHECK(origin.texts.at("A") == "first"); // lookup returns the first value
}

OBL_TEST_CASE("convert.save_pack_origin_reads_mod_text_but_keeps_pack_identity")
{
    const obl::test::TempPath game("save-pack-layers");
    const auto pack = game.path / "packs" / "base";
    const auto mod = game.path / "mod";
    fs::create_directories(pack / "text");
    fs::create_directories(mod / "text");
    std::ofstream(pack / "pack.json") << R"({"id":"base","content_version":"1","language":"en"})";
    std::ofstream(mod / "pack.json") << R"({"id":"mod","content_version":"2","language":"pl"})";
    std::ofstream(pack / "text/en.json") << R"({"entries":[["TITLE","base"]]})";
    std::ofstream(mod / "text/en.json") << R"({"entries":[["TITLE","mod"]]})";
    obl::convert::SaveOrigin origin;
    std::string error;
    OBL_REQUIRE(obl::formats::ReadSavePackOrigin(pack, origin, error));
    OBL_CHECK(origin.packId == "base");
    OBL_CHECK(origin.packHash == *obl::sha256::OfFile(pack / "pack.json"));
    OBL_CHECK(origin.texts.at("TITLE") == "mod");
    fs::remove(mod / "text/en.json");
    OBL_REQUIRE(obl::formats::ReadSavePackOrigin(pack, origin, error));
    OBL_CHECK(origin.texts.at("TITLE") == "base");

    fs::create_directories(pack / "catalog");
    fs::create_directories(mod / "catalog");
    std::ofstream(pack / "catalog/index.json") << "invalid";
    std::ofstream(mod / "catalog/index.json") << "{}";
    const auto save = Save::Make(0, {}, 4);
    ordered_json document;
    OBL_REQUIRE(obl::convert::OldSaveBytesDocument(save.data, pack, origin, document, error));
    fs::remove(mod / "catalog/index.json");
    OBL_CHECK(!obl::convert::OldSaveBytesDocument(save.data, pack, origin, document, error));
    OBL_CHECK(error.find("index.json") != std::string::npos);
}

OBL_TEST_CASE("data.convert_writes_local_saves_as_obl_save")
{
    const fs::path game(OBL_GAME_DIR);
    const fs::path saves = game / "SaveGames";
    if (!fs::is_directory(saves) || !fs::exists(game / "Data" / "Script" / "BuildingList.ble"))
        OBL_SKIP("no local saves or catalog source");
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadRepoSchemas(schemas));
    const obl::test::TempPath outTmp("obl_convert_save_data");
    // Outside the pack: pack.json hashes it, so it cannot sit inside the pack's directory.
    const obl::test::TempPath savesTmp("obl_convert_save_data_saves");
    const auto& out = outTmp.path;
    const auto& savesOut = savesTmp.path;
    int written = 0;
    std::string error;
    obl::convert::Source source;
    // The installation holds several text tables; the local saves are from the Polish game (BeachLife.ini Language=5).
    const std::optional<std::string> language =
            fs::exists(game / "Data" / "text_polish.ble") ? std::optional<std::string>("pl") : std::nullopt;
    const bool ok = obl::convert::ConvertCatalog(
                            *obl::convert::OpenDirectorySource(game), *obl::convert::OpenDirectorySink(out), schemas, written, error) &&
            obl::convert::ConvertTexts(
                    *obl::convert::OpenDirectorySource(game), *obl::convert::OpenDirectorySink(out), schemas, written, error) &&
            obl::convert::IdentifySource(*obl::convert::OpenDirectorySource(game), source, error) &&
            obl::convert::WritePackManifest(*obl::convert::OpenDirectorySink(out), source, language, schemas, written, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    OBL_REQUIRE(ok);
    obl::convert::SaveOrigin origin;
    OBL_REQUIRE(obl::formats::ReadSavePackOrigin(out, origin, error));
    int converted = 0;
    for (const auto& entry : fs::directory_iterator(saves)) {
        if (entry.path().extension() != ".dat") continue;
        const fs::path file = savesOut / (entry.path().stem().string() + ".json");
        const bool done = obl::convert::ConvertOldSave(entry.path(), out, file, schemas, error);
        if (!done) std::fprintf(stderr, "%s\n", error.c_str());
        OBL_CHECK(done);
        if (!fs::exists(file)) continue;
        ++converted;
        std::ifstream in(file, std::ios::binary);
        const json d = json::parse(in);
        size_t buildings = 0;
        for (const auto& o : d["objects"])
            if (o["kind"] == "building") buildings += o.contains("dir") ? 1 : 0;
        OBL_CHECK(buildings > 0);
        size_t cells = 0;
        for (const auto& run : d["map"]["cell_state"]["terrain"]) cells += run[0].get<size_t>();
        OBL_CHECK(cells == d["map"]["heights"].size());
        OBL_CHECK(d["header"]["packs"][0]["pack_hash"] == *obl::sha256::OfFile(out / "pack.json"));
        if (d["header"].contains("title")) {
            const std::string title = d["header"]["title"];
            const auto text = origin.texts.find(title);
            OBL_CHECK(d["header"]["title_text"] == (text == origin.texts.end() ? title : text->second));
        }
    }
    if (!converted) OBL_SKIP("no local .dat saves");
}
