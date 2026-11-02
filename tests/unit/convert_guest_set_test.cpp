#include "unit_test.h"
#include "game/tables/world_params_tables.h"
#include "importer/src/readers/world_params_process.h"
#include "importer/src/writers/guest_set_convert.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "temp_dir.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace obl::convert;

namespace {

obl::json_schema::Registry Schemas()
{
    auto dir = obl::formats::FindSchemaDirectory(std::filesystem::current_path());
    OBL_REQUIRE(dir);
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadSchemaDirectory(*dir, schemas, error));
    return schemas;
}

std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// The file as the engine reads it: every byte XOR 0xFF (the .ble encoding).
std::string Decode(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(file)), {});
    for (char& c : text) c = static_cast<char>(static_cast<uint8_t>(c) ^ 0xFF);
    return text;
}

// The engine's own parser (the world-parameter parser) on `text`, into a zeroed WorldParams. The fatal hook records instead of exiting.
bool g_fatal = false;
void RecordFatal(const char*, const char*)
{
    g_fatal = true;
}
void* Allocate(size_t bytes)
{
    return std::malloc(bytes);
}

bool EngineLoads(std::string text, obl::tables::WorldParams& p)
{
    std::vector<char> buffer(text.begin(), text.end());
    const obl::world_params_add::Env env{&RecordFatal, &Allocate};
    g_fatal = false;
    const bool ok = obl::world_params_process::Process(p, env, buffer.data(), uint32_t(buffer.size()));
    return ok && !g_fatal;
}

// Exact comparison of a float: the document's text must come back as the engine's float32, bit for bit.
void ExpectBits(float engine, float document)
{
    OBL_CHECK(std::bit_cast<uint32_t>(engine) == std::bit_cast<uint32_t>(document));
}

float Value(const nlohmann::ordered_json& object, const char* name)
{
    return object.contains(name) ? float(object[name].get<double>()) : 0.0f;
}

int IntValue(const nlohmann::ordered_json& object, const char* name)
{
    return object.contains(name) ? object[name].get<int>() : 0;
}

void ExpectPair(const std::array<float, 2>& engine, const nlohmann::ordered_json& group, const char* name)
{
    const float low = group.contains(name) ? float(group[name][0].get<double>()) : 0.0f;
    const float high = group.contains(name) ? float(group[name][1].get<double>()) : 0.0f;
    ExpectBits(engine[0], low);
    ExpectBits(engine[1], high);
}

// The engine's index order: GuestDef::m_Pref, the seven opinions, and GuestDef::m_Max with its MAX_ names.
constexpr const char* kPreferenceNames[] = {"alcohol", "amusement", "meals", "party_fun", "snacks", "soft_drinks", "souvenirs",
        "sports_fun", "sunbathe", "swimming", "water_fun", "stroll", "sleep"};
constexpr const char* kOpinionNames[] = {"beauty", "crowding", "attractiveness", "price", "variety", "cleanliness", "safety"};
constexpr const char* kMaxNames[] = {"accommodation", "alcohol", "amusement", "first_aid", "headache", "meals", "party_fun", "shower",
        "snacks", "soft_drinks", "souvenirs", "sports_fun", "sunbathe", "swim_fun", "toilet", "water_sports_fun", "stroll", "leave",
        "vandalise", "fight", "flirt", "umbrellas", "suntan", "intimacy", "sleep", "scared"};
constexpr const char* kDrunkNames[] = {"none", "tired", "flirt", "fight", "vandalise"};

