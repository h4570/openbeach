// The importer's byte-source and pack-sink API (importer/include/openbeach/importer/byte_source.h, pack_sink.h): the
// in-memory source and sink of the browser adapter, the desktop directory ones and the case-insensitive lookup.

#include "unit_test.h"

#include "importer/src/writers/game_parameters_convert.h"
#include "formats/schemas.h"
#include "openbeach/importer/byte_source.h"
#include "openbeach/importer/pack_sink.h"
#include "temp_dir.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

std::vector<uint8_t> Bytes(std::string_view s)
{
    return {s.begin(), s.end()};
}

std::vector<std::string> Names(const std::vector<SourceEntry>& entries)
{
    std::vector<std::string> names;
    for (const SourceEntry& entry : entries) names.push_back(entry.name + (entry.directory ? "/" : ""));
    return names;
}

} // namespace

OBL_TEST_CASE("convert.memory_source_lists_reads_and_finds")
{
    MemorySource source;
    source.Add("BeachLife.exe", Bytes("exe"));
    source.Add("Data/Sprites/BLSprite.hdr", Bytes("0123456789"));
    source.Add("Data/text_english.ble", Bytes("t"));
    source.Add("Data/Sprites-old.txt", Bytes("x"));

    const auto root = source.List("");
    OBL_CHECK(root.has_value());
    OBL_CHECK((Names(*root) == std::vector<std::string>{"BeachLife.exe", "Data/"}));
    const auto data = source.List("Data");
    OBL_CHECK(data.has_value());
    OBL_CHECK((Names(*data) == std::vector<std::string>{"Sprites-old.txt", "Sprites/", "text_english.ble"}));
    OBL_CHECK(!source.List("Missing").has_value());
    OBL_CHECK(!source.List("BeachLife.exe").has_value());

    OBL_CHECK(source.Size("Data/Sprites/BLSprite.hdr") == std::optional<uint64_t>(10));
    OBL_CHECK(!source.Size("Data/Sprites").has_value());
    std::array<uint8_t, 3> part{};
    OBL_CHECK(source.Read("Data/Sprites/BLSprite.hdr", 7, part));
    OBL_CHECK(part == (std::array<uint8_t, 3>{'7', '8', '9'}));
    OBL_CHECK(!source.Read("Data/Sprites/BLSprite.hdr", 8, part));
    OBL_CHECK(source.ReadAll("BeachLife.exe") == std::optional<std::vector<uint8_t>>(Bytes("exe")));
    OBL_CHECK(!source.ReadAll("beachlife.exe").has_value());

    OBL_CHECK(FindFile(source, "beachlife.EXE") == std::optional<std::string>("BeachLife.exe"));
    OBL_CHECK(FindFile(source, "data/sprites/blsprite.hdr") == std::optional<std::string>("Data/Sprites/BLSprite.hdr"));
    OBL_CHECK(!FindFile(source, "Data/Sprites/BLSprite.dat").has_value());
    OBL_CHECK(!FindFile(source, "BeachLife.exe/x").has_value());
}

OBL_TEST_CASE("convert.find_file_prefers_exact_case")
{
    MemorySource source;
    source.Add("DATA/a.txt", Bytes("upper"));
    source.Add("Data/a.txt", Bytes("mixed"));
    OBL_CHECK(FindFile(source, "Data/A.TXT") == std::optional<std::string>("Data/a.txt"));
    OBL_CHECK(FindFile(source, "DATA/a.txt") == std::optional<std::string>("DATA/a.txt"));
}

OBL_TEST_CASE("convert.directory_source_reads_files")
{
    const obl::test::TempPath dir("obl-byte-source-test");
    std::filesystem::create_directories(dir.path / "Data");
    std::ofstream(dir.path / "Data" / "File.bin", std::ios::binary) << "abcdef";

    const auto source = OpenDirectorySource(dir.path);
    const auto found = FindFile(*source, "data/file.BIN");
    OBL_CHECK(found == std::optional<std::string>("Data/File.bin"));
    OBL_CHECK(source->Size(*found) == std::optional<uint64_t>(6));
    OBL_CHECK(source->ReadAll(*found) == std::optional<std::vector<uint8_t>>(Bytes("abcdef")));
    std::array<uint8_t, 2> part{};
    OBL_CHECK(source->Read(*found, 4, part));
    OBL_CHECK(part == (std::array<uint8_t, 2>{'e', 'f'}));
    OBL_CHECK(!source->Read(*found, 5, part));
    OBL_CHECK(!source->Size("Data").has_value());
    const auto root = source->List("");
    OBL_CHECK((root.has_value() && Names(*root) == std::vector<std::string>{"Data/"}));
}

namespace {

// The sink contract both adapters share: write replaces, read returns what was written, files lists every file once,
// remove tolerates a missing file.
void ExpectSinkBasics(PackSink& sink)
{
    std::string error;
    OBL_CHECK(sink.WriteText("a/b/c.json", "first", error));
    OBL_CHECK(sink.WriteText("a/b/c.json", "second", error));
    OBL_CHECK(sink.Write("top.bin", Bytes("xy"), error));
    OBL_CHECK(sink.Read("a/b/c.json") == std::optional<std::vector<uint8_t>>(Bytes("second")));
    OBL_CHECK(!sink.Read("a/b").has_value());
    OBL_CHECK(!sink.Read("missing.json").has_value());
    auto files = sink.Files();
    std::sort(files.begin(), files.end());
    OBL_CHECK((files == std::vector<std::string>{"a/b/c.json", "top.bin"}));
    OBL_CHECK(sink.Remove("top.bin"));
    OBL_CHECK(sink.Remove("top.bin"));
    OBL_CHECK(!sink.Read("top.bin").has_value());
    OBL_CHECK((sink.Files() == std::vector<std::string>{"a/b/c.json"}));
}

} // namespace

OBL_TEST_CASE("convert.memory_sink_writes_reads_and_removes")
{
    MemorySink sink;
    ExpectSinkBasics(sink);
    OBL_CHECK(sink.files().size() == 1);
    OBL_CHECK(sink.Describe("a/b/c.json") == "a/b/c.json");
}

OBL_TEST_CASE("convert.directory_sink_writes_reads_and_removes")
{
    const obl::test::TempPath dir("obl-pack-sink-test");
    const auto sink = OpenDirectorySink(dir.path);
    ExpectSinkBasics(*sink);
    OBL_CHECK(std::filesystem::is_regular_file(dir.path / "a" / "b" / "c.json"));
    OBL_CHECK(sink->Describe("a/b/c.json") == (dir.path / "a/b/c.json").string());
}

OBL_TEST_CASE("convert.converter_writes_into_memory_sink")
{
    // Synthetic input and output both in memory: the path the browser adapter takes, without a file system.
    MemorySource source;
    const std::string text = "'iDAY_LENGTH_IN_SECONDS' 1200\r'sDEBUGFONT_NAME' \"Small Fonts\"\r";
    source.Add("Data/Script/GameParameters.pis", Bytes(text));
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    MemorySink sink;
    OBL_REQUIRE(ConvertGameParameters(source, sink, schemas, error));
    OBL_CHECK((sink.Files() == std::vector<std::string>{"rules/game_parameters.json", "rules/weather_parameters.json"}));
    const auto json = sink.Read("rules/game_parameters.json");
    OBL_REQUIRE(json.has_value());
    const auto document = nlohmann::ordered_json::parse(json->begin(), json->end());
    OBL_CHECK(document["format"] == "obl.game_parameters");
    OBL_CHECK(document["parameters"].contains("day_length_in_seconds"));
    OBL_CHECK(json->back() == '\n');
}
