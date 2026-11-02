#include "unit_test.h"
#include "game/ui/save_slots.h"
#include "game/ui/save_slot_header.h"
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct Slot {
    int32_t m_number;
    uint32_t m_time;
};
} // namespace
OBL_TEST_CASE("save_slots.unsigned_keys_and_first_gap")
{
    Slot a{2, 1}, b{0, 0xffffffff}, c{0, 1}, d{-1, 0};
    std::array slots{&a, &b, &c, &d};
    obl::save_slots::SortNumber(slots.begin(), slots.end());
    OBL_CHECK((slots == std::array{&b, &c, &a, &d}));
    OBL_CHECK_EQ(obl::save_slots::FirstFree(slots.begin(), slots.end()), 1);
    OBL_CHECK((slots == std::array{&b, &c, &a, &d}));
    obl::save_slots::SortTime(slots.begin(), slots.begin());
    OBL_CHECK_EQ(obl::save_slots::FirstFree(slots.begin(), slots.begin()), 0);
}
OBL_TEST_CASE("save_slots.stlport_tie_order_above_insertion_threshold")
{
    std::array<Slot, 17> values{};
    std::vector<Slot*> slots;
    for (auto& value : values) slots.push_back(&value);
    obl::save_slots::SortTime(slots.begin(), slots.end());
    constexpr int order[] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
    for (size_t i = 0; i < slots.size(); ++i) OBL_CHECK(slots[i] == &values[order[i]]);
    slots.clear();
    for (auto& value : values) slots.push_back(&value);
    obl::save_slots::SortNumber(slots.begin(), slots.end());
    for (size_t i = 0; i < slots.size(); ++i) OBL_CHECK(slots[i] == &values[order[i]]);
}

OBL_TEST_CASE("save_slots.header_filename_path_and_selection")
{
    OBL_CHECK(obl::save_slots::AcceptHeader("BeachLife Save", 1.6f));
    OBL_CHECK(obl::save_slots::AcceptHeader("BeachLife Save", std::nanf("")));
    OBL_CHECK(!obl::save_slots::AcceptHeader("BeachLife Save", 1.5f));
    OBL_CHECK(!obl::save_slots::AcceptHeader("BeachLife save", 1.6f));
    OBL_CHECK_EQ(obl::save_slots::SlotNumber("save12.dat"), 12);
    OBL_CHECK_EQ(obl::save_slots::SlotNumber("savex.dat"), 0);
    OBL_CHECK_EQ(obl::save_slots::SlotPath(3), std::string("savegames\\save3.dat"));
    OBL_CHECK_EQ(obl::save_slots::SandboxTitleKey(0), std::string("MAINSS_SANDBOX1_TITLE"));
    int rows[] = {2};
    OBL_CHECK_EQ(obl::save_slots::SelectedRow(rows, rows), 0);
    OBL_CHECK_EQ(obl::save_slots::SelectedRow(rows, rows + 1), 2);
    OBL_CHECK(obl::save_slots::ValidRow(2, 2));
    OBL_CHECK(!obl::save_slots::ValidRow(3, 2));
    OBL_CHECK(!obl::save_slots::ValidRow(-1, 2));
}

OBL_TEST_CASE("save_slots.display_minutes_floor_at_quarters")
{
    for (float boundary : {0.25f, 0.5f, 0.75f}) {
        OBL_CHECK_EQ(obl::save_slots::DisplayMinute(12 + boundary), int(boundary * 60));
        OBL_CHECK_EQ(obl::save_slots::DisplayMinute(std::nextafter(12 + boundary, 0.0f)), int(boundary * 60) - 15);
    }
    OBL_CHECK_EQ(obl::save_slots::DisplayMinute(23.99f), 45);
    OBL_CHECK_EQ(obl::save_slots::DisplayMinute(-0.25f), 0);
}
OBL_TEST_CASE("save_slots.obl_header_reads_the_summary_and_stops_after_the_header")
{
    // The sections after the header are not valid JSON: the reader must stop before them.
    std::istringstream in(R"({"format": "obl.save", "version": 2, "header": {"saved_at": 4294967295, "scenario": "s12",
        "sandbox": false, "title": "MAINSS_S12_TITLE", "title_text": "Happy", "day": 3, "day_time": 7.75, "cash": "f32:7f800000",
        "guests": 12, "packs": [{"id": "beachlife-2002", "content_version": "1.0.0"}], "converted_from": {"file": "x"}}, "world": {)");
    obl::save_slots::Slot slot{};
    OBL_REQUIRE(obl::save_slots::ReadOblHeader(in, slot));
    OBL_CHECK_EQ(slot.m_time, 0xffffffffu);
    OBL_CHECK_EQ(slot.m_scenario, 11);
    OBL_CHECK_EQ(slot.m_day, 3);
    OBL_CHECK_EQ(slot.m_hour, 7.75f);
    OBL_CHECK(std::isinf(slot.m_cash));
    OBL_CHECK_EQ(slot.m_guests, 12);
    OBL_CHECK_EQ(std::string(slot.m_title), std::string("MAINSS_S12_TITLE"));
}
OBL_TEST_CASE("save_slots.obl_header_title_text_and_sandbox_number")
{
    std::istringstream in(R"({"format": "obl.save", "version": 2, "header": {"saved_at": 5, "scenario": "sandbox2", "sandbox": true,
        "title_text": "Plain", "day": 0, "day_time": 0, "cash": -1.5, "guests": 0, "packs": [], "converted_from": null}})");
    obl::save_slots::Slot slot{};
    OBL_REQUIRE(obl::save_slots::ReadOblHeader(in, slot));
    OBL_CHECK_EQ(slot.m_scenario, 1);
    OBL_CHECK_EQ(slot.m_cash, -1.5f);
    OBL_CHECK_EQ(std::string(slot.m_title), std::string("Plain"));
}
OBL_TEST_CASE("save_slots.obl_header_rejects_other_documents")
{
    const char* bad[] = {
            R"({"format": "obl.catalog", "version": 1, "header": {"saved_at": 1, "scenario": "s1", "title": "T", "day": 0,
                "day_time": 0, "cash": 0, "guests": 0}})",
            R"({"format": "obl.save", "version": 1, "header": {"saved_at": 1, "scenario": "s1", "title": "T", "day": 0,
                "day_time": 0, "cash": 0, "guests": 0}})",
            R"({"format": "obl.save", "version": 2, "header": {"saved_at": 1, "scenario": "s1", "title": "T", "day": 0})",
            R"({"format": "obl.save", "version": 2, "header": {"saved_at": 1, "scenario": "s1", "title": "T", "day": 0,
                "day_time": 0, "guests": 0}})",
            "BeachLife Save",
    };
    for (const char* text : bad) {
        std::istringstream in(text);
        obl::save_slots::Slot slot{};
        OBL_CHECK(!obl::save_slots::ReadOblHeader(in, slot));
    }
}