void ExpectGuest(const obl::tables::GuestDef& engine, const nlohmann::ordered_json& type)
{
    OBL_CHECK(Lower(engine.m_key) == type["key"].get<std::string>());
    OBL_CHECK(std::string(engine.m_Name) == type["name"].get<std::string>());
    OBL_CHECK((engine.m_Gender != 0) == (type["gender"] == "f"));
    OBL_CHECK(Lower(engine.m_Sprite) == type["sprite"].get<std::string>());
    OBL_CHECK(engine.m_DefSprite == type["default_sprite"].get<int>());
    OBL_CHECK(engine.m_Colour == type["colour"].get<int>());
    OBL_CHECK(std::string(kDrunkNames[engine.m_DrunkType]) == type["drunk_type"].get<std::string>());
    ExpectBits(engine.m_Money, Value(type, "money"));
    ExpectBits(engine.m_Litter, Value(type, "litter"));
    ExpectBits(engine.m_Attractive, Value(type, "attractive"));
    ExpectBits(engine.m_AttractivePref, Value(type, "attractive_pref"));
    ExpectBits(engine.m_SwimEnergy, Value(type, "swim_energy"));
    ExpectBits(engine.m_SwimDist, Value(type, "swim_dist"));
    ExpectBits(engine.m_SunburnTime, Value(type, "sunburn_time"));
    OBL_CHECK(engine.m_NightsMin == type["nights"][0].get<int>());
    OBL_CHECK(engine.m_NightsMax == type["nights"][1].get<int>());
    ExpectPair(engine.m_SleepTime, type, "sleep_time");
    ExpectPair(engine.m_WakeTime, type, "wake_time");

    const auto& preferences = type["preferences"];
    for (size_t i = 0; i < std::size(kPreferenceNames); ++i) ExpectPair(engine.m_Pref[i], preferences, kPreferenceNames[i]);
    const auto& tolerances = type["tolerances"];
    const auto& effects = type["opinion_effects"];
    const auto& starts = type["opinion_start"];
    for (size_t i = 0; i < std::size(kOpinionNames); ++i) {
        ExpectPair(engine.m_OpinionTol[i], tolerances, kOpinionNames[i]);
        ExpectPair(engine.m_OpinionEffect[i], effects, kOpinionNames[i]);
        ExpectPair(engine.m_OpinionStart[i], starts, kOpinionNames[i]);
    }

    const auto& start = type["start"];
    ExpectBits(engine.m_StartBladder, Value(start, "bladder"));
    ExpectBits(engine.m_StartFight, Value(start, "fight"));
    ExpectBits(engine.m_StartFlirt, Value(start, "flirt"));
    ExpectBits(engine.m_StartIll, Value(start, "ill"));
    ExpectBits(engine.m_StartDrunk, Value(start, "drunk"));
    ExpectBits(engine.m_StartSunburn, Value(start, "sunburn"));
    ExpectBits(engine.m_StartVandalise, Value(start, "vandalise"));
    const auto& decay = type["decay"];
    ExpectBits(engine.m_DrunkDecay, Value(decay, "drunk"));
    ExpectBits(engine.m_FlirtDecay, Value(decay, "flirt"));
    ExpectBits(engine.m_FightDecay, Value(decay, "fight"));
    ExpectBits(engine.m_VandaliseDecay, Value(decay, "vandalise"));
    ExpectBits(engine.m_IllDecay, Value(decay, "ill"));

    const auto& max = type["max"];
    for (size_t i = 0; i < std::size(kMaxNames); ++i) OBL_CHECK(engine.m_Max[i] == IntValue(max, kMaxNames[i]));
}

// Where the engine keeps each name pool: male from 0, female from 0x8c, surnames from 0x118 (WorldParams::m_names).
void ExpectNames(const obl::tables::WorldParams& engine, const nlohmann::ordered_json& names)
{
    struct Pool {
        const char* key;
        int first;
        int slot;
    };
    const Pool pools[] = {{"male", 0, 0}, {"female", 0x8c, 1}, {"surnames", 0x118, 2}};
    for (const Pool& pool : pools) {
        const auto& list = names[pool.key];
        OBL_CHECK(engine.m_nameCounts[pool.slot] == int(list.size()));
        for (size_t i = 0; i < list.size(); ++i) OBL_CHECK(std::string(engine.m_names[pool.first + int(i)]) == list[i].get<std::string>());
    }
}

// The installation's guest files and their set names (`campaign_01` ... `sandbox_hard`).
struct InstalledSet {
    const char* file;
    const char* set;
};
constexpr InstalledSet kInstalledSets[] = {{"Data/Script/Guests01.ble", "campaign_01"}, {"Data/Script/Guests02.ble", "campaign_02"},
        {"Data/Script/guests03.ble", "campaign_03"}, {"Data/Script/Guests04.ble", "campaign_04"},
        {"Data/Script/Guests05.ble", "campaign_05"}, {"Data/Script/Guests06.ble", "campaign_06"},
        {"Data/Script/Guests07.ble", "campaign_07"}, {"Data/Script/Guests08.ble", "campaign_08"},
        {"Data/Script/Guests09.ble", "campaign_09"}, {"Data/Script/Guests10.ble", "campaign_10"},
        {"Data/Script/Guests11.ble", "campaign_11"}, {"Data/Script/Guests12.ble", "campaign_12"},
        {"Data/Script/SandboxE_guests.ble", "sandbox_easy"}, {"Data/Script/SandboxM_Guests.ble", "sandbox_medium"},
        {"Data/Script/SandboxH_Guests.ble", "sandbox_hard"}};

