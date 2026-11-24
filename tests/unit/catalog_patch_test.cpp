// A mod's catalog/index.patch.json appends records to the pack's catalog (src/game/tables/catalog_load.cpp, docs/modding/mods.md).

#include "unit_test.h"

#include "core/pack_path.h"
#include "game/tables/catalog_load.h"
#include "temp_dir.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace obl;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct ModsGuard {
    ~ModsGuard() { pack_path::SetActiveMods({}); }
};

json ReadJson(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return json::parse(in);
}

void WriteJson(const fs::path& file, const json& doc)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << doc.dump(2);
}

// A game directory with the L0 pack's catalog in packs/l0.
struct Game {
    test::TempPath dir{"obl_catalog_patch"};
    fs::path pack = dir.path / "packs" / "l0";
    Game()
    {
        fs::create_directories(pack);
        fs::copy(fs::path(OBL_REPO_DIR) / "data/packs/l0/catalog", pack / "catalog", fs::copy_options::recursive);
    }

    // A record of the pack under a new key, written into the mod.
    void Record(const std::string& mod, const std::string& kind, const std::string& from, const std::string& key,
            const json& changes = json::object())
    {
        json doc = ReadJson(pack / "catalog" / kind / (from + ".json"));
        doc["key"] = key;
        doc.update(changes);
        WriteJson(dir.path / "mods" / mod / "catalog" / kind / (key + ".json"), doc);
    }
    void Patch(const std::string& mod, const json& add)
    {
        WriteJson(dir.path / "mods" / mod / "catalog" / "index.patch.json",
                {{"format", "obl.catalog_index_patch"}, {"version", 1}, {"add", add}});
    }
    bool Load(tables::WorldParams& params, std::string& error) { return catalog::LoadPack(pack, params, error); }
};

} // namespace

OBL_TEST_CASE("catalog_patch.records_are_appended_in_load_order")
{
    ModsGuard guard;
    Game game;
    game.Record("mod_a", "buildings", "kiosk", "kiosk_a");
    game.Record("mod_a", "buildings", "kiosk", "kiosk_b");
    game.Patch("mod_a", {{"buildings", {"kiosk_a", "kiosk_b"}}});
    game.Record("mod_b", "buildings", "kiosk", "kiosk_c");
    game.Patch("mod_b", {{"buildings", {"kiosk_c"}}});

    auto plain = std::make_unique<tables::WorldParams>();
    std::string error;
    OBL_REQUIRE(game.Load(*plain, error));
    const int base = plain->m_numBuildings;

    pack_path::SetActiveMods({"mod_a", "mod_b"});
    auto params = std::make_unique<tables::WorldParams>();
    OBL_REQUIRE(game.Load(*params, error));
    OBL_CHECK_EQ(params->m_numBuildings, base + 3);
    // The pack's records keep their indices; the mods' records follow in load order.
    OBL_CHECK_EQ(std::string(params->m_buildings[0].m_key), std::string(plain->m_buildings[0].m_key));
    OBL_CHECK_EQ(std::string(params->m_buildings[base - 1].m_key), std::string(plain->m_buildings[base - 1].m_key));
    OBL_CHECK_EQ(std::string(params->m_buildings[base].m_key), std::string("kiosk_a"));
    OBL_CHECK_EQ(std::string(params->m_buildings[base + 1].m_key), std::string("kiosk_b"));
    OBL_CHECK_EQ(std::string(params->m_buildings[base + 2].m_key), std::string("kiosk_c"));
    OBL_CHECK_EQ(params->m_buildings[base + 2].m_id, base + 2);

    // The other order gives the other indices.
    pack_path::SetActiveMods({"mod_b", "mod_a"});
    OBL_REQUIRE(game.Load(*params, error));
    OBL_CHECK_EQ(std::string(params->m_buildings[base].m_key), std::string("kiosk_c"));
    OBL_CHECK_EQ(std::string(params->m_buildings[base + 1].m_key), std::string("kiosk_a"));
}

