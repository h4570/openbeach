// Command recorder (src/game/game/command_recorder.h): order rules, the replay file text, and the cursor a replay reads it with
// (docs/architecture/commands.md, "Replay file").

#include "unit_test.h"

#include "game/game/command_recorder.h"

using namespace obl::command;

namespace {

ReplayHeader Header()
{
    ReplayHeader header;
    header.startKind = StartKind::Scenario;
    header.start = "s3";
    header.seed = 1;
    header.fixedDt = 0.02f;
    header.hashSchema = 2;
    return header;
}

Recording Sample()
{
    Recorder recorder(Header());
    OBL_REQUIRE(recorder.AddCheckpoint(0, 0x0123456789abcdefull));
    OBL_REQUIRE(recorder.Record(0, SetGameSpeed{2}));
    OBL_REQUIRE(recorder.Record(120, PlaceBuilding{"BeachBar", 40, 52, 2}));
    OBL_REQUIRE(recorder.Record(120, SetPrice{17, 1, 0.75f}));
    OBL_REQUIRE(recorder.AddCheckpoint(312, 0xfedcba9876543210ull));
    OBL_REQUIRE(recorder.Record(312, SetPaused{true}));
    OBL_REQUIRE(recorder.Finish(1000));
    return recorder.Result();
}

const char* const kSampleText = "OBL-replay 1\n"
                                "start scenario s3\n"
                                "seed 1\n"
                                "fixed-dt 0.02\n"
                                "hash-schema 2\n"
                                "@0 hash 0123456789abcdef\n"
                                "@0 set-game-speed level=2\n"
                                "@120 place-building key=BeachBar x=40 y=52 dir=2\n"
                                "@120 set-price building=17 slot=1 price=0.75\n"
                                "@312 hash fedcba9876543210\n"
                                "@312 set-paused paused=1\n"
                                "end 1000\n";

std::string ReadError(const std::string& text)
{
    std::string error;
    OBL_CHECK(!ReadReplay(text, &error).has_value());
    return error;
}

const std::string kHeaderText = "OBL-replay 1\nstart scenario s3\nseed 1\nfixed-dt 0.02\nhash-schema 2\n";

} // namespace

OBL_TEST_CASE("command_recorder.write_matches_the_documented_format")
{
    OBL_CHECK_EQ(WriteReplay(Sample()), std::string(kSampleText));
}

OBL_TEST_CASE("command_recorder.read_round_trips")
{
    std::string error;
    const auto read = ReadReplay(kSampleText, &error);
    OBL_REQUIRE(read.has_value());
    OBL_CHECK(error.empty());
    OBL_CHECK(*read == Sample());
    OBL_CHECK_EQ(WriteReplay(*read), std::string(kSampleText));
}

OBL_TEST_CASE("command_recorder.read_accepts_comments_blank_lines_and_crlf")
{
    const std::string text = "# reference replay\r\nOBL-replay 1\r\n\r\nstart save run/save/slot%201.sav\r\nseed 7\r\nfixed-dt 0.02\r\n"
                             "hash-schema 2\r\n  # body\r\n@5 demolish object=3\r\nend 6\r\n";
    const auto read = ReadReplay(text);
    OBL_REQUIRE(read.has_value());
    OBL_CHECK(read->header.startKind == StartKind::Save);
    OBL_CHECK_EQ(read->header.start, std::string("run/save/slot 1.sav"));
    OBL_CHECK_EQ(read->header.seed, 7u);
    OBL_REQUIRE(read->commands.size() == 1);
    OBL_CHECK((read->commands[0] == TimedCommand{5, Demolish{3}}));
    OBL_CHECK_EQ(read->endTick, 6u);
}

