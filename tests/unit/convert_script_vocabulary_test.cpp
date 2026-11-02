
#include "unit_test.h"

#include "importer/src/source/pe_file.h"
#include "importer/src/readers/script_vocabulary.h"
#include "game/objects/script_program_api.h"
#include "game/objects/script_program_ids.h"
#include "game/world/world_script_api.h"
#include "game/world/world_script_load.h"
#include "synthetic_exe.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

using obl::convert::ExtractScriptVocabulary;
using obl::convert::PeFile;
using obl::convert::ScriptTables;

namespace {

constexpr uint32_t kImageBase = obl::test::SyntheticExe::kImageBase;
constexpr uint32_t kDataVa = kImageBase + 0x1000; // .data, the records and their names

using obl::test::PutU32;

// A minimal PE32 with one data section at kDataVa; no imports, no relocations.
std::vector<uint8_t> MakeExe(const std::vector<uint8_t>& data)
{
    obl::test::SyntheticExe exe;
    exe.AddSection(".data", kDataVa - kImageBase, data, obl::test::SyntheticExe::kDataFlags);
    return exe.bytes;
}

std::vector<uint8_t> MakeTables(size_t states, size_t messages)
{
    return obl::test::ScriptTableData(states, messages, kDataVa);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

template<class Rows> void CheckApiTable(const Rows& rows)
{
    OBL_REQUIRE(!rows.empty());
    std::set<std::string> lowered;
    for (size_t i = 0; i < rows.size(); ++i) {
        OBL_CHECK(!rows[i].name.empty());
        if (i > 0) OBL_CHECK(rows[i - 1].id <= rows[i].id); // sorted by number
        std::string key = rows[i].name;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        OBL_CHECK_EQ(lowered.insert(key).second ? std::string() : key, std::string()); // unique ignoring case: one match per name
    }
}

} // namespace

OBL_TEST_CASE("convert.script_vocabulary_finds_tables")
{
    std::string error;
    auto exe = PeFile::Parse(MakeExe(MakeTables(136, 108)), error);
    OBL_REQUIRE(exe.has_value());
    ScriptTables tables;
    OBL_REQUIRE(ExtractScriptVocabulary(*exe, tables, error));
    OBL_REQUIRE(tables.states.size() == 135); // the empty slot is skipped
    OBL_CHECK(tables.states[3].id == 4);
    OBL_CHECK(tables.states[3].name == "STATE_OWN_4");
    OBL_CHECK(tables.states[134].id == 135);
    OBL_REQUIRE(tables.messages.size() == 108);
    OBL_CHECK(tables.messages[107].id == 107);
    OBL_CHECK(tables.messages[107].name == "MT_OWN_107");
}

OBL_TEST_CASE("convert.script_vocabulary_renumbers_august_states")
{
    // 134 named states (the August CD build): the ids from 64 on move up to the engine's numbering, slot 64 stays empty.
    std::string error;
    auto exe = PeFile::Parse(MakeExe(MakeTables(135, 108)), error);
    OBL_REQUIRE(exe.has_value());
    ScriptTables tables;
    OBL_REQUIRE(ExtractScriptVocabulary(*exe, tables, error));
    OBL_REQUIRE(tables.states.size() == 134);
    OBL_CHECK(tables.states[62].id == 63);
    OBL_CHECK(tables.states[62].name == "STATE_OWN_63");
    OBL_CHECK(tables.states[63].id == 65);
    OBL_CHECK(tables.states[63].name == "STATE_OWN_64");
    OBL_CHECK(tables.states[133].id == 135);
    OBL_CHECK(tables.messages[0].id == 0); // the messages keep their numbers

    // Any other state count is refused.
    exe = PeFile::Parse(MakeExe(MakeTables(100, 108)), error);
    OBL_REQUIRE(exe.has_value());
    OBL_CHECK(!ExtractScriptVocabulary(*exe, tables, error));
    OBL_CHECK(error.find("135 states") != std::string::npos);
}

OBL_TEST_CASE("convert.script_vocabulary_refuses_other_message_counts")
{
    std::string error;
    auto exe = PeFile::Parse(MakeExe(MakeTables(136, 100)), error);
    OBL_REQUIRE(exe.has_value());
    ScriptTables tables;
    OBL_CHECK(!ExtractScriptVocabulary(*exe, tables, error));
    OBL_CHECK(error.find("108 messages") != std::string::npos);
    OBL_CHECK(tables.messages.empty());

    // Too few states: the run does not split into two tables of 64 names.
    exe = PeFile::Parse(MakeExe(MakeTables(20, 108)), error);
    OBL_REQUIRE(exe.has_value());
    OBL_CHECK(!ExtractScriptVocabulary(*exe, tables, error));
}

OBL_TEST_CASE("script_api.tables_are_sorted_and_unique")
{
    // A function may have two names for one number, never one name for two numbers.
    CheckApiTable(obl::script_program::EngineFunctions());
    CheckApiTable(obl::script_program::EngineValues());
    CheckApiTable(obl::world_script_parse::EngineFunctions());
    CheckApiTable(obl::world_script_parse::EngineValues());
    OBL_CHECK_EQ(obl::world_script_parse::EngineFunctions().size(), 9u);
}

OBL_TEST_CASE("data.script_vocabulary_of_installation")
{
    const std::filesystem::path gameDir = OBL_GAME_DIR;
    const auto packObject = gameDir / "packs" / "base" / "scripts" / "object_vocabulary.json";
    const auto packWorld = gameDir / "packs" / "base" / "scripts" / "world_vocabulary.json";
    const auto exePath = gameDir / "BeachLife.exe";
    if (!std::filesystem::exists(packObject) || !std::filesystem::exists(exePath)) OBL_SKIP("run/ has no imported game data");

    std::string error;
    auto exe = PeFile::Parse(ReadBytes(exePath), error);
    OBL_REQUIRE(exe.has_value());
    ScriptTables tables;
    OBL_REQUIRE(ExtractScriptVocabulary(*exe, tables, error));
    OBL_CHECK_EQ(tables.states.size(), 135u);
    OBL_CHECK_EQ(tables.messages.size(), 108u);

    const auto objectPack = ReadBytes(packObject);
    obl::script_program::Vocabulary object;
    OBL_REQUIRE(obl::object::ParseScriptIds(
            std::string_view(reinterpret_cast<const char*>(objectPack.data()), objectPack.size()), object, error));
    OBL_REQUIRE(object.states.size() == tables.states.size());
    OBL_REQUIRE(object.messages.size() == tables.messages.size());
    for (size_t i = 0; i < tables.states.size(); ++i)
        OBL_CHECK((object.states[i].id == tables.states[i].id && object.states[i].name == tables.states[i].name));
    OBL_CHECK_EQ(object.functions.size(), obl::script_program::EngineFunctions().size());
    OBL_CHECK_EQ(object.values.size(), obl::script_program::EngineValues().size());

    const auto worldPack = ReadBytes(packWorld);
    obl::world_script_parse::Vocabulary world;
    OBL_REQUIRE(obl::world::ParseWorldScriptIds(
            std::string_view(reinterpret_cast<const char*>(worldPack.data()), worldPack.size()), world, error));
    OBL_REQUIRE(world.messages.size() == object.messages.size());
    for (size_t i = 0; i < world.messages.size(); ++i)
        OBL_CHECK((world.messages[i].id == object.messages[i].id && world.messages[i].name == object.messages[i].name));
    OBL_CHECK_EQ(world.functions.size(), 9u);
}
