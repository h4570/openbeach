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
            "{\"format\":\"obl.mod\",\"version\":1,\"id\":\"" + id + "\",\"title\":\"T\",\"mod_version\":\"1\",\"pack\":\"" + pack +
                    "\"" + (needs.empty() ? "" : ",\"requires\":[" + needs + "]") + "}");
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
