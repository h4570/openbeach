#include "importer/src/readers/save/save_body.h"
#include "old_save_builder.h"
#include "unit_test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
using obl::convert::OldSaveBody;
using obl::convert::SaveRecord;
using obl::convert::SaveStream;
using obl::convert::SaveTag;
using obl::test::Save;

std::vector<size_t> RecordOffsets(const std::vector<uint8_t>& data)
{
    std::vector<size_t> offsets;
    SaveStream stream(data);
    SaveRecord record;
    std::string error;
    while (stream.Next(record, error)) offsets.push_back(record.offset);
    return offsets;
}
} // namespace

OBL_TEST_CASE("convert.save_body_reads_every_section_and_object")
{
    const Save save = Save::Make(2);
    OldSaveBody body;
    std::string error;
    OBL_REQUIRE(obl::convert::ReadOldSaveBody(save.data, body, error));
    OBL_CHECK(error.empty());
    OBL_REQUIRE(body.sections.size() == 14u);
    OBL_CHECK_EQ(body.sections[0].kind, "World");
    OBL_CHECK_EQ(body.sections[0].fields.size(), 74u);
    OBL_CHECK_EQ(body.sections[6].kind, "EventManager"); // the unmarked tails are blocks of their own
    OBL_CHECK_EQ(body.sections[13].kind, "Weather");
    OBL_CHECK_EQ(body.sections[13].fields.size(), 29u);
    OBL_REQUIRE(body.objects.size() == 2u);
    OBL_CHECK_EQ(body.objects[1].kind, "Scenery");
    OBL_CHECK_EQ(body.objects[1].type, 1);
    OBL_REQUIRE(body.objects[1].Find("m_dwID"));
    OBL_CHECK_EQ(body.objects[1].Find("m_dwID")->record.Bits(), 2u);
    OBL_REQUIRE(body.objects[1].Find("sprite.m_bTriggeredSound"));
    OBL_CHECK_EQ(body.objects[1].fields.back().name, "m_SelectLevel");
    OBL_CHECK(obl::convert::CheckOldSaveBody(body).empty());

    std::string report;
    OBL_REQUIRE(obl::convert::DescribeOldSaveObjects(save.data, report, error));
    OBL_CHECK(report.find("objects   2: 0 buildings, 0 guests, 2 others\n") != std::string::npos);
    OBL_CHECK(report.find("  Scenery    ") != std::string::npos);
    OBL_CHECK(report.find("checks    ok\n") != std::string::npos);
}

OBL_TEST_CASE("convert.save_body_rejects_truncated_extra_and_mistyped_records")
{
    const Save save = Save::Make();
    OldSaveBody body;
    std::string error;
    // Cut at every record boundary after the header: a missing record or section is an error, never a partial body.
    for (size_t offset : RecordOffsets(save.data)) {
        if (offset < 50) continue;
        const std::vector<uint8_t> cut(save.data.begin(), save.data.begin() + static_cast<std::ptrdiff_t>(offset));
        OBL_CHECK(!obl::convert::ReadOldSaveBody(cut, body, error));
        OBL_CHECK(!error.empty());
        OBL_CHECK(body.sections.empty());
    }
    Save extra = save;
    extra.Codes("i");
    OBL_CHECK(!obl::convert::ReadOldSaveBody(extra.data, body, error));
    OBL_CHECK(error.find("records after the weather section") != std::string::npos);

    // The weather time (the third weather record) as an int: the error names the block and the field.
    const auto offsets = RecordOffsets(save.data);
    Save typed = save;
    typed.data[offsets[offsets.size() - 27]] = static_cast<uint8_t>(SaveTag::Int);
    OBL_CHECK(!obl::convert::ReadOldSaveBody(typed.data, body, error));
    OBL_CHECK(error.find("Weather.m_WeatherTime: expected float, found int") != std::string::npos);
}

OBL_TEST_CASE("convert.save_body_bounds_counts_by_the_remaining_input")
{
    Save save = Save::Make(0);
    const auto offsets = RecordOffsets(save.data);
    // The ferry's "List size" is the second record after its class record.
    size_t ferry = 0;
    SaveStream stream(save.data);
    SaveRecord record;
    std::string error;
    while (stream.Next(record, error))
        if (record.tag == SaveTag::Class && record.ClassName() == "FerryManager") ferry = record.offset;
    OBL_REQUIRE(ferry);
    const size_t list = *std::upper_bound(offsets.begin(), offsets.end(), *std::upper_bound(offsets.begin(), offsets.end(), ferry));
    for (int i = 0; i < 4; ++i) save.data[list + 1 + i] = 0xff;
    OldSaveBody body;
    OBL_CHECK(!obl::convert::ReadOldSaveBody(save.data, body, error));
    OBL_CHECK(error.find("FerryManager.List size: count 4294967295 exceeds the remaining records") != std::string::npos);
}

