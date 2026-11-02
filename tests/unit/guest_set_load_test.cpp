#include "core/ascii_case.h"
#include "unit_test.h"
#include "game/tables/guest_set_load.h"
#include "game/objects/guest_sets.h"
#include "game/tables/world_params_tables.h"
#include "importer/src/readers/world_params_process.h"
#include "importer/src/writers/guest_set_convert.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "temp_dir.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace obl;

namespace {
using nlohmann::ordered_json;
using tables::WorldParams;

obl::json_schema::Registry Schemas()
{
    auto dir = obl::formats::FindSchemaDirectory(std::filesystem::current_path());
    OBL_REQUIRE(dir);
    obl::json_schema::Registry schemas;
    std::string error;
    OBL_REQUIRE(obl::formats::LoadSchemaDirectory(*dir, schemas, error));
    return schemas;
}

std::unique_ptr<WorldParams> Zeroed()
{
    return std::make_unique<WorldParams>();
}

// The engine's parser (the world params process routine) on `text`: the fatal hook records instead of exiting.
bool g_fatal = false;
void RecordFatal(const char*, const char*)
{
    g_fatal = true;
}
void* Allocate(size_t bytes)
{
    return std::malloc(bytes);
}
bool EngineLoads(std::string text, WorldParams& p)
{
    std::vector<char> buffer(text.begin(), text.end());
    const world_params_add::Env env{&RecordFatal, &Allocate};
    g_fatal = false;
    const bool ok = world_params_process::Process(p, env, buffer.data(), uint32_t(buffer.size()));
    return ok && !g_fatal;
}

// The file as the engine reads it: every byte XOR 0xFF (the .ble encoding).
std::string Decode(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(file)), {});
    for (char& c : text) c = static_cast<char>(static_cast<uint8_t>(c) ^ 0xFF);
    return text;
}

// The guest part of the record: what the loader replaces (records, name pools and their counts), byte for byte.
// Guest keys and sprites are canonical in the packs, so their spelling is left out of the comparison.
auto FoldedGuests(const WorldParams& params)
{
    auto guests = params.m_guests;
    for (auto& guest : guests) {
        for (char& c : guest.m_key) c = obl::ascii::Lower(c);
        for (char& c : guest.m_Sprite) c = obl::ascii::Lower(c);
    }
    return guests;
}

bool SameGuests(const WorldParams& a, const WorldParams& b)
{
    return a.m_numGuests == b.m_numGuests && FoldedGuests(a) == FoldedGuests(b) &&
            std::memcmp(a.m_names, b.m_names, sizeof(a.m_names)) == 0 &&
            std::memcmp(a.m_nameCounts, b.m_nameCounts, sizeof(a.m_nameCounts)) == 0;
}

// Two guests (one with an engine spelling for its key and sprite, one female), a few pool entries: the engine grammar.
constexpr const char* kSample =
        "\"GUEST_Lad\" \"Lad_GENDER\" \"M\" \"Lad_SPRITE\" \"Fit_Bloke\" \"Lad_DEF_SPRITE\" \"2\" \"Lad_MONEY\" \"120.5\"\n"
        "\"Lad_DRUNK_TYPE\" \"HORNY\" \"Lad_COLOUR\" \"1\" \"Lad_NIGHTS_MIN\" \"1\" \"Lad_NIGHTS_MAX\" \"4\"\n"
        "\"Lad_ALCOHOLPREF\" \"0.4\" \"0.9\" \"Lad_SNACKSPREF\" \"0.2\" \"0.6\" \"Lad_SLEEPPREF\" \"0.1\" \"0.3\"\n"
        "\"Lad_BEAUTYTOL\" \"0.2\" \"0.8\" \"Lad_SAFETY_EFFECT\" \"3\" \"-2\" \"Lad_CLEANLINESS_START\" \"0.5\" \"0.7\"\n"
        "\"Lad_SLEEPTIME\" \"22\" \"24\" \"Lad_WAKETIME\" \"7\" \"9\" \"Lad_MAX_ALCOHOL\" \"1\" \"Lad_MAX_HARDSEX\" \"2\"\n"
        "\"Lad_START_DRUNK\" \"0.3\" \"Lad_START_ILL\" \"0.6\" \"Lad_DRUNKDECAY\" \"0.01\" \"Lad_ILL_DECAY\" \"0.02\"\n"
        "\"GUEST_lass\" \"lass_GENDER\" \"F\" \"lass_SPRITE\" \"girl\" \"lass_DRUNK_TYPE\" \"VANDALISE\" \"lass_SWIM_DIST\" \"12.25\"\n"
        "\"MALENAME\" \"ID_JOHN\" \"MALENAME\" \"ID_TOM\" \"FEMALENAME\" \"ID_ANN\" \"SURNAME\" \"ID_DEL BOSCO\"\n";

