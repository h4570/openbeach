// Mods as packages (src/formats/mod_pack.h) and the layer order of src/core/pack_path.h with mods active.

#include "unit_test.h"

#include "core/key_code.h"

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

OBL_TEST_CASE("mod_pack.hash_ignores_crlf_in_text_files_only")
{
    test::TempPath game("mod-hash-crlf");
    Write(game.path / "lf" / "a.json", "{\n}\n");
    Write(game.path / "lf" / "s.wsc", "x\ny\n");
    Write(game.path / "lf" / "d.bin", "x\ny\n");
    Write(game.path / "crlf" / "a.json", "{\r\n}\r\n");
    Write(game.path / "crlf" / "s.wsc", "x\r\ny\r\n");
    Write(game.path / "crlf" / "d.bin", "x\ny\n");
    OBL_CHECK(!pack_path::ModContentHash(game.path / "lf").empty());
    OBL_CHECK(pack_path::ModContentHash(game.path / "lf") == pack_path::ModContentHash(game.path / "crlf"));
    // Binary files are hashed as they are, and a lone CR in a text file is content.
    Write(game.path / "crlf" / "d.bin", "x\r\ny\n");
    OBL_CHECK(pack_path::ModContentHash(game.path / "lf") != pack_path::ModContentHash(game.path / "crlf"));
    Write(game.path / "crlf" / "d.bin", "x\ny\n");
    Write(game.path / "crlf" / "s.wsc", "x\ry\n");
    OBL_CHECK(pack_path::ModContentHash(game.path / "lf") != pack_path::ModContentHash(game.path / "crlf"));
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

    refused("overlay/sub/icon.png");
    refused("overlay/icon.tga");
    refused("images/icon.png"); // the pack's own directory: the file would shadow a pack image

    // The overlay's images are drawn by scripts only.
    WriteManifest(game.path, "icons", fields);
    Write(game.path / "mods/icons/scripts/presentation/main.lua", "");
    Write(game.path / "mods/icons/overlay/icon.png", "");
    OBL_CHECK(Accepted(game.path, "l0", "icons", error));

    // A GUI layout patch and a JSON data patch of a presentation file are presentation data.
    WriteManifest(game.path, "layout", fields);
    Write(game.path / "mods/layout/gui/main_menu.patch.json", R"({"format":"obl.gui_patch","version":1,"ops":[]})");
    Write(game.path / "mods/layout/gui/object_browser.patch.json", R"({"format":"obl.json_patch","version":1,"ops":[]})");
    OBL_CHECK(Accepted(game.path, "l0", "layout", error));

    // A presentation mod may ship recipes that output overlay or menu screen PNGs, never sprites or other pack files.
    WriteManifest(game.path, "recipes_ok", fields);
    Write(game.path / "mods/recipes_ok/recipes/banner.json",
            R"({"format":"obl.asset_recipe","version":1,"source":"images/screens/l0_title.png","output":"overlay/banner.png","operations":[]})");
    Write(game.path / "mods/recipes_ok/recipes/title.json",
            R"({"format":"obl.asset_recipe","version":1,"source":"images/screens/l0_title.png","output":"images/screens/l0_title.png","operations":[]})");
    OBL_CHECK(Accepted(game.path, "l0", "recipes_ok", error));

    WriteManifest(game.path, "recipes_sprite", fields);
    Write(game.path / "mods/recipes_sprite/recipes/icon.json",
            R"({"format":"obl.asset_recipe","version":1,"source":"images/screens/l0_title.png","output":"sprites/icon.json","operations":[]})");
    OBL_CHECK(!Accepted(game.path, "l0", "recipes_sprite", error));
    OBL_CHECK(error.find("recipes/icon.json") != std::string::npos);

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

namespace {

// ParseManifest of a manifest whose `settings` is `list`; the reason when it is refused.
std::string SettingsError(const std::string& list, mod_pack::Manifest* out = nullptr)
{
    const std::string text =
            R"({"format":"obl.mod","version":1,"id":"m","title":"T","mod_version":"1","pack":"l0","settings":)" + list + "}";
    mod_pack::Manifest manifest;
    std::string error;
    const bool ok = mod_pack::ParseManifest(text, manifest, error);
    if (out) *out = manifest;
    return ok ? std::string() : error;
}

} // namespace

OBL_TEST_CASE("mod_pack.settings_declare_toggles_choices_and_numbers")
{
    mod_pack::Manifest m;
    const std::string list = R"([
        {"id":"wide","type":"toggle","label":"Wide","hint":"Uses the width","default":true},
        {"id":"theme","type":"choice","label":"Theme","options":["sand",{"value":"sea","label":"Deep sea"}],"default":"sea"},
        {"id":"zoom","type":"number","label":"Zoom","min":1,"max":4,"step":0.5,"default":2},
        {"id":"plain","type":"toggle","label":"Plain"}])";
    OBL_CHECK_EQ(SettingsError(list, &m), std::string());
    OBL_REQUIRE(m.settings.size() == 4);
    OBL_CHECK_EQ(m.settings[0].defaultValue, std::string("1"));
    OBL_CHECK_EQ(m.settings[0].hint, std::string("Uses the width"));
    OBL_CHECK_EQ(m.settings[1].defaultValue, std::string("sea"));
    OBL_CHECK_EQ(m.settings[1].options[0].label, std::string("sand"));
    OBL_CHECK_EQ(m.settings[1].options[1].label, std::string("Deep sea"));
    OBL_CHECK_EQ(m.settings[2].defaultValue, std::string("2"));
    OBL_CHECK_EQ(m.settings[3].defaultValue, std::string("0")); // no default: off
}

