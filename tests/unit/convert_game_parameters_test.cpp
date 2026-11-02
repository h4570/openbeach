#include "unit_test.h"
#include "game/game/game_parameters_construct.h"
#include "game/game/game_parameters.h"
#include "core/float_to_int.h"
#include "importer/src/readers/pis.h"
#include "importer/src/writers/game_parameters_convert.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "temp_dir.h"

#include <bit>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace obl::convert;
using nlohmann::ordered_json;

namespace {

// The engine's entry array for `entries`: the RawEntry records the parameter-file loader leaves in game memory, with the pointers the engine
// reads (key and string NUL-terminated, the float behind `number`). Built once, never copied: the pointers point into the vectors.
struct EngineEntries {
    std::vector<std::string> keys;
    std::vector<std::string> strings;
    std::vector<float> numbers;
    std::vector<obl::pis::RawEntry> raw;

    explicit EngineEntries(const std::vector<obl::pis::Entry>& entries)
        : keys(entries.size()), strings(entries.size()), numbers(entries.size()), raw(entries.size())
    {
        for (size_t i = 0; i < entries.size(); ++i) {
            keys[i] = entries[i].key;
            strings[i] = entries[i].string;
            numbers[i] = entries[i].number;
            raw[i].key = keys[i].data();
            raw[i].type = entries[i].type;
            raw[i].string = entries[i].hasString ? strings[i].data() : nullptr;
            raw[i].number = &numbers[i];
        }
    }
    EngineEntries(const EngineEntries&) = delete;
    EngineEntries& operator=(const EngineEntries&) = delete;
};

// The game's FatalErrorF2 records instead of ending the process; the construct returns after it, so a test sees one fatal call.
bool gEngineFatal = false;
void RecordFatal(const char*, const char*, const char*)
{
    gEngineFatal = true;
}
void* Allocate(size_t bytes)
{
    return std::malloc(bytes);
}

// Frees the list the construct built, with the 0x100-byte string copies, when the test leaves the scope.
struct EngineList {
    obl::game::GameParameters list{};
    ~EngineList()
    {
        for (auto* node = list.m_first; node;) {
            auto* next = node->m_next;
            std::free(node->m_string);
            std::free(node);
            node = next;
        }
    }
};

// The engine's list from the `.pis` entries, as it was built before the converted JSON: the first letter of the key is the
// type (b, i, f, s), anything else is a fatal error. FromJson must agree with it.
void Construct(obl::game::GameParameters& list, const obl::pis::RawEntry* entries, int count, const char* path,
        const obl::game_parameters_construct::Env& env)
{
    list.m_first = nullptr;
    for (int i = 0; i < count; ++i) {
        const obl::pis::RawEntry& entry = entries[i];
        // Every fatal path returns: the game's FatalError never comes back, a test's recorder does, and nothing may be read after it.
        switch (entry.key[0]) {
        case 'b':
            if (entry.type != obl::pis::String) {
                env.fatal("GameParameters - wrong type in script for boolean (expecting string) - %s - %s", entry.key, path);
                return;
            }
            obl::game_parameters_construct::Add(list, entry.key, entry.string && !std::strcmp(entry.string, "true"), 0, 0, nullptr, env);
            break;
        case 'f':
            if (entry.type != obl::pis::Number) {
                env.fatal("GameParameters - wrong type in script for float (expecting float) - %s - %s", entry.key, path);
                return;
            }
            obl::game_parameters_construct::Add(list, entry.key, 0, 0, *reinterpret_cast<const uint32_t*>(entry.number), nullptr, env);
            break;
        case 'i':
            if (entry.type != obl::pis::Number) {
                env.fatal("GameParameters - wrong type in script for integer (expecting float) - %s - %s", entry.key, path);
                return;
            }
            obl::game_parameters_construct::Add(list, entry.key, 0, obl::FloatToIntWrap(*entry.number), 0, nullptr, env);
            break;
        case 's':
            if (entry.type != obl::pis::String) {
                env.fatal("GameParameters - wrong type in script for string (expecting string) - %s - %s", entry.key, path);
                return;
            }
            obl::game_parameters_construct::Add(list, entry.key, 0, 0, 0, entry.string, env);
            break;
        default: env.fatal("GameParameters - error - none defined type (needs prefix) - %s - %s", entry.key, path); return;
        }
    }
}

// Converts `text` with the converter and builds the engine's list from the same entries, then checks that the JSON holds what the
// engine's first node for each exact key holds: the bool, the int, the float bits and the string. Every entry must be a node.
void ExpectMatchesEngine(const std::string& text)
{
    std::string buffer = text;
    const auto entries = obl::pis::ParseEntries(buffer.data(), static_cast<int>(buffer.size()));
    OBL_REQUIRE(!entries.empty());
    EngineEntries raw(entries);
    EngineList engine;
    gEngineFatal = false;
    Construct(engine.list, raw.raw.data(), int(raw.raw.size()), "test.pis", {&RecordFatal, &Allocate});
    OBL_REQUIRE(!gEngineFatal);

    std::vector<obl::game::GameParameterNode*> nodes;
    for (auto* node = engine.list.m_first; node; node = node->m_next) nodes.push_back(node);
    OBL_REQUIRE(nodes.size() == entries.size());

    ordered_json parameters;
    std::string error;
    OBL_REQUIRE(ParseGameParameters(text, parameters, error));

    // The key lookup answers the first node of a key, so only the first node per key is the value the game reads.
    std::set<std::string> seen;
    for (const auto* node : nodes) {
        const std::string key = node->m_name;
        if (!seen.insert(key).second) continue;
        std::string name = key.substr(1);
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        OBL_REQUIRE(parameters.contains(name));
        const auto& value = parameters[name];
        switch (key[0]) {
        case 'b': OBL_CHECK(value.is_boolean() && value == (node->m_boolValue != 0)); break;
        case 'i': OBL_CHECK(value.is_number_integer() && value.get<int32_t>() == node->m_intValue); break;
        case 'f': {
            OBL_CHECK(value.is_number());
            const float f = static_cast<float>(value.get<double>());
            OBL_CHECK(std::bit_cast<uint32_t>(f) == node->m_floatBits);
            break;
        }
        case 's': OBL_CHECK(node->m_string != nullptr && value.is_string() && value == std::string(node->m_string)); break;
        default: OBL_CHECK(false); break;
        }
    }
    OBL_CHECK(parameters.size() == seen.size());
}

// The game's load: `text` converted, written as the converter writes it, read back and built with FromJson must give the list
// the engine builds from the `.pis` (one node per entry, so `text` repeats no key): count, order, name, bool, int, float bits and
// the string or its absence.
void ExpectJsonListMatchesEngine(const std::string& text)
{
    std::string buffer = text;
    const auto entries = obl::pis::ParseEntries(buffer.data(), static_cast<int>(buffer.size()));
    OBL_REQUIRE(!entries.empty());
    EngineEntries raw(entries);
    EngineList engine;
    gEngineFatal = false;
    Construct(engine.list, raw.raw.data(), int(raw.raw.size()), "test.pis", {&RecordFatal, &Allocate});
    OBL_REQUIRE(!gEngineFatal);

    ordered_json parameters;
    std::string error;
    OBL_REQUIRE(ParseGameParameters(text, parameters, error));
    const ordered_json written = {{"format", "obl.game_parameters"}, {"version", 1}, {"parameters", parameters}};
    const auto document = ordered_json::parse(written.dump(2) + "\n");
    EngineList loaded;
    const bool built = obl::game_parameters_construct::FromJson(loaded.list, document, {&RecordFatal, &Allocate}, error);
    if (!built) std::printf("  %s\n", error.c_str());
    OBL_REQUIRE(built);

    const auto* a = engine.list.m_first;
    const auto* b = loaded.list.m_first;
    for (; a && b; a = a->m_next, b = b->m_next) {
        OBL_CHECK(std::string(a->m_name) == std::string(b->m_name));
        OBL_CHECK(a->m_boolValue == b->m_boolValue);
        OBL_CHECK(a->m_intValue == b->m_intValue);
        OBL_CHECK(a->m_floatBits == b->m_floatBits);
        OBL_CHECK((a->m_string == nullptr) == (b->m_string == nullptr));
        if (a->m_string && b->m_string) OBL_CHECK(std::string(a->m_string) == std::string(b->m_string));
    }
    OBL_CHECK(a == nullptr);
    OBL_CHECK(b == nullptr);
}

bool LoadSchemas(obl::json_schema::Registry& registry)
{
    std::string error;
    const bool ok = obl::formats::LoadEmbeddedSchemas(registry, error);
    if (!ok) std::printf("  %s\n", error.c_str());
    return ok;
}

} // namespace