// A document of `count` types named k0, k1, ... (the converter's output shape) with one male name.
ordered_json DocumentOfTypes(size_t count)
{
    ordered_json document;
    std::string error;
    OBL_REQUIRE(convert::ParseGuestSet(kSample, document, error));
    ordered_json type = document["types"][0];
    document["types"] = ordered_json::array();
    for (size_t i = 0; i < count; ++i) {
        type["key"] = "k" + std::to_string(i);
        document["types"].push_back(type);
    }
    return document;
}

void WriteSet(const std::filesystem::path& pack, const char* set, const ordered_json& document)
{
    std::filesystem::create_directories(pack / "guests");
    std::ofstream out(pack / "guests" / (std::string(set) + ".json"), std::ios::binary);
    out << document.dump(2) << '\n';
}

} // namespace

OBL_TEST_CASE("guest_set.names_map_to_their_sets")
{
    for (const guest_sets::Set& set : guest_sets::kSets) {
        OBL_CHECK(guest_sets::SetForList(std::string("Data/Script/") + set.stem + ".pis") == std::string(set.name));
        OBL_CHECK(guest_sets::SetForList(std::string("data\\script\\") + set.stem + ".ble") == std::string(set.name));
    }
    OBL_CHECK(guest_sets::SetForList("Data/Script/Guests03.pis") == std::string("campaign_03"));
    OBL_CHECK(guest_sets::SetForList("data/script/SandboxH_Guests.PIS") == std::string("sandbox_hard"));
    OBL_CHECK(guest_sets::SetForList("guests12.pis") == std::string("campaign_12"));
    OBL_CHECK(guest_sets::SetForList("Data/Script/Guests13.pis") == nullptr);
    OBL_CHECK(guest_sets::SetForList("Data/Script/Guests03") == nullptr);
    OBL_CHECK(guest_sets::SetForList("Data/Script/Guests03.txt") == nullptr);
    OBL_CHECK(guest_sets::SetForList("") == nullptr);
}

OBL_TEST_CASE("guest_set.pack_set_names_cannot_leave_the_folder")
{
    OBL_CHECK(guest_sets::IsPackSetName("l0"));
    OBL_CHECK(guest_sets::IsPackSetName("own_set-2"));
    OBL_CHECK(!guest_sets::IsPackSetName(""));
    OBL_CHECK(!guest_sets::IsPackSetName("../l0"));
    OBL_CHECK(!guest_sets::IsPackSetName("a/b"));
    OBL_CHECK(!guest_sets::IsPackSetName("L0"));
}

OBL_TEST_CASE("guest_set.load_matches_engine_on_a_sample")
{
    const obl::test::TempPath tmp("obl-guest-set-sample");
    ordered_json document;
    std::string error;
    OBL_REQUIRE(convert::ParseGuestSet(kSample, document, error));
    WriteSet(tmp.path, "campaign_01", document);

    auto engine = Zeroed();
    OBL_REQUIRE(EngineLoads(kSample, *engine));
    auto loaded = Zeroed();
    OBL_REQUIRE(guest_set::LoadPack(tmp.path, "campaign_01", *loaded, error));
    OBL_CHECK(SameGuests(*engine, *loaded));
    OBL_CHECK(loaded->m_numGuests == 2);
    OBL_CHECK(std::string(loaded->m_guests[0].m_key) == "lad");
    OBL_CHECK(std::string(loaded->m_guests[0].m_Sprite) == "fit_bloke");
}

