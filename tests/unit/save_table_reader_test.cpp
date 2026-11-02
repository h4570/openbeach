#include "unit_test.h"
#include "game/construction/construction_lifecycle.h"
#include "game/events/event_lifecycle.h"
#include "game/feedback/feedback_lifecycle.h"
#include "game/ferry/ferry_lifecycle.h"
#include "game/messages/message_lifecycle.h"
#include "game/repair/repair_lifecycle.h"
#include "game/world/save_sections_json.h"
#include "game/world/save_sections_load.h"
#include "game/world/save_table_reader.h"
#include "game/world/world_save_json.h"

#include <cstring>
#include <memory>
#include <string>

using namespace obl;

namespace {

// Latin-1 both ways: enough for the readers' code page step.
world_save::JsonContext WriteContext()
{
    world_save::JsonContext context;
    context.narrowToUtf8 = [](std::string_view text) {
        std::u16string wide(text.begin(), text.end());
        for (char16_t& c : wide) c = static_cast<uint8_t>(c);
        return code_page::Utf16ToUtf8(wide);
    };
    return context;
}

world_save::ReadContext ReadContext()
{
    world_save::ReadContext context;
    context.utf8ToNarrow = [](std::string_view text, std::string& out) {
        out.clear();
        for (const char16_t c : code_page::Utf8ToUtf16(text)) {
            if (c > 0xff) return false;
            out.push_back(static_cast<char>(c));
        }
        return true;
    };
    return context;
}

// The construction reader of the game, with a definition that gives every job work points.
class DefiningReader : public world_save::TableReader<DefiningReader> {
public:
    using TableReader::TableReader;
    void Define(construction::Job& job) { job.hasWorkPoints = job.m_buildingId != 0; }
};

} // namespace

OBL_TEST_CASE("save_table_reader.repair_reads_back_the_three_lists")
{
    repair::Manager manager;
    manager.buildingJobs.push_back({.id = 7, .calls = 2, .distance = 1.5f, .timeCalled = 3.0f, .urgent = 1, .type = 2});
    manager.sceneryJobs.push_back({.id = 9, .type = 5});
    manager.activeJobs.push_back({.id = 4, .repairing = 1, .urgent = 0, .type = 0});
    manager.breakdownCount = 3;
    manager.activeCount = -1;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteRepairJson(manager, written, error));

    repair::Manager loaded;
    loaded.sceneryJobs.push_back({.id = 99});
    OBL_REQUIRE(world_save::ReadRepairJson(nlohmann::json(written), loaded, error));
    OBL_REQUIRE(loaded.buildingJobs.size() == size_t(1));
    OBL_CHECK_EQ(loaded.buildingJobs.front().id, 7);
    OBL_CHECK_EQ(loaded.buildingJobs.front().calls, 2u);
    OBL_CHECK(loaded.buildingJobs.front().distance == 1.5f);
    OBL_CHECK_EQ(loaded.buildingJobs.front().urgent, 1);
    OBL_REQUIRE(loaded.sceneryJobs.size() == size_t(1));
    OBL_CHECK_EQ(loaded.sceneryJobs.front().type, 5);
    OBL_REQUIRE(loaded.activeJobs.size() == size_t(1));
    OBL_CHECK_EQ(loaded.activeJobs.front().repairing, 1);
    OBL_CHECK_EQ(loaded.breakdownCount, 3);
    OBL_CHECK_EQ(loaded.activeCount, -1);

    // Missing keys: empty lists and zero counters, as the bridge reads them.
    OBL_REQUIRE(world_save::ReadRepairJson(nlohmann::json::object(), loaded, error));
    OBL_CHECK(loaded.buildingJobs.empty());
    OBL_CHECK_EQ(loaded.activeCount, 0);

    OBL_CHECK(!world_save::ReadRepairJson(nlohmann::json{{"building_jobs", 3}}, loaded, error));
    OBL_CHECK_EQ(error, std::string("RepairManager.building_jobs: not a list"));
    OBL_CHECK(!world_save::ReadRepairJson(nlohmann::json{{"breakdowns", 1.5}}, loaded, error));
    OBL_CHECK_EQ(error, std::string("RepairManager.breakdowns: not an integer"));
}