OBL_TEST_CASE("convert.game_parameters_follow_the_engine_reader")
{
    // Comments run to a CR; a repeated exact key keeps its first value (the key lookup answers the first node).
    const std::string text = "// header comment\r"
                             "\t'bPLAY_MOVIE'\t\t\"true\"\r"
                             "\t'iDAY_LENGTH_IN_SECONDS'\t1200\r"
                             "\t'fMAP_SCALE_Z'\t\t2.5\r"
                             "\t'fRATE'\t\t0.1\r"
                             "\t'sDEBUGFONT_NAME'\t\"Small Fonts\"\r"
                             "\t'bBUFFER_LIGHTMAPS'\t\"false\"  // trailing comment\r"
                             "\t'bPLAY_MOVIE'\t\"false\"\r";
    ordered_json parameters;
    std::string error;
    const bool parsed = ParseGameParameters(text, parameters, error);
    if (!parsed) std::printf("  %s\n", error.c_str());
    OBL_REQUIRE(parsed);
    OBL_REQUIRE(parameters.size() == 6);
    OBL_CHECK(parameters["play_movie"].is_boolean());
    OBL_CHECK(parameters["play_movie"] == true);
    OBL_CHECK(parameters["day_length_in_seconds"].is_number_integer());
    OBL_CHECK(parameters["day_length_in_seconds"] == 1200);
    OBL_CHECK(parameters["map_scale_z"].is_number_float());
    OBL_CHECK(parameters["map_scale_z"] == 2.5);
    OBL_CHECK(parameters["debugfont_name"].is_string());
    OBL_CHECK(parameters["debugfont_name"] == "Small Fonts");
    OBL_CHECK(parameters["buffer_lightmaps"] == false);
    // The float is the reader's float (the engine's 0.1f scale quirk included), so the JSON number reads back to the same bits.
    const auto entries = obl::pis::ParseEntries("'fRATE'\t\t0.1\r", 13);
    OBL_REQUIRE(entries.size() == 1);
    OBL_CHECK(static_cast<float>(parameters["rate"].get<double>()) == entries[0].number);
}