OBL_TEST_CASE("guest_set.load_keeps_the_catalog_when_it_replaces_the_guests")
{
    const obl::test::TempPath tmp("obl-guest-set-keep");
    ordered_json document;
    std::string error;
    OBL_REQUIRE(convert::ParseGuestSet(kSample, document, error));
    WriteSet(tmp.path, "campaign_01", document);

    auto params = Zeroed();
    params->m_numBuildings = 9;
    params->m_buildings.resize(9);
    params->m_buildings[0].m_GridWidth = 4;
    OBL_REQUIRE(guest_set::LoadPack(tmp.path, "campaign_01", *params, error));
    OBL_CHECK(params->m_numBuildings == 9);
    OBL_CHECK(params->m_buildings[0].m_GridWidth == 4);
    OBL_CHECK(params->m_numGuests == 2);
}

OBL_TEST_CASE("guest_set.load_refuses_transactionally")
{
    const obl::test::TempPath tmp("obl-guest-set-bad");
    auto out = Zeroed();
    out->m_numGuests = 7;
    out->m_nameCounts[0] = 3;
    const auto before = std::make_unique<WorldParams>(*out);

    // A null reason only checks that the file is named and nothing changes (the schema's own wording is not ours).
    const auto refuse = [&](const ordered_json& bad, const char* reason) {
        WriteSet(tmp.path, "campaign_01", bad);
        std::string error;
        OBL_CHECK(!guest_set::LoadPack(tmp.path, "campaign_01", *out, error));
        if (reason) OBL_CHECK(error.find(reason) != std::string::npos);
        OBL_CHECK(error.find("campaign_01.json") != std::string::npos);
        OBL_CHECK(SameGuests(*before, *out));
    };

    refuse(DocumentOfTypes(21), "more than 20 guest types");

    ordered_json names = DocumentOfTypes(1);
    names["names"]["male"] = ordered_json::array();
    for (int i = 0; i < 141; ++i) names["names"]["male"].push_back("ID_N" + std::to_string(i));
    refuse(names, "male names: engine name pool capacity exceeded");

    ordered_json twice = DocumentOfTypes(2);
    twice["types"][1]["key"] = "k0";
    refuse(twice, "guest key appears twice");

    ordered_json gender = DocumentOfTypes(1);
    gender["types"][0]["gender"] = "x";
    refuse(gender, nullptr);

    std::string error;
    OBL_CHECK(!guest_set::LoadPack(tmp.path, "campaign_99", *out, error));
    OBL_CHECK(error.find("campaign_99.json") != std::string::npos);
    OBL_CHECK(SameGuests(*before, *out));
}

OBL_TEST_CASE("data.guest_set_load_matches_engine")
{
    // Every installed guest file, converted and loaded by the loader, must hold the same guest records and name pools as the
    // engine's own parser reads from the same .ble text.
    if (!convert::FindInstalledFile(OBL_GAME_DIR, "Data/Script/Guests01.ble")) OBL_SKIP("no installation guest files");
    const obl::test::TempPath tmp("obl-guest-set-data");
    int files = 0;
    std::string error;
    OBL_REQUIRE(convert::ConvertGuestSets(
            *obl::convert::OpenDirectorySource(OBL_GAME_DIR), *obl::convert::OpenDirectorySink(tmp.path), Schemas(), files, error));
    OBL_CHECK(files == 15);

    for (const guest_sets::Set& set : guest_sets::kSets) {
        const auto path = convert::FindInstalledFile(OBL_GAME_DIR, std::string("Data/Script/") + set.stem + ".ble");
        OBL_REQUIRE(path);
        auto engine = Zeroed();
        OBL_REQUIRE(EngineLoads(Decode(*path), *engine));
        auto loaded = Zeroed();
        OBL_REQUIRE(guest_set::LoadPack(tmp.path, set.name, *loaded, error));
        OBL_CHECK(SameGuests(*engine, *loaded));
    }
}