OBL_TEST_CASE("save_table_reader.messages_read_the_repeated_time_label_in_order")
{
    messages::Handler handler;
    handler.delayed.push_back({.sender = 1, .receiver = 2, .type = 3, .flags = 4, .data = 5, .extra = 7, .floatData = 0.5f, .time = 6.0f});
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteMessagesJson(handler, written, error));
    messages::Handler loaded;
    OBL_REQUIRE(world_save::ReadMessagesJson(nlohmann::json(written), loaded, error));
    OBL_REQUIRE(loaded.delayed.size() == size_t(1));
    OBL_CHECK_EQ(loaded.delayed.front().receiver, 2);
    OBL_CHECK(loaded.delayed.front().time == 6.0f);
    OBL_CHECK_EQ(loaded.delayed.front().extra, 7);
}

OBL_TEST_CASE("save_table_reader.feedback_reads_wide_and_key_texts")
{
    feedback::Manager manager;
    manager.pending.push_back({.objectId = 12, .text = feedback::Text::Key("FB_\xe9T")});
    feedback::Text wide;
    wide.wide = true;
    wide.wideText = {u'Z', u'o', char16_t(0x0142), char16_t(0x0105), u'd'};
    manager.history.push_back({.objectId = -1, .day = 3, .hour = 9.5f, .text = wide});
    manager.suppressedCount = 2;
    manager.suppressed[0] = 40;
    manager.suppressed[99] = 7;
    manager.suppressObject = 1;
    manager.lastReviewHour = 25;
    const auto writeContext = WriteContext();
    const auto readContext = ReadContext();
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteFeedbackJson(manager, writeContext, written, error));

    feedback::Manager loaded;
    OBL_REQUIRE(world_save::ReadFeedbackJson(nlohmann::json(written), readContext, loaded, error));
    OBL_CHECK(loaded.pending == manager.pending);
    OBL_CHECK(loaded.history == manager.history);
    OBL_CHECK(loaded.suppressed == manager.suppressed);
    OBL_CHECK_EQ(loaded.suppressedCount, 2u);
    OBL_CHECK_EQ(loaded.suppressObject, 1);
    OBL_CHECK(loaded.lastReviewHour == 25.0f);

    // A key past the 128-byte buffer and a suppressed list of the wrong length are refused.
    nlohmann::json bad = written;
    bad["pending"][0]["text"] = std::string(128, 'a');
    OBL_CHECK(!world_save::ReadFeedbackJson(bad, readContext, loaded, error));
    OBL_CHECK_EQ(error, std::string("FeedbackManager.text: text of 128 bytes"));
    bad = written;
    bad["suppressed"] = nlohmann::json::array({1, 2});
    OBL_CHECK(!world_save::ReadFeedbackJson(bad, readContext, loaded, error));
    OBL_CHECK_EQ(error, std::string("FeedbackManager.suppressed: not a list of 100"));
}

OBL_TEST_CASE("save_table_reader.ferry_reads_boats_keywords_and_the_cursor")
{
    ferry::Manager manager;
    for (int i = 0; i < 3; ++i) {
        ferry::Boat boat;
        boat.GuestInfo = 2;
        boat.GuestType[1] = 1;
        std::strcpy(boat.GuestKeyword[0], i == 1 ? "SURFER" : "");
        boat.GuestKeywordNumbers[0] = 4 + i;
        boat.Time = 8.0f + static_cast<float>(i);
        boat.Day = i;
        manager.schedule.push_back(boat);
    }
    manager.cursor = std::next(manager.schedule.begin(), 2);
    manager.guests.fAvgHapp[2] = 0.75f;
    manager.guests.iHappyLeavers = 6;
    const auto writeContext = WriteContext();
    const auto readContext = ReadContext();
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteFerryJson(manager, writeContext, written, error));

    ferry::Manager loaded;
    OBL_REQUIRE(world_save::ReadFerryJson(nlohmann::json(written), readContext, loaded, error));
    OBL_REQUIRE(loaded.schedule.size() == size_t(3));
    const ferry::Boat& second = *std::next(loaded.schedule.begin());
    OBL_CHECK_EQ(std::string(second.GuestKeyword[0]), std::string("SURFER"));
    OBL_CHECK_EQ(second.GuestKeywordNumbers[0], 5);
    OBL_CHECK_EQ(second.GuestType[1], 1);
    OBL_REQUIRE(loaded.cursor.has_value());
    OBL_CHECK(*loaded.cursor == std::next(loaded.schedule.begin(), 2));
    OBL_CHECK(loaded.guests.fAvgHapp[2] == 0.75f);
    OBL_CHECK_EQ(loaded.guests.iHappyLeavers, 6u);

    // Without `next_boat` the cursor is past the end (kNoCursor), as the bridge's default.
    nlohmann::json noCursor = written;
    noCursor.erase("next_boat");
    OBL_REQUIRE(world_save::ReadFerryJson(noCursor, readContext, loaded, error));
    OBL_REQUIRE(loaded.cursor.has_value());
    OBL_CHECK(*loaded.cursor == loaded.schedule.end());
}

