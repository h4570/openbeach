// Mods as packages (src/formats/mod_pack.h) and the layer order of src/core/pack_path.h with mods active.

#include "unit_test.h"

#include "core/pack_path.h"
#include "formats/mod_pack.h"
#include "temp_dir.h"

#include <filesystem>
#include <fstream>

using namespace obl;

namespace {

void Write(const std::filesystem::path& file, const std::string& text)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

void MakeMod(const std::filesystem::path& game, const std::string& id, const std::string& pack = "l0", const std::string& needs = "")
{
    Write(game / "mods" / id / "mod.json",
            "{\"format\":\"obl.mod\",\"version\":1,\"id\":\"" + id + "\",\"title\":\"T\",\"mod_version\":\"1\",\"pack\":\"" + pack + "\"" +
                    (needs.empty() ? "" : ",\"requires\":[" + needs + "]") + "}");
}

// Every test leaves no mod active for the next one.
struct ModsGuard {
    ~ModsGuard() { pack_path::SetActiveMods({}); }
};

} // namespace

OBL_TEST_CASE("mod_pack.layers_run_from_the_last_mod_to_the_pack")
{
    ModsGuard guard;
    test::TempPath game("mod-layers");
    const auto pack = game.path / "packs" / "l0";
    Write(pack / "gui" / "pack_only.json", "x");
    Write(pack / "gui" / "all.json", "x");
    Write(game.path / "mod" / "gui" / "all.json", "x");
    Write(game.path / "mod" / "gui" / "legacy.json", "x");
    Write(game.path / "mods" / "a" / "gui" / "all.json", "x");
    Write(game.path / "mods" / "a" / "gui" / "a_only.json", "x");
    Write(game.path / "mods" / "b" / "gui" / "all.json", "x");

    // No active mods: the legacy layer shadows the pack, as before.
    OBL_CHECK(pack_path::Resolve(pack, "gui/all.json") == game.path / "mod" / "gui" / "all.json");
    pack_path::SetActiveMods({"a", "b"});
    // Shadowed by three layers: the last mod wins.
    OBL_CHECK(pack_path::Resolve(pack, "gui/all.json") == game.path / "mods" / "b" / "gui" / "all.json");
    OBL_CHECK(pack_path::Resolve(pack, "gui/a_only.json") == game.path / "mods" / "a" / "gui" / "a_only.json");
    OBL_CHECK(pack_path::Resolve(pack, "gui/legacy.json") == game.path / "mod" / "gui" / "legacy.json");
    OBL_CHECK(pack_path::Resolve(pack, "gui/pack_only.json") == pack / "gui" / "pack_only.json");
    OBL_CHECK(pack_path::Resolve(pack, "gui/none.json") == pack / "gui" / "none.json");
    // A mod that is not active contributes nothing.
    pack_path::SetActiveMods({"b"});
    OBL_CHECK(pack_path::Resolve(pack, "gui/a_only.json") == pack / "gui" / "a_only.json");
    pack_path::SetActiveMods({"a", "b"});
    const std::vector<std::string> expected = {"a_only", "all", "legacy", "pack_only"};
    OBL_CHECK(pack_path::ListStems(pack, "gui", ".json") == expected);
}

OBL_TEST_CASE("mod_pack.parse_mod_list")
{
    const std::vector<std::string> two = {"a", "b_2"};
    OBL_CHECK(pack_path::ParseModList(" a , b_2,,") == two);
    OBL_CHECK(pack_path::ParseModList("").empty());
}

