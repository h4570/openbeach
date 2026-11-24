#include "unit_test.h"
#include "temp_dir.h"
#include "formats/data_patch.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;

// A game directory with an L0-style pack holding one guest set, and two mods in load order.
struct PatchGame {
    obl::test::TempPath game{"json-patch"};
    std::filesystem::path pack = game.path / "packs" / "l0";
    std::filesystem::path logical = "guests/island.json";
    std::vector<std::string> mods{"early", "late"};
    std::vector<std::string> conflicts;
    std::string error;
    Json visitor = {{"key", "visitor"}, {"gender", "m"}, {"name", "Visitor"}, {"sprite", "dummy"}, {"money", 100}};

    PatchGame()
    {
        File(pack / logical,
                {{"format", "obl.guest_set"}, {"version", 2}, {"types", Json::array({visitor})},
                        {"names", {{"male", Json::array()}, {"female", Json::array()}, {"surnames", Json::array()}}}});
        for (const auto& mod : mods) std::filesystem::create_directories(game.path / "mods" / mod);
    }
    // The patched copies are process-wide: leave none behind for the next test.
    ~PatchGame() { obl::data_patch::Prepare(pack, {}, conflicts, error); }

    static void File(const std::filesystem::path& path, const Json& value)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << value;
    }
    // A vector, not a Json: a one-element braced list would copy the element instead of making an array.
    void Patch(const std::string& mod, const std::vector<Json>& ops)
    {
        File(game.path / "mods" / mod / "guests/island.patch.json", {{"format", "obl.json_patch"}, {"version", 1}, {"ops", Json(ops)}});
    }
    bool Prepare() { return obl::data_patch::Prepare(pack, mods, conflicts, error); }
    Json Read()
    {
        obl::data_patch::Input in(pack / logical);
        return Json::parse(in);
    }
    Json Guest(const std::string& key) const
    {
        auto guest = visitor;
        guest["key"] = key;
        return guest;
    }
    bool ErrorNames(const std::string& mod, size_t op, const std::string& path) const
    {
        return error.find("mod " + mod) != std::string::npos && error.find("island.patch.json") != std::string::npos &&
                error.find("op " + std::to_string(op) + " " + path) != std::string::npos;
    }
};

Json Op(const char* op, const char* path, const char* field, const Json& value)
{
    return {{"op", op}, {"path", path}, {field, value}};
}

Json Items(const std::vector<Json>& items)
{
    return Json(items);
}
} // namespace