OBL_TEST_CASE("save_table_reader.events_read_the_running_indices")
{
    disaster::Manager manager;
    manager.nextEvent = 4;
    manager.numEvents = 6;
    manager.active = {1, 3};
    manager.numCurEvents = 2;
    manager.curDayOffset = 14;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteEventsJson(manager, written, error));
    disaster::Manager loaded;
    const nlohmann::json in(written);
    world_save::PlainTableReader reader("EventManager", nullptr, in, error);
    VisitFields(loaded, reader);
    OBL_REQUIRE(reader.Ok());
    OBL_CHECK(loaded == manager);
    OBL_CHECK(world_save::CheckEventsJson(nlohmann::json(written), error));
    OBL_CHECK(!world_save::CheckEventsJson(nlohmann::json{{"current_events", nlohmann::json::array({"x"})}}, error));
    OBL_CHECK_EQ(error, std::string("EventManager.current_events[]: not an integer"));
}

OBL_TEST_CASE("save_table_reader.construction_reads_jobs_work_points_and_builders")
{
    construction::Manager manager;
    auto& job = *manager.jobs.emplace_back(std::make_unique<construction::Job>());
    job.m_buildingId = 31;
    job.hoardings.push_back({.m_direction = 1, .x = 10, .y = 11, .m_savedMetadata = 2});
    job.m_constructionRemaining = 40.0f;
    job.m_constructionProgress = 0.25f;
    job.m_started = 1;
    job.m_finished = 0;
    job.hasWorkPoints = true;
    job.workPoints[3] = {.x = 5, .y = 6, .direction = 2};
    manager.m_constructionCount = 1;
    manager.m_builderCount = 2;
    manager.m_builders[1] = {.m_builderId = 77, .m_buildingId = 31, .m_manuallyAssigned = 1};
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteConstructionJson(manager, written, error));

    construction::Manager loaded;
    const nlohmann::json in(written);
    DefiningReader reader("ConstructionManager", nullptr, in, error);
    construction::LoadFields(loaded, reader);
    OBL_REQUIRE(reader.Ok());
    OBL_REQUIRE(loaded.jobs.size() == size_t(1));
    const construction::Job& read = *loaded.jobs.front();
    OBL_CHECK(read.hoardings == job.hoardings);
    OBL_CHECK(read.m_constructionRemaining == 40.0f);
    OBL_CHECK(read.m_constructionProgress == 0.25f);
    OBL_CHECK_EQ(read.m_started, 1);
    OBL_CHECK(read.workPoints == job.workPoints);
    OBL_CHECK_EQ(loaded.m_builderCount, 2);
    OBL_CHECK(loaded.m_builders[1] == manager.m_builders[1]);

    // The plain reader defines nothing: the work points of a job without them stay zero.
    construction::Manager plain;
    world_save::PlainTableReader plainReader("ConstructionManager", nullptr, in, error);
    construction::LoadFields(plain, plainReader);
    OBL_REQUIRE(plainReader.Ok());
    OBL_CHECK(plain.jobs.front()->workPoints == construction::Job{}.workPoints);

    nlohmann::json tooMany = written;
    tooMany["builders"] = nlohmann::json::array();
    for (int i = 0; i <= construction::kMaxBuilders; ++i) tooMany["builders"].push_back(nlohmann::json::object());
    OBL_CHECK(!world_save::CheckConstructionJson(tooMany, error));
    OBL_CHECK_EQ(error, std::string("ConstructionManager.builders: 51 over 50"));
}