OBL_TEST_CASE("convert.game_parameters_refuse_bad_entries")
{
    // An unrecognized or upper-case type letter (the engine stops on it), a value of the wrong kind, a missing value, a string
    // without its closing quote, and two different keys that name the same parameter.
    const char* bad[] = {
            "'xFOO' 1\r",
            "'BFOO' \"true\"\r",
            "'bFOO' 1\r",
            "'iFOO' \"3\"\r",
            "'fFOO' \"1.5\"\r",
            "'sFOO' 3\r",
            "'iFOO'\r",
            "'sFOO' \"unterminated\r",
            "'bFOO' \"true\"\r'bfoo' \"false\"\r",
            "'fCamSpeed' 1\r", // the game rebuilds keys upper-cased, so this one could not be found again
    };
    for (const char* text : bad) {
        ordered_json parameters;
        std::string error;
        OBL_CHECK(!ParseGameParameters(text, parameters, error));
        OBL_CHECK(!error.empty());
    }
}

OBL_TEST_CASE("data.convert_game_parameters_of_installation")
{
    // Every entry of the installed GameParameters.pis reaches the JSON under its name with the value the engine's reader gives.
    const auto script = FindInstalledFile(OBL_GAME_DIR, "Data/Script/GameParameters.pis");
    if (!script) OBL_SKIP("no installation GameParameters.pis");
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(LoadSchemas(schemas));
    const obl::test::TempPath tmp("obl-params-test-installation");
    const auto& out = tmp.path;
    std::string error;
    OBL_REQUIRE(
            ConvertGameParameters(*obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(out), schemas, error));
    std::ifstream in(out / "rules" / "game_parameters.json");
    const auto document = ordered_json::parse(in, nullptr, false);
    OBL_REQUIRE(!document.is_discarded());
    OBL_CHECK(document["format"] == "obl.game_parameters");
    OBL_CHECK(document["version"] == 1);
    const auto& parameters = document["parameters"];

    std::string text;
    {
        std::ifstream pis(*script, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(pis), std::istreambuf_iterator<char>());
    }
    text.push_back('\0');
    const auto entries = obl::pis::ParseEntries(text.data(), static_cast<int>(text.size() - 1));
    OBL_REQUIRE(!entries.empty());
    for (const auto& entry : entries) {
        std::string name = entry.key.substr(1);
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        OBL_REQUIRE(parameters.contains(name));
        const char type = entry.key[0];
        if (type == 'b') OBL_CHECK(parameters[name] == (entry.hasString && entry.string == "true"));
        if (type == 'i') OBL_CHECK(parameters[name] == static_cast<int32_t>(entry.number));
        if (type == 'f') OBL_CHECK(static_cast<float>(parameters[name].get<double>()) == entry.number);
        if (type == 's') OBL_CHECK(parameters[name] == entry.string);
    }
    // The values the engine reads at start-up (game.cpp, guest_dispatch.cpp).
    OBL_CHECK(parameters["play_movie"] == true);
    OBL_CHECK(parameters["sound_enabled"] == true);
    OBL_CHECK(parameters["day_length_in_seconds"] == 1200);
    OBL_CHECK(parameters["debugfont_name"] == "Small Fonts");
    OBL_CHECK(parameters["debugfont_size"] == 6);
}