OBL_TEST_CASE("convert.save_body_checks_counts_ids_and_references")
{
    OldSaveBody body;
    std::string error;
    Save save = Save::Make(2, {2, 7});
    OBL_REQUIRE(obl::convert::ReadOldSaveBody(save.data, body, error));
    // A repair job may name a removed object (it outlives the object for a while), so id 7 is no problem.
    auto problems = obl::convert::CheckOldSaveBody(body);
    OBL_CHECK(problems.empty());

    // Two objects with one id, and an active count that disagrees with the objects in the file.
    Save twice;
    twice.Header();
    twice.World();
    twice.Class("WorldMap").Codes("ui");
    twice.Class("Path").Codes("u");
    twice.Class("ObjectManager").Codes("u").Int(3).Int(0).Int(0).Int(2).Ints(18500).Int(2).Codes("b");
    twice.Scenery(1);
    twice.Scenery(1);
    const Save tail = Save::Make(0);
    constexpr std::string_view kPlayer = "Class name : Player";
    const auto player = std::search(tail.data.begin(), tail.data.end(), kPlayer.begin(), kPlayer.end());
    OBL_REQUIRE(player != tail.data.end());
    twice.data.insert(twice.data.end(), player - 5, tail.data.end());
    OBL_REQUIRE(obl::convert::ReadOldSaveBody(twice.data, body, error));
    problems = obl::convert::CheckOldSaveBody(body);
    OBL_REQUIRE(problems.size() == 2u);
    OBL_CHECK_EQ(problems[0], "ObjectManager: m_dwNumActiveObjects 3 but 2 objects");
    OBL_CHECK_EQ(problems[1], "duplicate object id 1");
}

OBL_TEST_CASE("data.convert_interprets_local_save_bodies")
{
    const std::filesystem::path dir = std::filesystem::path(OBL_GAME_DIR) / "SaveGames";
    if (!std::filesystem::is_directory(dir)) OBL_SKIP("no local saves");
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".dat") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        OldSaveBody body;
        std::string error;
        const bool ok = obl::convert::ReadOldSaveBody(bytes, body, error);
        if (!ok) obl::unit::ReportFailure(__FILE__, __LINE__, entry.path().string() + ": " + error);
        OBL_REQUIRE(ok);
        for (const std::string& problem : obl::convert::CheckOldSaveBody(body))
            obl::unit::ReportFailure(__FILE__, __LINE__, entry.path().string() + ": " + problem);
        // Completeness: every record of the file is a typed field, a class record or one of the 11 header records.
        size_t typed = 0, classes = 0;
        for (const auto& section : body.sections) {
            typed += section.fields.size();
            classes += section.kind != "EventManager" && section.kind != "Weather";
        }
        for (const auto& object : body.objects) typed += object.fields.size();
        OBL_CHECK_EQ(RecordOffsets(bytes).size(), typed + classes + 11);
        ++count;
    }
    if (!count) OBL_SKIP("no local .dat saves");
}

OBL_TEST_CASE("data.convert_interprets_performer_save")
{
    // Written by tests/replays/s1_performers.record.txt (saves.md): the five performer classes that no other local save holds.
    const std::filesystem::path path = std::filesystem::path(OBL_GAME_DIR) / "SaveGames" / "s1_performers.dat";
    if (!std::filesystem::is_regular_file(path)) OBL_SKIP("no generated performer save");
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    OldSaveBody body;
    std::string error;
    const bool ok = obl::convert::ReadOldSaveBody(bytes, body, error);
    if (!ok) obl::unit::ReportFailure(__FILE__, __LINE__, path.string() + ": " + error);
    OBL_REQUIRE(ok);
    for (const std::string& problem : obl::convert::CheckOldSaveBody(body))
        obl::unit::ReportFailure(__FILE__, __LINE__, path.string() + ": " + problem);
    for (const char* kind : {"Cabaret", "Catwalk", "WeightLifter", "WetTShirt", "Aerobic"}) {
        const bool found = std::any_of(body.objects.begin(), body.objects.end(), [&](const auto& object) { return object.kind == kind; });
        if (!found) obl::unit::ReportFailure(__FILE__, __LINE__, std::string("no ") + kind + " object in " + path.string());
    }
}