OBL_TEST_CASE("mod_pack.only_a_presentation_mod_declares_settings")
{
    test::TempPath game("mod-settings-scope");
    const std::string setting = R"("settings":[{"id":"wide","type":"toggle","label":"Wide"}])";
    std::string error;
    WriteManifest(game.path, "world_default", "\"pack\":\"l0\"," + setting);
    OBL_CHECK(!Accepted(game.path, "l0", "world_default", error));
    OBL_CHECK(error.find("world_default") != std::string::npos && error.find("settings") != std::string::npos);
    WriteManifest(game.path, "world_named", "\"pack\":\"l0\",\"scope\":\"world\"," + setting);
    OBL_CHECK(!Accepted(game.path, "l0", "world_named", error));
    WriteManifest(game.path, "shown", "\"pack\":\"l0\",\"scope\":\"presentation\"," + setting);
    OBL_CHECK(Accepted(game.path, "l0", "shown", error));
    WriteManifest(game.path, "world_plain", "\"pack\":\"l0\"");
    OBL_CHECK(Accepted(game.path, "l0", "world_plain", error));
}

OBL_TEST_CASE("mod_pack.presentation_mod_cannot_ship_object_scripts")
{
    test::TempPath game("mod-object-script-scope");
    std::string error;
    WriteManifest(game.path, "pres_obs", "\"pack\":\"l0\",\"scope\":\"presentation\"");
    Write(game.path / "mods" / "pres_obs" / "scripts" / "object" / "busker.obs", "#obl-script 1 object\n");
    OBL_CHECK(!Accepted(game.path, "l0", "pres_obs", error));
    OBL_CHECK(error.find("scripts/object/busker.obs") != std::string::npos);

    WriteManifest(game.path, "pres_vocab", "\"pack\":\"l0\",\"scope\":\"presentation\"");
    Write(game.path / "mods" / "pres_vocab" / "scripts" / "object_vocabulary.patch.json",
            R"({"format":"obl.object_script_vocabulary_patch","version":1,"states":["STATE_X"]})");
    OBL_CHECK(!Accepted(game.path, "l0", "pres_vocab", error));
    OBL_CHECK(error.find("scripts/object_vocabulary.patch.json") != std::string::npos);
}