OBL_TEST_CASE("mod_pack.check_accepts_a_fit_list_and_refuses_the_others")
{
    test::TempPath game("mod-check");
    MakeMod(game.path, "base_fix");
    MakeMod(game.path, "extra", "l0", "\"base_fix\"");
    MakeMod(game.path, "other_pack", "base");
    std::vector<mod_pack::ActiveMod> mods;
    std::string error;

    OBL_CHECK(mod_pack::CheckActive(game.path, "l0", {}, mods, error) && mods.empty());
    const std::vector<std::string> good = {"base_fix", "extra"};
    OBL_CHECK(mod_pack::CheckActive(game.path, "l0", good, mods, error));
    OBL_CHECK(mods.size() == 2 && mods[1].manifest.id == "extra" && mods[1].hash.size() == 64);

    const auto refused = [&](std::vector<std::string> list, const char* mod, const char* reason) {
        const bool ok = mod_pack::CheckActive(game.path, "l0", list, mods, error);
        OBL_CHECK(!ok);
        OBL_CHECK(error.find(mod) != std::string::npos);
        OBL_CHECK(error.find(reason) != std::string::npos);
    };
    refused({"extra"}, "extra", "not active");
    refused({"extra", "base_fix"}, "extra", "earlier");
    refused({"missing"}, "missing", "no mod.json");
    refused({"Bad"}, "Bad", "not a mod id");
    refused({"../x"}, "../x", "not a mod id");
    refused({"other_pack"}, "other_pack", "\"base\"");
    refused({"base_fix", "base_fix"}, "base_fix", "twice");

    // A manifest that names another id than its directory.
    Write(game.path / "mods" / "liar" / "mod.json",
            "{\"format\":\"obl.mod\",\"version\":1,\"id\":\"truth\",\"title\":\"T\",\"mod_version\":\"1\",\"pack\":\"l0\"}");
    refused({"liar"}, "liar", "truth");
    Write(game.path / "mods" / "broken" / "mod.json", "{\"format\":\"obl.mod\"");
    refused({"broken"}, "broken", "JSON");
}

OBL_TEST_CASE("mod_pack.hash_follows_names_and_contents")
{
    test::TempPath game("mod-hash");
    Write(game.path / "m" / "a.txt", "one");
    Write(game.path / "m" / "d" / "b.txt", "two");
    const std::string first = pack_path::ModContentHash(game.path / "m");
    OBL_CHECK(first.size() == 64);
    OBL_CHECK(pack_path::ModContentHash(game.path / "m") == first);
    Write(game.path / "m" / "d" / "b.txt", "twp");
    OBL_CHECK(pack_path::ModContentHash(game.path / "m") != first);
    Write(game.path / "m" / "d" / "b.txt", "two");
    OBL_CHECK(pack_path::ModContentHash(game.path / "m") == first);
    Write(game.path / "m" / "c.txt", "");
    OBL_CHECK(pack_path::ModContentHash(game.path / "m") != first);
}

namespace {

void WriteManifest(const std::filesystem::path& game, const std::string& id, const std::string& fields)
{
    Write(game / "mods" / id / "mod.json",
            "{\"format\":\"obl.mod\",\"version\":1,\"id\":\"" + id + "\",\"title\":\"T\",\"mod_version\":\"1\"," + fields + "}");
}

bool Accepted(const std::filesystem::path& game, const std::string& pack, const std::string& id, std::string& error)
{
    std::vector<mod_pack::ActiveMod> mods;
    const std::vector<std::string> list = {id};
    return mod_pack::CheckActive(game, pack, list, mods, error);
}

} // namespace

OBL_TEST_CASE("mod_pack.a_mod_listing_several_packs_starts_on_each")
{
    test::TempPath game("mod-packs");
    WriteManifest(game.path, "any", "\"packs\":[\"base\",\"l0\"]");
    std::string error;
    OBL_CHECK(Accepted(game.path, "base", "any", error));
    OBL_CHECK(Accepted(game.path, "l0", "any", error));
    OBL_CHECK(!Accepted(game.path, "other", "any", error));
    OBL_CHECK(error.find("\"base\", \"l0\"") != std::string::npos);

    WriteManifest(game.path, "both", "\"pack\":\"l0\",\"packs\":[\"l0\"]");
    OBL_CHECK(!Accepted(game.path, "l0", "both", error) && error.find("both") != std::string::npos);
    WriteManifest(game.path, "none", "\"scope\":\"world\"");
    OBL_CHECK(!Accepted(game.path, "l0", "none", error) && error.find("missing") != std::string::npos);
    WriteManifest(game.path, "empty", "\"packs\":[]");
    OBL_CHECK(!Accepted(game.path, "l0", "empty", error));
    WriteManifest(game.path, "badscope", "\"pack\":\"l0\",\"scope\":\"sim\"");
    OBL_CHECK(!Accepted(game.path, "l0", "badscope", error) && error.find("scope") != std::string::npos);
}