OBL_TEST_CASE("data.convert_game_parameters_match_engine")
{
    // The installed GameParameters.pis: the JSON holds, for every key, what the engine's parameter list holds
    // (the same common construct the game runs), float bits included.
    const auto script = FindInstalledFile(OBL_GAME_DIR, "Data/Script/GameParameters.pis");
    if (!script) OBL_SKIP("no installation GameParameters.pis");
    std::ifstream pis(*script, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(pis)), std::istreambuf_iterator<char>());
    ExpectMatchesEngine(text);
}

OBL_TEST_CASE("data.game_parameters_json_list_matches_engine")
{
    // What the game loads from packs/base/rules/game_parameters.json is the list the parameter-list constructor built from the
    // installed GameParameters.pis.
    const auto script = FindInstalledFile(OBL_GAME_DIR, "Data/Script/GameParameters.pis");
    if (!script) OBL_SKIP("no installation GameParameters.pis");
    std::ifstream pis(*script, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(pis)), std::istreambuf_iterator<char>());
    ExpectJsonListMatchesEngine(text);
}

OBL_TEST_CASE("convert.game_parameters_json_list_matches_engine")
{
    // Every type, an integral float (written as 60.0, so it stays a float), the float rule's hard cases and a digit in a name.
    const std::string text = "'bPLAY_MOVIE' \"true\"\r"
                             "'bOFF' \"false\"\r"
                             "'iDAY_LENGTH_IN_SECONDS' 1200\r"
                             "'iNEG' -2.7\r"
                             "'fMAP_MAX_VELOCITY' 60.0\r"
                             "'fTENTH' 0.1\r"
                             "'fNEGZERO' -0\r"
                             "'fTINY' 0.00000000000000000000000000000000000000000000140\r"
                             "'fLARGEST' 340282346638528859811704183484516925440\r"
                             "'sDEBUGFONT_NAME' \"Small Fonts\"\r"
                             "'sEMPTY' \"\"\r"
                             "'iMAP_GRIDSIZE_XY2' 40\r";
    ExpectJsonListMatchesEngine(text);
}