OBL_TEST_CASE("mod_pack.settings_that_cannot_be_kept_in_an_ini_file_are_refused")
{
    const auto refused = [](const std::string& list, const std::string& part) {
        const std::string error = SettingsError(list);
        OBL_CHECK(error.find(part) != std::string::npos);
    };
    refused(R"({"id":"x"})", "not a list");
    refused(R"([{"id":"Wide","type":"toggle","label":"W"}])", "not an id");
    refused(R"([{"id":"w","type":"toggle"}])", "needs \"label\"");
    refused(R"([{"id":"w","type":"slider","label":"W"}])", "\"type\"");
    refused(R"([{"id":"w","type":"toggle","label":"W"},{"id":"w","type":"toggle","label":"W"}])", "listed twice");
    refused(R"([{"id":"c","type":"choice","label":"C","options":[]}])", "\"options\"");
    refused(R"([{"id":"c","type":"choice","label":"C","options":["a","a"]}])", "listed twice");
    refused(R"([{"id":"c","type":"choice","label":"C","options":["a;b"]}])", "ini file");
    refused(R"([{"id":"c","type":"choice","label":"C","options":["a"],"default":"b"}])", "default");
    refused(R"([{"id":"n","type":"number","label":"N","min":1,"max":1,"step":1}])", "min < max");
    refused(R"([{"id":"n","type":"number","label":"N","min":0,"max":4}])", "\"step\"");
    refused(R"([{"id":"n","type":"number","label":"N","min":0,"max":4,"step":1,"default":5}])", "default");
    refused(R"([{"id":"t","type":"toggle","label":"T","default":"maybe"}])", "default");
    // More than the limit.
    std::string many = "[";
    for (size_t i = 0; i <= mod_pack::kMaxSettings; ++i)
        many += std::string(i ? "," : "") + R"({"id":"s)" + std::to_string(i) + R"(","type":"toggle","label":"S"})";
    refused(many + "]", "at most");
}

OBL_TEST_CASE("mod_pack.a_mod_takes_over_known_defaults_with_its_scripts")
{
    test::TempPath game("mod-takes-over");
    const std::string fields = "\"pack\":\"l0\",\"scope\":\"presentation\",\"api\":1,";
    WriteManifest(game.path, "touch", fields + "\"takes_over\":[\"input.touch\",\"camera.edge_scroll\",\"input.touch\"]");
    Write(game.path / "mods/touch/scripts/presentation/main.lua", "");
    std::vector<mod_pack::ActiveMod> mods;
    std::string error;
    std::vector<std::string> list = {"touch"};
    OBL_REQUIRE(mod_pack::CheckActive(game.path, "l0", list, mods, error));
    OBL_CHECK(mods[0].manifest.takesOver == (std::vector<std::string>{"input.touch", "camera.edge_scroll"}));

    WriteManifest(game.path, "unknown", fields + "\"takes_over\":[\"input.everything\"]");
    Write(game.path / "mods/unknown/scripts/presentation/main.lua", "");
    OBL_CHECK(!Accepted(game.path, "l0", "unknown", error) && error.find("input.everything") != std::string::npos);
    WriteManifest(game.path, "not_list", fields + "\"takes_over\":\"input.touch\"");
    OBL_CHECK(!Accepted(game.path, "l0", "not_list", error) && error.find("takes_over") != std::string::npos);
    // Only a script can do what the default did.
    WriteManifest(game.path, "scriptless", fields + "\"takes_over\":[\"input.touch\"]");
    OBL_CHECK(!Accepted(game.path, "l0", "scriptless", error) && error.find("ships no scripts") != std::string::npos);

    // Two mods taking over one default: the later one in load order wins, and the conflict names both.
    WriteManifest(game.path, "later", fields + "\"takes_over\":[\"input.touch\"]");
    Write(game.path / "mods/later/scripts/presentation/main.lua", "");
    list = {"touch", "later"};
    OBL_REQUIRE(mod_pack::CheckActive(game.path, "l0", list, mods, error));
    const std::vector<std::string> conflicts = mod_pack::TakeOverConflicts(mods);
    OBL_REQUIRE(conflicts.size() == 1);
    OBL_CHECK_EQ(conflicts[0], std::string("\"input.touch\" is taken over by \"touch\", \"later\"; \"later\" wins (later in load order)"));
}