const char* const kGrammarSample = R"(//a comment outside quotes is ignored by the engine
"GUEST_Lad"
"Lad_NAME" ""
"Lad_GENDER" "M"
"Lad_SPRITE" "Fit_Bloke"
"Lad_DEF_SPRITE" "2"
"Lad_COLOUR" "300"
"Lad_DRUNK_TYPE" "HORNY"
"Lad_NAMESET" "MALE"
"Lad_MONEY" "120"
"Lad_NIGHTS_MIN" "1"
"Lad_NIGHTS_MAX" "4"
"Lad_SLEEPTIME" "22" "24"
"Lad_ALCOHOLPREF" "0.4" "0.9"
"Lad_TOTTYTOL" "0.2" "0.8"
"Lad_TOTTY_EFFECT" "0.01" "0.02"
"Lad_START_DRUNK" "0.5"
"Lad_DRUNKDECAY" "0.01"
"Lad_MAX_HARDSEX" "3"
"MALENAME" "ID_JOHN"
"SURNAME" "ID_SMITH"
"SURNAME" "ID_DEL BOSCO"
"FEMALENAME" "ID_ANN"
)";

} // namespace

OBL_TEST_CASE("convert.guest_set_parses_the_engine_grammar")
{
    nlohmann::ordered_json document;
    std::string error;
    OBL_REQUIRE(ParseGuestSet(kGrammarSample, document, error));
    const auto& type = document["types"][0];
    OBL_CHECK(document["format"] == "obl.guest_set");
    OBL_CHECK(type["key"] == "lad");
    OBL_CHECK(type["name"] == "");
    OBL_CHECK(type["gender"] == "m");
    OBL_CHECK(type["sprite"] == "fit_bloke");
    OBL_CHECK(type["default_sprite"] == 2);
    OBL_CHECK(type["colour"] == 44); // 300 wraps to a byte, as the engine stores it
    OBL_CHECK(type["drunk_type"] == "flirt");
    OBL_CHECK(float(type["money"].get<double>()) == 120.0f);
    OBL_CHECK(type["nights"] == nlohmann::ordered_json::array({1, 4}));
    OBL_CHECK(type["sleep_time"] == nlohmann::ordered_json::array({22, 24}));
    OBL_CHECK(float(type["preferences"]["alcohol"][0].get<double>()) == 0.4f);
    OBL_CHECK(float(type["preferences"]["alcohol"][1].get<double>()) == 0.9f);
    OBL_CHECK(float(type["tolerances"]["attractiveness"][1].get<double>()) == 0.8f);
    OBL_CHECK(float(type["opinion_effects"]["attractiveness"][0].get<double>()) == 0.01f);
    OBL_CHECK(float(type["start"]["drunk"].get<double>()) == 0.5f);
    OBL_CHECK(float(type["decay"]["drunk"].get<double>()) == 0.01f);
    OBL_CHECK(type["max"]["intimacy"] == 3);
    OBL_CHECK(!type["preferences"].contains("snacks"));
    OBL_CHECK(document["names"]["male"] == nlohmann::ordered_json::array({"ID_JOHN"}));
    OBL_CHECK(document["names"]["surnames"] == nlohmann::ordered_json::array({"ID_SMITH", "ID_DEL BOSCO"}));
    OBL_CHECK(document["names"]["female"] == nlohmann::ordered_json::array({"ID_ANN"}));
    OBL_CHECK(obl::formats::ValidateDocument(Schemas(), "obl.guest_set", document, error));
}