OBL_TEST_CASE("command_recorder.recorder_refuses_out_of_order_entries")
{
    Recorder recorder(Header());
    OBL_CHECK(recorder.Record(10, SetPaused{true}));
    OBL_CHECK(recorder.Record(10, SetPaused{false})); // same tick: submission order
    OBL_CHECK(!recorder.Record(9, SetPaused{true}));
    OBL_CHECK(!recorder.AddCheckpoint(10, 1)); // a checkpoint is the state before its tick's commands
    OBL_CHECK(recorder.AddCheckpoint(11, 1));
    OBL_CHECK(!recorder.AddCheckpoint(11, 2));
    OBL_CHECK(!recorder.Record(10, SetPaused{true}));
    OBL_CHECK(recorder.Record(11, SetPaused{true}));
    OBL_CHECK(!recorder.Finish(11)); // tick 11 would never run
    OBL_CHECK(!recorder.Finished());
    OBL_CHECK(recorder.Finish(12));
    OBL_CHECK(recorder.Finished());
    OBL_CHECK(!recorder.Record(12, SetPaused{true}));
    OBL_CHECK(!recorder.AddCheckpoint(12, 3));
    OBL_CHECK(!recorder.Finish(13));
    const Recording& result = recorder.Result();
    OBL_CHECK_EQ(result.commands.size(), 3u);
    OBL_CHECK_EQ(result.checkpoints.size(), 1u);
    OBL_CHECK_EQ(result.endTick, 12u);
}

OBL_TEST_CASE("command_recorder.final_checkpoint_may_sit_on_the_end_tick")
{
    Recorder recorder(Header());
    OBL_CHECK(recorder.AddCheckpoint(0, 1));
    OBL_CHECK(recorder.AddCheckpoint(100, 2)); // the state after the last tick
    OBL_CHECK(recorder.Finish(100));
    const auto read = ReadReplay(WriteReplay(recorder.Result()));
    OBL_REQUIRE(read.has_value());
    OBL_CHECK(*read == recorder.Result());
}

OBL_TEST_CASE("command_recorder.read_reports_the_line_of_an_error")
{
    OBL_CHECK_EQ(ReadError(""), std::string("line 1: missing 'end' (truncated replay)"));
    OBL_CHECK_EQ(ReadError("OBL-replay 2\n"), std::string("line 1: unsupported replay format version 2"));
    OBL_CHECK_EQ(ReadError("OBL-world-v3\n"), std::string("line 1: expected 'OBL-replay <version>'"));
    OBL_CHECK_EQ(ReadError("OBL-replay 1\nstart map s3\n"), std::string("line 2: no such start kind 'map'"));
    OBL_CHECK_EQ(ReadError("OBL-replay 1\nstart scenario\n"), std::string("line 2: bad start name"));
    OBL_CHECK_EQ(ReadError("OBL-replay 1\nstart scenario s3\nfixed-dt 0.02\n"), std::string("line 3: expected 'seed <n>'"));
    OBL_CHECK_EQ(ReadError("OBL-replay 1\nstart scenario s3\nseed 1\nfixed-dt 0\n"),
            std::string("line 4: expected 'fixed-dt <seconds>' above 0"));
    OBL_CHECK_EQ(ReadError(kHeaderText + "@1 demolish object=1\n"), std::string("line 7: missing 'end' (truncated replay)"));
    OBL_CHECK_EQ(ReadError(kHeaderText + "1 demolish object=1\n"), std::string("line 6: expected '@<tick> ...' or 'end'"));
    OBL_CHECK_EQ(ReadError(kHeaderText + "@1 hash 123\n"),
            std::string("line 6: expected '@<tick> hash <16 hex digits> [<section>=<digest> ...]'"));
    OBL_CHECK_EQ(ReadError(kHeaderText + "@1 demolish obj=1\n"), std::string("line 6: demolish: missing field object"));
    OBL_CHECK_EQ(
            ReadError(kHeaderText + "@5 demolish object=1\n@4 demolish object=1\n"), std::string("line 7: command at tick 4 out of order"));
    OBL_CHECK_EQ(ReadError(kHeaderText + "@5 demolish object=1\n@5 hash 0000000000000000\n"),
            std::string("line 7: checkpoint at tick 5 out of order"));
    OBL_CHECK_EQ(
            ReadError(kHeaderText + "@5 demolish object=1\nend 5\n"), std::string("line 7: end tick 5 does not follow the last entry"));
    OBL_CHECK_EQ(ReadError(kHeaderText + "end 5\n@6 demolish object=1\n"), std::string("line 7: text after 'end'"));
}