OBL_TEST_CASE("mod_pack.a_presentation_mod_ships_no_simulation_data")
{
    test::TempPath game("mod-scope");
    const std::string fields = "\"packs\":[\"l0\"],\"scope\":\"presentation\",\"api\":1";
    WriteManifest(game.path, "look", fields);
    Write(game.path / "mods/look/scripts/presentation/main.lua", "");
    Write(game.path / "mods/look/scripts/presentation/lib/util.lua", "");
    Write(game.path / "mods/look/text/en.patch.json", "{}");
    Write(game.path / "mods/look/README.md", "");
    Write(game.path / "mods/look/LICENSE", "");
    std::string error;
    OBL_CHECK(Accepted(game.path, "l0", "look", error));
    std::vector<mod_pack::ActiveMod> mods;
    const std::vector<std::string> list = {"look"};
    OBL_REQUIRE(mod_pack::CheckActive(game.path, "l0", list, mods, error));
    OBL_CHECK(mods[0].manifest.scope == mod_pack::Scope::Presentation);
    OBL_CHECK(!mod_pack::Recorded(mods[0])); // saves and replays leave it out
    OBL_CHECK(mods[0].directory == game.path / "mods" / "look");

    const auto refused = [&](const std::string& file) {
        WriteManifest(game.path, "data", fields);
        Write(game.path / "mods/data" / file, "{}");
        const bool ok = Accepted(game.path, "l0", "data", error);
        OBL_CHECK(!ok);
        OBL_CHECK(error.find(file) != std::string::npos);
        std::filesystem::remove_all(game.path / "mods/data");
    };
    refused("catalog/buildings/kiosk.json");
    refused("catalog/index.patch.json");
    refused("rules/world.json");
    refused("scenarios/1/scenario.json");
    refused("scripts/world/x.wsc");
    refused("sprites/kiosk.json");
    refused("text/en.json"); // a whole table could change existing texts
    refused("text/sub/en.patch.json");
    refused("gui/sim_list.patch.json"); // a JSON data patch of a gui/ file that is not on the presentation list

    // A GUI layout patch and a JSON data patch of a presentation file are presentation data.
    WriteManifest(game.path, "layout", fields);
    Write(game.path / "mods/layout/gui/main_menu.patch.json", R"({"format":"obl.gui_patch","version":1,"ops":[]})");
    Write(game.path / "mods/layout/gui/object_browser.patch.json", R"({"format":"obl.json_patch","version":1,"ops":[]})");
    OBL_CHECK(Accepted(game.path, "l0", "layout", error));

    // A world mod (the default scope) may ship all of it and is recorded.
    WriteManifest(game.path, "world", "\"pack\":\"l0\"");
    Write(game.path / "mods/world/catalog/buildings/kiosk.json", "{}");
    OBL_CHECK(Accepted(game.path, "l0", "world", error));
}

OBL_TEST_CASE("mod_pack.a_mod_needing_a_newer_api_is_refused")
{
    test::TempPath game("mod-api");
    WriteManifest(
            game.path, "future", "\"pack\":\"l0\",\"scope\":\"presentation\",\"api\":" + std::to_string(mod_pack::kPresentationApi + 1));
    std::string error;
    OBL_CHECK(!Accepted(game.path, "l0", "future", error));
    OBL_CHECK(error.find("future") != std::string::npos);
    OBL_CHECK(error.find("API " + std::to_string(mod_pack::kPresentationApi + 1)) != std::string::npos);

    // Scripts need the version they were written for.
    WriteManifest(game.path, "unversioned", "\"pack\":\"l0\",\"scope\":\"presentation\"");
    Write(game.path / "mods/unversioned/scripts/presentation/main.lua", "");
    OBL_CHECK(!Accepted(game.path, "l0", "unversioned", error) && error.find("api") != std::string::npos);
    WriteManifest(game.path, "zero", "\"pack\":\"l0\",\"api\":0");
    OBL_CHECK(!Accepted(game.path, "l0", "zero", error));
    WriteManifest(game.path, "current", "\"pack\":\"l0\",\"api\":1");
    Write(game.path / "mods/current/scripts/presentation/main.lua", "");
    OBL_CHECK(Accepted(game.path, "l0", "current", error));
}