OBL_TEST_CASE("convert.game_parameters_json_refused_whole")
{
    // A document the converter cannot write is refused before the first node, so the list stays empty.
    const char* bad[] = {
            R"({"format":"obl.game_parameters","version":1,"parameters":{"ok":1,"nested":{}}})",
            R"({"format":"obl.game_parameters","version":1,"parameters":{"ok":1,"Upper":2}})",
            R"({"format":"obl.game_parameters","version":1,"parameters":{"ok":1,"big":3000000000}})",
            R"({"format":"obl.game_parameters","version":1,"parameters":{"ok":1,"none":null}})",
            R"({"format":"obl.other","version":1,"parameters":{}})",
            R"({"format":"obl.game_parameters","version":2,"parameters":{}})",
            R"({"format":"obl.game_parameters","version":1})",
    };
    for (const char* text : bad) {
        EngineList loaded;
        std::string error;
        OBL_CHECK(!obl::game_parameters_construct::FromJson(loaded.list, ordered_json::parse(text), {&RecordFatal, &Allocate}, error));
        OBL_CHECK(!error.empty());
        OBL_CHECK(loaded.list.m_first == nullptr);
    }
    const std::string longString = R"({"format":"obl.game_parameters","version":1,"parameters":{"s":")" + std::string(256, 'x') + "\"}}";
    EngineList loaded;
    std::string error;
    OBL_CHECK(!obl::game_parameters_construct::FromJson(loaded.list, ordered_json::parse(longString), {&RecordFatal, &Allocate}, error));
}

OBL_TEST_CASE("convert.game_parameters_bits_match_engine")
{
    // Values that test the rules rather than the installation: the float bits of 0.1 and -0, a denormal, the largest float, a
    // long fraction; the int truncation of a negative value, a value beyond int32 (wraps), beyond int64 (0) and infinity (0).
    const std::string text = "'fTENTH' 0.1\r"
                             "'fNEGZERO' -0\r"
                             "'fTINY' 0.00000000000000000000000000000000000000000000140\r"
                             "'fLARGEST' 340282346638528859811704183484516925440\r"
                             "'fLONG' 0.1234567890123456789\r"
                             "'iNEG' -2.7\r"
                             "'iWRAP' 3000000000\r"
                             "'iBEYOND' 100000000000000000000\r"
                             "'iINF' 1" +
            std::string(400, '0') +
            "\r"
            "'bON' \"true\"\r"
            "'sTEXT' \"with spaces\"\r";
    ExpectMatchesEngine(text);
}

OBL_TEST_CASE("convert.game_parameters_refuse_what_the_engine_slots_cannot_hold")
{
    // The engine copies a key (with its type letter) and a string into 0x100-byte slots, and a float must be finite for JSON.
    std::string longKey = "'f" + std::string(255, 'A') + "' 1\r";
    std::string longString = "'sLONG' \"" + std::string(256, 'x') + "\"\r";
    std::string infinite = "'fBIG' 1" + std::string(400, '0') + "\r";
    const std::string fits = "'f" + std::string(254, 'A') + "' 1\r";
    for (const std::string& text : {longKey, longString, infinite}) {
        ordered_json parameters;
        std::string error;
        OBL_CHECK(!ParseGameParameters(text, parameters, error));
        OBL_CHECK(!error.empty());
    }
    ordered_json parameters;
    std::string error;
    OBL_CHECK(ParseGameParameters(fits, parameters, error));
}