OBL_TEST_CASE("mod_pack.a_mod_declares_actions_and_bindings_and_may_take_over_an_engine_action")
{
    test::TempPath game("mod-actions");
    const std::string fields = "\"pack\":\"l0\",\"scope\":\"presentation\",\"api\":1,";
    WriteManifest(game.path, "keys",
            fields +
                    "\"actions\":[{\"id\":\"zoom_fit\",\"label\":\"Fit\",\"keys\":\"F9\"},{\"id\":\"two\",\"keys\":[\"F8\",\"F7\"]}],"
                    "\"bindings\":{\"game.pause\":\"F10\",\"panel.music\":\"none\"},\"takes_over\":[\"action.game.speed_up\"]");
    Write(game.path / "mods/keys/scripts/presentation/main.lua", "");
    std::vector<mod_pack::ActiveMod> mods;
    std::string error;
    std::vector<std::string> list = {"keys"};
    OBL_REQUIRE(mod_pack::CheckActive(game.path, "l0", list, mods, error));
    const auto& m = mods[0].manifest;
    OBL_REQUIRE(m.actions.size() == 2 && m.bindings.size() == 2);
    OBL_CHECK(m.actions[0].id == "zoom_fit" && m.actions[0].label == "Fit");
    OBL_CHECK(m.actions[0].keys == key_actions::KeyList{input::F9});
    OBL_CHECK(m.actions[1].keys == (key_actions::KeyList{input::F8, input::F7}));
    OBL_CHECK(m.takesOver == std::vector<std::string>{"action.game.speed_up"});
    OBL_CHECK(m.bindings[1].keys.empty());

    // The mods screen's source: nothing clashes here, then a second mod takes F9.
    OBL_CHECK(mod_pack::KeyConflicts(mods, nullptr).empty());
    WriteManifest(game.path, "other", fields + "\"actions\":[{\"id\":\"x\",\"keys\":\"F9\"}]");
    Write(game.path / "mods/other/scripts/presentation/main.lua", "");
    list = {"keys", "other"};
    OBL_REQUIRE(mod_pack::CheckActive(game.path, "l0", list, mods, error));
    const auto conflicts = mod_pack::KeyConflicts(mods, nullptr);
    OBL_REQUIRE(conflicts.size() == 1);
    OBL_CHECK(conflicts[0].find("\"other.x\" wins (mod \"other\")") != std::string::npos);

    const auto refused = [&](const std::string& id, const std::string& extra, const std::string& part) {
        WriteManifest(game.path, id, fields + extra);
        Write(game.path / ("mods/" + id + "/scripts/presentation/main.lua"), "");
        OBL_CHECK(!Accepted(game.path, "l0", id, error) && error.find(part) != std::string::npos);
    };
    refused("bad_key", "\"actions\":[{\"id\":\"a\",\"keys\":\"Banana\"}]", "Banana");
    refused("bad_id", "\"actions\":[{\"id\":\"A b\",\"keys\":\"F9\"}]", "lowercase");
    refused("twice", "\"actions\":[{\"id\":\"a\"},{\"id\":\"a\"}]", "listed twice");
    refused("bad_bind", "\"bindings\":{\"game.pause\":5}", "game.pause");
    refused("bad_over", "\"takes_over\":[\"action.game.nothing\"]", "action.game.nothing");
    // An action needs a script to call; a binding alone does not.
    WriteManifest(game.path, "no_script", fields + "\"actions\":[{\"id\":\"a\",\"keys\":\"F9\"}]");
    OBL_CHECK(!Accepted(game.path, "l0", "no_script", error) && error.find("no scripts") != std::string::npos);
    WriteManifest(game.path, "only_bind", "\"pack\":\"l0\",\"bindings\":{\"game.pause\":\"F10\"}");
    OBL_CHECK(Accepted(game.path, "l0", "only_bind", error));
}