OBL_TEST_CASE("data_patch.without_patches_reads_the_file_bytes")
{
    PatchGame game;
    const auto file = game.pack / game.logical;
    std::ofstream(file, std::ios::binary) << "{ \"format\" : \"obl.guest_set\",\r\n  \"kept\": 1.50 }";
    OBL_REQUIRE(game.Prepare());
    OBL_CHECK(game.conflicts.empty());
    obl::data_patch::Input in(file);
    const std::string read{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    OBL_CHECK(read == "{ \"format\" : \"obl.guest_set\",\r\n  \"kept\": 1.50 }");
    obl::data_patch::Input missing(game.pack / "rules/missing.json");
    OBL_CHECK(!missing);
}

OBL_TEST_CASE("data_patch.set_changes_and_adds_members")
{
    PatchGame game;
    game.Patch("early", {Op("set", "/types[key=visitor]/money", "value", 250), Op("set", "/types[key=visitor]/litter", "value", 0.5)});
    OBL_REQUIRE(game.Prepare());
    const auto guest = game.Read()["types"][0];
    OBL_CHECK(guest["money"] == 250);
    OBL_CHECK(guest["litter"] == 0.5);
    OBL_CHECK(guest["name"] == "Visitor");
}

OBL_TEST_CASE("data_patch.merge_keeps_members_it_does_not_name")
{
    PatchGame game;
    game.Patch("early", {Op("merge", "/types[key=visitor]", "value", Json::object({{"money", 15}, {"nights", {1, 2}}}))});
    OBL_REQUIRE(game.Prepare());
    auto expected = game.visitor;
    expected["money"] = 15;
    expected["nights"] = {1, 2};
    OBL_CHECK(game.Read()["types"][0] == expected);
}

OBL_TEST_CASE("data_patch.append_adds_at_the_end_and_keeps_positions")
{
    PatchGame game;
    game.Patch("early", {Op("append", "/types", "items", Items({game.Guest("surfer"), game.Guest("swimmer")}))});
    OBL_REQUIRE(game.Prepare());
    const auto types = game.Read()["types"];
    OBL_REQUIRE(types.size() == 3);
    OBL_CHECK(types[0] == game.visitor);
    OBL_CHECK(types[1]["key"] == "surfer" && types[2]["key"] == "swimmer");
}

OBL_TEST_CASE("data_patch.refuses_a_selector_without_a_match")
{
    PatchGame game;
    game.Patch("early", {Op("set", "/types[key=missing]/money", "value", 1)});
    OBL_CHECK(!game.Prepare());
    OBL_CHECK(game.ErrorNames("early", 0, "/types[key=missing]/money"));
    OBL_CHECK(game.error.find("no match") != std::string::npos);
}

OBL_TEST_CASE("data_patch.refuses_a_selector_matching_two_elements")
{
    PatchGame game;
    game.Patch("early", {Op("append", "/types", "items", Items({game.visitor})), Op("set", "/types[key=visitor]/money", "value", 1)});
    OBL_CHECK(!game.Prepare());
    OBL_CHECK(game.ErrorNames("early", 1, "/types[key=visitor]/money"));
    OBL_CHECK(game.error.find("ambiguous") != std::string::npos);
}

OBL_TEST_CASE("data_patch.refuses_indices_other_key_fields_and_unknown_ops")
{
    PatchGame game;
    const Json refused[] = {Op("set", "/types/0/money", "value", 1), Op("set", "/types[name=Visitor]/money", "value", 1),
            Op("set", "/types", "value", Json::array()), Op("set", "/types[key=visitor]", "value", Json::object({{"key", "visitor"}})),
            Op("remove", "/types[key=visitor]", "value", 1), Op("append", "/types[key=visitor]", "items", Items({1}))};
    for (const auto& op : refused) {
        game.Patch("late", {op});
        OBL_CHECK(!game.Prepare());
        OBL_CHECK(game.ErrorNames("late", 0, op["path"].get<std::string>()));
    }
}

OBL_TEST_CASE("data_patch.refuses_a_result_its_schema_rejects")
{
    PatchGame game;
    game.Patch("early", {Op("set", "/types[key=visitor]/litter", "value", 0.5)});
    game.Patch("late", {Op("set", "/types[key=visitor]/money", "value", "a lot")});
    OBL_CHECK(!game.Prepare());
    OBL_CHECK(game.ErrorNames("late", 0, "/types[key=visitor]/money"));
    OBL_CHECK(game.error.find("/types/0/money") != std::string::npos);
    // A refused set publishes nothing: the loaders keep reading the file as it is.
    OBL_CHECK(game.Read()["types"][0] == game.visitor);
}

OBL_TEST_CASE("data_patch.later_mod_wins_and_both_are_reported")
{
    PatchGame game;
    game.Patch("early", {Op("merge", "/types[key=visitor]", "value", Json::object({{"money", 15}}))});
    game.Patch("late", {Op("set", "/types[key=visitor]/money", "value", 25)});
    OBL_REQUIRE(game.Prepare());
    OBL_CHECK(game.Read()["types"][0]["money"] == 25);
    OBL_REQUIRE(game.conflicts.size() == 1);
    const auto& conflict = game.conflicts[0];
    OBL_CHECK(conflict.find("mod late") < conflict.find("overrides") && conflict.find("overrides") < conflict.find("mod early"));
    OBL_CHECK(conflict.find("/types[key=visitor]/money") != std::string::npos);
}

OBL_TEST_CASE("data_patch.two_appends_both_land_in_load_order")
{
    PatchGame game;
    game.Patch("early", {Op("append", "/types", "items", Items({game.Guest("surfer")}))});
    // Tuning another mod's guest builds on that mod; it is not reported as a conflict.
    game.Patch(
            "late", {Op("append", "/types", "items", Items({game.Guest("swimmer")})), Op("set", "/types[key=surfer]/money", "value", 5)});
    OBL_REQUIRE(game.Prepare());
    const auto types = game.Read()["types"];
    OBL_REQUIRE(types.size() == 3);
    OBL_CHECK(types[1]["key"] == "surfer" && types[1]["money"] == 5);
    OBL_CHECK(types[2]["key"] == "swimmer");
    OBL_CHECK(game.conflicts.empty());
}

OBL_TEST_CASE("data_patch.patches_a_whole_replacement_from_a_layer")
{
    PatchGame game;
    auto replaced = game.visitor;
    replaced["money"] = 7;
    PatchGame::File(game.game.path / "mods" / "early" / game.logical,
            {{"format", "obl.guest_set"}, {"version", 2}, {"types", Json::array({replaced})},
                    {"names", {{"male", Json::array()}, {"female", Json::array()}, {"surnames", Json::array()}}}});
    game.Patch("late", {Op("set", "/types[key=visitor]/litter", "value", 0.25)});
    OBL_REQUIRE(game.Prepare());
    obl::data_patch::Input in(game.game.path / "mods" / "early" / game.logical);
    const auto guest = Json::parse(in)["types"][0];
    OBL_CHECK(guest["money"] == 7 && guest["litter"] == 0.25);
}

OBL_TEST_CASE("data_patch.classifies_simulation_inputs")
{
    using obl::data_patch::IsSimulationDataPath;
    OBL_CHECK(IsSimulationDataPath("rules/game_parameters.json"));
    OBL_CHECK(IsSimulationDataPath("guests/l0.json"));
    OBL_CHECK(IsSimulationDataPath("sounds/sound_script.json"));
    OBL_CHECK(IsSimulationDataPath("sprites/dummy.json"));
    OBL_CHECK(!IsSimulationDataPath("gui/object_browser.json"));
    OBL_CHECK(!IsSimulationDataPath("fonts/GuiFont8b.json"));
    OBL_CHECK(!IsSimulationDataPath("sprites/palette_modifiers.json"));
}

OBL_TEST_CASE("data_patch.leaves_gui_layout_patches_to_the_gui")
{
    // gui/main_menu.patch.json patches the .gui layout (game/ui/gui_patch), so no JSON base file is looked for.
    PatchGame game;
    PatchGame::File(game.game.path / "mods" / "early" / "gui/main_menu.patch.json",
            {{"format", obl::data_patch::kGuiPatchFormat}, {"version", 1}, {"ops", Json::array()}});
    OBL_CHECK(game.Prepare());
    OBL_CHECK(game.error.empty());
    OBL_CHECK(obl::data_patch::PatchFormat(game.game.path / "mods" / "early" / "gui/main_menu.patch.json") ==
            obl::data_patch::kGuiPatchFormat);
}
