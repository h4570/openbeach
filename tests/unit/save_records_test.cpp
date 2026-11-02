#include "formats/save_records.h"
#include "formats/cell_state.h"

#include "unit_test.h"

using nlohmann::json;
using nlohmann::ordered_json;

namespace {

ordered_json Building()
{
    ordered_json slots = ordered_json::object();
    slots["staff"] = {7, 0, 9, 0, 0, 0, 0, 0, 0, 0};
    slots["staff_busy"] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    slots["commodity_price"] = {1.5, -0.0, 0.0, 0.0, 0.0};
    slots["commodity_cost"] = {0.0, 0.0, 0.0, 0.0, 0.0};
    return slots;
}

} // namespace

OBL_TEST_CASE("save_records.lists_drop_their_unused_tail_and_a_load_fills_it_back")
{
    ordered_json document = {{"objects", ordered_json::array({{{"building", Building()}}})}};
    const json slots = json::parse(document.dump());
    obl::formats::SlotsToRecords(document);
    const ordered_json& staff = document["objects"][0]["building"]["staff"];
    OBL_CHECK_EQ(staff.size(), size_t{3});
    OBL_CHECK(staff[0]["id"] == 7);
    OBL_CHECK(staff[0]["busy"] == 1);
    OBL_CHECK(staff[2]["id"] == 9);
    // -0.0 is a value of its own, so the second commodity stays.
    OBL_CHECK_EQ(document["objects"][0]["building"]["commodities"].size(), size_t{2});

    json back = json::parse(document.dump());
    obl::formats::RecordsToSlots(back);
    OBL_CHECK(back["objects"][0]["building"]["staff"] == slots["objects"][0]["building"]["staff"]);
    OBL_CHECK(back["objects"][0]["building"]["staff_busy"] == slots["objects"][0]["building"]["staff_busy"]);
    OBL_CHECK(back["objects"][0]["building"]["commodity_price"].dump() == slots["objects"][0]["building"]["commodity_price"].dump());
}

OBL_TEST_CASE("save_records.stale_words_in_the_tail_stay")
{
    ordered_json document = {{"objects", ordered_json::array({{{"vehicle", {{"guests", ordered_json::array()}}}}})}};
    ordered_json& guests = document["objects"][0]["vehicle"]["guests"];
    for (int i = 0; i < 300; ++i) guests.push_back(i == 299 ? 2019914784 : 0);
    obl::formats::SlotsToRecords(document);
    OBL_CHECK_EQ(document["objects"][0]["vehicle"]["guests"].size(), size_t{300});
}

OBL_TEST_CASE("save_records.named_records_read_missing_names_as_zero")
{
    ordered_json document = {{"view", {{"position", {1.5, 2.5}}}}};
    obl::formats::SlotsToRecords(document);
    OBL_CHECK(document["view"]["position"]["y"] == 2.5);

    json back = {{"view", {{"position", {{"y", 2.5}}}}}};
    obl::formats::RecordsToSlots(back);
    OBL_CHECK(back["view"]["position"] == json::array({0.0, 2.5}));
    OBL_CHECK(back["view"]["velocity"] == json::array({0.0, 0.0}));
}

OBL_TEST_CASE("save_records.map_cell_words_become_named_runs_and_come_back")
{
    ordered_json words = {0x80000003u, 0x80000003u, 0x4f7ff4ffu, 0xffffffffu, 0u};
    ordered_json document = {{"map", {{"flags", words}}}};
    obl::formats::SlotsToRecords(document);
    const ordered_json& state = document["map"]["cell_state"];
    OBL_CHECK(!document["map"].contains("flags"));
    OBL_CHECK(state["terrain"][0] == ordered_json::array({2, 3}));
    OBL_CHECK(state["free_land"].dump() == "[[2,1],[1,0],[1,1],[1,0]]");
    OBL_CHECK(state["neighbours"].dump() == "[[2,0],[1,127],[1,255],[1,0]]");

    json back = json::parse(document.dump());
    obl::formats::RecordsToSlots(back);
    OBL_CHECK(!back["map"].contains("cell_state"));
    OBL_CHECK(back["map"]["flags"].dump() == words.dump());
}

OBL_TEST_CASE("cell_state.fields_cover_every_bit_once_and_records_keep_the_word")
{
    uint32_t covered = 0;
    for (const obl::formats::CellField& field : obl::formats::CellFields()) {
        OBL_CHECK_EQ(covered & field.mask, uint32_t{0});
        covered |= field.mask;
    }
    OBL_CHECK_EQ(covered, 0xffffffffu);
    OBL_CHECK(obl::formats::CellWordToRecord(0).empty());
    OBL_CHECK(obl::formats::CellWordToRecord(0x80000800u).dump() == R"({"built":1,"free_land":1})");
    for (const uint32_t word : {0u, 0x200u, 0x800000cu, 0x80000007u, 0x40000000u, 0xffffffffu})
        OBL_CHECK_EQ(obl::formats::CellWordFromRecord(json::parse(obl::formats::CellWordToRecord(word).dump())), word);
}

OBL_TEST_CASE("save_records.missing_lists_read_as_unused_slots")
{
    json document = {{"objects", json::array({{{"scenery", json::object()}}})}};
    obl::formats::RecordsToSlots(document);
    OBL_CHECK(document["objects"][0]["scenery"]["attached_guests"] == json::array({-1, -1}));
}