OBL_TEST_CASE("mod_pack.mods_that_name_each_other_in_conflicts_never_start_together")
{
    test::TempPath game("mod-conflicts");
    const auto manifest = [&](const std::string& id, const std::string& extra) {
        Write(game.path / "mods" / id / "mod.json",
                "{\"format\":\"obl.mod\",\"version\":1,\"id\":\"" + id + "\",\"title\":\"T\",\"mod_version\":\"1\",\"pack\":\"l0\"" +
                        extra + "}");
    };
    manifest("camera", ",\"conflicts\":[\"zoom\"]");
    manifest("zoom", "");
    manifest("other", "");
    std::vector<mod_pack::ActiveMod> mods;
    std::string error;
    // Either order, and whichever of the two names the other.
    const std::vector<std::string> first = {"camera", "zoom"}, second = {"zoom", "other", "camera"};
    OBL_CHECK(!mod_pack::CheckActive(game.path, "l0", first, mods, error));
    OBL_CHECK(error.find("\"zoom\"") != std::string::npos && error.find("conflicts") != std::string::npos);
    OBL_CHECK(!mod_pack::CheckActive(game.path, "l0", second, mods, error));
    OBL_CHECK(error.find("mod \"camera\"") != std::string::npos);
    const std::vector<std::string> fine = {"camera", "other"};
    OBL_CHECK(mod_pack::CheckActive(game.path, "l0", fine, mods, error));

    mod_pack::Manifest parsed;
    const auto parse = [&](const std::string& extra) {
        return mod_pack::ParseManifest(
                "{\"format\":\"obl.mod\",\"version\":1,\"id\":\"m\",\"title\":\"T\",\"mod_version\":\"1\",\"pack\":\"l0\"" + extra + "}",
                parsed, error);
    };
    OBL_CHECK(parse(",\"conflicts\":[\"a\",\"b\"]") && parsed.conflictingMods.size() == 2);
    OBL_CHECK(!parse(",\"conflicts\":\"a\"") && error.find("conflicts") != std::string::npos);
    OBL_CHECK(!parse(",\"conflicts\":[\"m\"]") && error.find("itself") != std::string::npos);
    OBL_CHECK(!parse(",\"requires\":[\"a\"],\"conflicts\":[\"a\"]") && error.find("both") != std::string::npos);
}

OBL_TEST_CASE("mod_pack.list_installed_reads_every_mod_directory")
{
    test::TempPath game("mod-installed");
    MakeMod(game.path, "beta");
    MakeMod(game.path, "alpha", "base");
    Write(game.path / "mods" / "broken" / "mod.json", "{");
    std::filesystem::create_directories(game.path / "mods" / "empty");
    std::filesystem::create_directories(game.path / "mods" / "Not An Id");
    const auto installed = mod_pack::ListInstalled(game.path);
    OBL_CHECK(installed.size() == 4);
    if (installed.size() != 4) return;
    OBL_CHECK(installed[0].id == "alpha" && installed[0].valid && installed[0].manifest.packs[0] == "base");
    OBL_CHECK(installed[1].id == "beta" && installed[1].valid);
    OBL_CHECK(installed[2].id == "broken" && !installed[2].valid && !installed[2].error.empty());
    OBL_CHECK(installed[3].id == "empty" && !installed[3].valid && installed[3].error == "no mod.json");
    // No mods directory at all is no mod.
    test::TempPath none("mod-installed-none");
    OBL_CHECK(mod_pack::ListInstalled(none.path).empty());
}