OBL_TEST_CASE("convert.guest_set_refuses_what_the_engine_refuses")
{
    // Each refused text is the valid base record plus one defect, so it fails for the reason its message names. The base
    // itself must parse: that is the control for every case below.
    const std::string base = "\"GUEST_a\" \"a_GENDER\" \"M\" \"a_SPRITE\" \"x\"\n";
    nlohmann::ordered_json document;
    std::string error;
    OBL_REQUIRE(ParseGuestSet(base, document, error));

    struct Refusal {
        std::string text;
        const char* message;
    };
    const std::vector<Refusal> refused = {
            {"\"a_MONEY\" \"1\"\n" + base, "no GUEST_ line before it"},
            {base + "\"GUEST_a\" \"a_GENDER\" \"M\" \"a_SPRITE\" \"x\"\n", "appears twice"},
            {base + "\"a_BOGUS\" \"1\"\n", "unrecognized guest field"},
            {base + "\"a_MONEY\"\n", "has no value"},
            {base + "\"a_MONEY\" \"1e999\"\n", "is not a finite number"},
            {base + "\"a_DRUNKTYPE\" \"7\"\n", "DRUNKTYPE is not a drunk type"},
            {base + "\"a_DRUNK_TYPE\" \"SAD\"\n", "DRUNK_TYPE is not"},
            {base + "\"a_NAME\" \"" + std::string(128, 'n') + "\"\n", "NAME is longer"},
            {"\"GUEST_a\" \"a_GENDER\" \"X\" \"a_SPRITE\" \"x\"\n", "GENDER is not M or F"},
            {"\"GUEST_a\" \"a_SPRITE\" \"x\"\n", "has no GENDER"},
            {base + "\"BOGUS\" \"x\"\n", "unrecognized keyword BOGUS"},
            {base + "\"MALENAME\" \"ID:X\"\n", "is not a text key"},
            {base + "\"SURNAME\"\n", "has no value"},
            {base + "\"a_NAME\" \"unclosed\n", "odd number of quotes"},
    };
    for (const Refusal& refusal : refused) {
        error.clear();
        OBL_CHECK(!ParseGuestSet(refusal.text, document, error));
        OBL_CHECK(error.find(refusal.message) != std::string::npos);
    }

    // The engine's limits: 20 guests and 140 male names are the most it holds.
    const auto guest = [](int i) {
        const std::string key = "g" + std::to_string(i);
        return "\"GUEST_" + key + "\" \"" + key + "_GENDER\" \"M\" \"" + key + "_SPRITE\" \"x\"\n";
    };
    std::string guests;
    for (int i = 0; i < 20; ++i) guests += guest(i);
    OBL_REQUIRE(ParseGuestSet(guests, document, error));
    OBL_CHECK(!ParseGuestSet(guests + guest(20), document, error));
    OBL_CHECK(error.find("more than 20") != std::string::npos);

    std::string names = base;
    for (int i = 0; i < 140; ++i) names += "\"MALENAME\" \"ID_N" + std::to_string(i) + "\"\n";
    OBL_REQUIRE(ParseGuestSet(names, document, error));
    OBL_CHECK(!ParseGuestSet(names + "\"MALENAME\" \"ID_N140\"\n", document, error));
    OBL_CHECK(error.find("pool is full") != std::string::npos);
}

OBL_TEST_CASE("data.convert_guest_sets_match_engine")
{
    // Every installed guest file, converted and written, must read back as the engine's own loader builds it from the same text:
    // the same records, every field (zeros included, floats by their bits), the same name pools.
    if (!FindInstalledFile(OBL_GAME_DIR, "Data/Script/Guests01.ble")) OBL_SKIP("no installation guest files");
    const obl::test::TempPath tmp("obl-guest-sets");
    const auto& out = tmp.path;
    int files = 0;
    std::string error;
    OBL_REQUIRE(ConvertGuestSets(
            *obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(out), Schemas(), files, error));
    OBL_CHECK(files == 15);

    for (const InstalledSet& installed : kInstalledSets) {
        const auto path = FindInstalledFile(OBL_GAME_DIR, installed.file);
        OBL_REQUIRE(path);
        std::ifstream in(out / "guests" / (std::string(installed.set) + ".json"), std::ios::binary);
        const auto document = nlohmann::ordered_json::parse(std::string((std::istreambuf_iterator<char>(in)), {}));

        auto engine = std::make_unique<obl::tables::WorldParams>();
        OBL_REQUIRE(EngineLoads(Decode(*path), *engine));
        OBL_CHECK(engine->m_numGuests == int(document["types"].size()));
        for (size_t i = 0; i < document["types"].size() && int(i) < engine->m_numGuests; ++i)
            ExpectGuest(engine->m_guests[i], document["types"][i]);
        ExpectNames(*engine, document["names"]);
    }
}