OBL_TEST_CASE("command_recorder.cursor_hands_out_each_tick_once")
{
    const Recording recording = Sample();
    ReplayCursor cursor(recording);
    const auto atZero = cursor.Due(0);
    OBL_REQUIRE(atZero.size() == 1);
    OBL_CHECK(atZero[0] == Command{SetGameSpeed{2}});
    OBL_CHECK(cursor.Due(0).empty());
    OBL_CHECK(cursor.Due(119).empty());
    const auto at120 = cursor.Due(120);
    OBL_REQUIRE(at120.size() == 2);
    OBL_CHECK((at120[0] == Command{PlaceBuilding{"BeachBar", 40, 52, 2}}));
    OBL_CHECK((at120[1] == Command{SetPrice{17, 1, 0.75f}}));
    // A loop that skipped ticks still gets the late commands, in order.
    const auto late = cursor.Due(5000);
    OBL_REQUIRE(late.size() == 1);
    OBL_CHECK(late[0] == Command{SetPaused{true}});

    OBL_CHECK(cursor.ExpectedHash(0) == std::optional<uint64_t>{0x0123456789abcdefull});
    OBL_CHECK(cursor.ExpectedHash(312) == std::optional<uint64_t>{0xfedcba9876543210ull});
    OBL_CHECK(!cursor.ExpectedHash(1).has_value());
    OBL_CHECK(!cursor.ExpectedHash(999).has_value());
    OBL_CHECK(!cursor.Done(999));
    OBL_CHECK(cursor.Done(1000));
}

OBL_TEST_CASE("command_recorder.checkpoint_section_digests_round_trip")
{
    obl::SectionDigests sections{};
    sections[static_cast<size_t>(obl::WorldSection::Guests)] = {0x0000abcd, 7};
    sections[static_cast<size_t>(obl::WorldSection::Rng)] = {0x12345678, 2};
    Recorder recorder(Header());
    OBL_REQUIRE(recorder.AddCheckpoint(0, 0x0123456789abcdefull, sections));
    OBL_REQUIRE(recorder.AddCheckpoint(5, 0xfedcba9876543210ull));
    OBL_REQUIRE(recorder.Finish(5));
    const std::string text = WriteReplay(recorder.Result());
    OBL_CHECK(text.find("@0 hash 0123456789abcdef guests=0000abcd/7 rng=12345678/2\n@5 hash fedcba9876543210\n") != std::string::npos);
    const auto read = ReadReplay(text);
    OBL_REQUIRE(read.has_value());
    OBL_CHECK(*read == recorder.Result());
    ReplayCursor cursor(*read);
    OBL_REQUIRE(cursor.ExpectedCheckpoint(0) != nullptr);
    OBL_CHECK(cursor.ExpectedCheckpoint(0)->sections == std::optional<obl::SectionDigests>{sections});
    OBL_REQUIRE(cursor.ExpectedCheckpoint(5) != nullptr);
    OBL_CHECK(!cursor.ExpectedCheckpoint(5)->sections.has_value());
    OBL_CHECK(cursor.ExpectedCheckpoint(3) == nullptr);
    OBL_CHECK_EQ(ReadError(kHeaderText + "@1 hash 0000000000000000 tourists=0000abcd/7\n"),
            std::string("line 6: unrecognized section 'tourists'"));
}

OBL_TEST_CASE("command_recorder.start_scenario_names")
{
    OBL_CHECK((ParseStartScenario("s1") == StartScenario{false, 1}));
    OBL_CHECK((ParseStartScenario("s12") == StartScenario{false, 12}));
    OBL_CHECK((ParseStartScenario("sandbox3") == StartScenario{true, 3}));
    OBL_CHECK((ParseStartScenario("1") == StartScenario{false, 1}));
    OBL_CHECK((ParseStartScenario("12") == StartScenario{false, 12}));
    for (const char* bad :
            {"", "s", "s0", "s01", "s-1", "s+1", "s1x", "sandbox", "sandbox0", "scenario1", "x1", "S1", "0", "01", "1x", "-1", "+1"})
        OBL_CHECK(!ParseStartScenario(bad).has_value());
}