OBL_TEST_CASE("catalog_patch.a_mod_record_may_use_a_record_of_another_mod")
{
    ModsGuard guard;
    Game game;
    game.Record("mod_a", "staff", "builder", "mod_builder");
    game.Patch("mod_a", {{"staff", {"mod_builder"}}});
    json operation = ReadJson(game.pack / "catalog/buildings/kiosk.json")["operation"];
    operation["staff"] = {{"types", {"mod_builder"}}, {"max", 1}};
    game.Record("mod_b", "buildings", "kiosk", "staffed_stand", {{"operation", operation}});
    game.Patch("mod_b", {{"buildings", {"staffed_stand"}}});

    pack_path::SetActiveMods({"mod_a", "mod_b"});
    auto params = std::make_unique<tables::WorldParams>();
    std::string error;
    const bool loaded = game.Load(*params, error);
    if (!loaded) std::printf("  %s\n", error.c_str());
    OBL_REQUIRE(loaded);
    const auto& stand = params->m_buildings[params->m_numBuildings - 1];
    OBL_CHECK_EQ(std::string(stand.m_key), std::string("staffed_stand"));
    OBL_CHECK_EQ(stand.m_NumStaff, 1);
    OBL_CHECK_EQ(stand.m_Staff[0], params->m_numStaff - 1);
    OBL_CHECK_EQ(std::string(params->m_staff[stand.m_Staff[0]].m_key), std::string("mod_builder"));

    // Without the staff member's mod the reference has nothing to resolve to.
    pack_path::SetActiveMods({"mod_b"});
    OBL_CHECK(!game.Load(*params, error));
    OBL_CHECK(error.find("missing staff reference") != std::string::npos);
}

OBL_TEST_CASE("catalog_patch.refuses_a_bad_patch_and_names_the_mod")
{
    ModsGuard guard;
    Game game;
    pack_path::SetActiveMods({"mod_a"});
    auto params = std::make_unique<tables::WorldParams>();
    std::string error;
    const auto refuse = [&](const char* reason) {
        OBL_CHECK(!game.Load(*params, error));
        OBL_CHECK(error.find("mod mod_a") != std::string::npos);
        OBL_CHECK(error.find(reason) != std::string::npos);
    };

    // A name the pack already lists.
    game.Record("mod_a", "buildings", "kiosk", "hut");
    game.Patch("mod_a", {{"buildings", {"hut"}}});
    refuse("already exists");

    // A name listed twice in one patch.
    game.Record("mod_a", "buildings", "kiosk", "twin");
    game.Patch("mod_a", {{"buildings", {"twin", "twin"}}});
    refuse("already exists");

    // A record with no file.
    game.Patch("mod_a", {{"buildings", {"ghost"}}});
    refuse("missing file");

    // A file under another key than the name added.
    game.Record("mod_a", "buildings", "kiosk", "other");
    fs::copy_file(game.dir.path / "mods/mod_a/catalog/buildings/other.json", game.dir.path / "mods/mod_a/catalog/buildings/named.json");
    game.Patch("mod_a", {{"buildings", {"named"}}});
    refuse("key differs");

    // The opening hours of the pack fill the engine's table already.
    game.Record("mod_a", "opening_hours", "day", "late");
    game.Patch("mod_a", {{"opening_hours", {"late"}}});
    refuse("engine capacity exceeded");

    // A malformed patch file.
    const auto patchFile = game.dir.path / "mods/mod_a/catalog/index.patch.json";
    WriteJson(patchFile, {{"format", "obl.other"}, {"version", 1}, {"add", json::object()}});
    refuse("unknown format");
    WriteJson(patchFile, {{"format", "obl.catalog_index_patch"}, {"version", 2}, {"add", json::object()}});
    refuse("unsupported version");
    WriteJson(patchFile, {{"format", "obl.catalog_index_patch"}, {"version", 1}, {"add", {{"buildings", "hut"}}}});
    refuse("not a list");
    std::ofstream(patchFile, std::ios::binary) << "{";
    refuse("invalid JSON");
}

OBL_TEST_CASE("catalog_patch.no_active_mod_leaves_the_catalog_alone")
{
    ModsGuard guard;
    Game game;
    game.Record("mod_a", "buildings", "kiosk", "kiosk_a");
    game.Patch("mod_a", {{"buildings", {"kiosk_a"}}});
    auto params = std::make_unique<tables::WorldParams>();
    std::string error;
    OBL_REQUIRE(game.Load(*params, error));
    OBL_CHECK_EQ(params->m_numBuildings, 9);
}
