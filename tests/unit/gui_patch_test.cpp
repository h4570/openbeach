#include "game/ui/gui_patch.h"
#include "game/ui/own_layer.h"

#include "core/pack_path.h"
#include "temp_dir.h"
#include "unit_test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace gp = obl::gui_patch;
namespace fs = std::filesystem;

constexpr std::string_view kSampleMenu = R"(
Font(GuiFont8)
DialogItem MainMenu(0, 0, 800, 600, None, 0, 0)
{
	TGA(0, 0, 800, 600, l0_welcome.tga)
	Button(310, 314, 490, 354, l0_menu_start, None, ShowScenSel, None, L0_MENU_TIP_START)
	Button(300, 363, 480, 403, l0_menu_sandbox, None, PlaySandBox, None, L0_MENU_TIP_SANDBOX)
	Button(290, 412, 470, 452, l0_menu_load, None, LoadGameMenu, None, L0_MENU_TIP_LOAD)
	Slot(extra_buttons, 270, 560, 450, 600, column, 6)
}
)";

void WriteFile(const fs::path& path, std::string_view content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string ReadL0Gui(const char* name)
{
    const fs::path path = fs::path(OBL_REPO_DIR) / "data" / "packs" / "l0" / "gui" / name;
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    constexpr std::string_view kHeader = "#obl-gui 1";
    if (text.compare(0, kHeader.size(), kHeader) == 0) {
        size_t nl = text.find('\n');
        text = nl == std::string::npos ? std::string() : text.substr(nl + 1);
    }
    return text;
}

bool Contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

} // namespace

OBL_TEST_CASE("gui_patch.derive_ids_deterministic_on_all_l0_layouts")
{
    for (const char* file : {"main_menu.gui", "in_game.gui", "intro.gui", "game_won.gui"}) {
        const std::string text = ReadL0Gui(file);
        std::vector<std::string> first, second;
        std::string error;
        OBL_REQUIRE(gp::DeriveControlIds(text, first, error));
        OBL_REQUIRE(gp::DeriveControlIds(text, second, error));
        OBL_CHECK(first == second);
        if (std::string_view(file) == "main_menu.gui") {
            OBL_CHECK(std::find(first.begin(), first.end(), "MainMenu.Button.ShowScenSel") != first.end());
            OBL_CHECK(std::find(first.begin(), first.end(), "MainMenu.TGA.l0_welcome.tga") != first.end());
            OBL_CHECK(std::find(first.begin(), first.end(), "LoadGameMenu.Button.LoadGame") != first.end());
        }
        if (std::string_view(file) == "in_game.gui") {
            OBL_CHECK(std::find(first.begin(), first.end(), "MainItem.Button.BuildList") != first.end());
            OBL_CHECK(std::find(first.begin(), first.end(), "MainItem640.Button.BuildList") != first.end());
            OBL_CHECK(std::find(first.begin(), first.end(), "L0Building.Button.CloseBuilding") != first.end());
        }
    }
}

OBL_TEST_CASE("gui_patch.refuses_colliding_derived_ids_and_supports_explicit_id")
{
    const std::string colliding = R"(
DialogItem Dup(0, 0, 800, 600, None, 0, 0)
{
	Button(10, 10, 110, 34, btn, None, ExitDialog, None)
	Button(10, 50, 110, 74, btn, None, ExitDialog, None)
}
)";
    std::vector<std::string> ids;
    std::string error;
    OBL_CHECK(!gp::DeriveControlIds(colliding, ids, error));
    OBL_CHECK(Contains(error, "Dup.Button.ExitDialog"));

    const std::string disambiguated = R"(
DialogItem Dup(0, 0, 800, 600, None, 0, 0)
{
	Button(10, 10, 110, 34, btn, None, ExitDialog, None) id=first_exit
	Button(10, 50, 110, 74, btn, None, ExitDialog, None) id=second_exit
}
)";
    OBL_REQUIRE(gp::DeriveControlIds(disambiguated, ids, error));
    OBL_REQUIRE(ids.size() == 2u);
    OBL_CHECK_EQ(ids[0], "first_exit");
    OBL_CHECK_EQ(ids[1], "second_exit");
}

OBL_TEST_CASE("gui_patch.operations_add_hide_move_and_anchor")
{
    const std::vector<std::string> active = {"mod_a"};
    const gp::PatchSource patch{
            "mod_a",
            "mods/mod_a/gui/main_menu.patch.json",
            R"json({
                "format": "obl.gui_patch",
                "version": 1,
                "ops": [
                    { "op": "hide_control", "id": "MainMenu.Button.LoadGameMenu" },
                    { "op": "move_control", "id": "MainMenu.Button.ShowScenSel", "x": 100, "y": 200 },
                    { "op": "add_control", "dialog": "MainMenu", "at": "back", "id": "wide_bg",
                      "control": "TGA(0, 0, 1066, 600, wide_bg.tga)", "anchor": "stretch" },
                    { "op": "add_control", "dialog": "MainMenu", "slot": "extra_buttons", "id": "mod_btn",
                      "control": "Button(0, 0, 180, 40, l0_menu_start, None, Mod:mod_a.open, None, TIP_MOD)" },
                    { "op": "add_dialog",
                      "dialog": "DialogItem ModPanel(0, 0, 400, 300, l0_play_panel, 99, 1) { Button(10, 10, 110, 34, l0_play_button, CLOSE, ExitDialog, None) }",
                      "anchor": "center" },
                    { "op": "set_anchor", "dialog": "MainMenu", "anchor": "stretch" }
                ]
            })json",
    };

    gp::ApplyResult out;
    std::string error;
    OBL_REQUIRE(gp::ApplyPatches(kSampleMenu, std::span(&patch, 1), active, 1066, 600, out, error));

    // Hidden control is omitted from emitted script.
    OBL_CHECK(!Contains(out.scriptText, "LoadGameMenu"));
    // Moved control keeps its 180x40 size at (100, 200).
    OBL_CHECK(Contains(out.scriptText, "Button(100, 200, 280, 240, l0_menu_start"));
    // MainMenu stretched to 1066x600 and background TGA stretched to 1066x600 at the back (before l0_welcome.tga).
    OBL_CHECK(Contains(out.scriptText, "DialogItem MainMenu(0, 0, 1066, 600, None, 0, 0)"));
    const size_t widePos = out.scriptText.find("TGA(0, 0, 1066, 600, wide_bg.tga)");
    const size_t origPos = out.scriptText.find("TGA(0, 0, 800, 600, l0_welcome.tga)");
    OBL_CHECK(widePos != std::string::npos);
    OBL_CHECK(origPos != std::string::npos);
    OBL_CHECK(widePos < origPos);
    // Slot placed mod_btn at (270, 560, 450, 600).
    OBL_CHECK(Contains(out.scriptText, "Button(270, 560, 450, 600, l0_menu_start, None, Mod:mod_a.open"));
    // Added dialog centered at 1066x600: x0 = (1066 - 400)/2 = 333, y0 = (600 - 300)/2 = 150.
    OBL_CHECK(Contains(out.scriptText, "DialogItem ModPanel(333, 150, 733, 450, l0_play_panel, 99, 1)"));
    // Bindings contain mod_btn and wide_bg.
    const auto hasBinding = [&](std::string_view id) {
        return std::any_of(out.bindings.begin(), out.bindings.end(), [&](const gp::ControlBinding& b) { return b.id == id; });
    };
    OBL_CHECK(hasBinding("mod_btn"));
    OBL_CHECK(hasBinding("wide_bg"));
    OBL_CHECK(hasBinding("ModPanel.Button.ExitDialog"));
}

OBL_TEST_CASE("gui_patch.slot_order_across_two_mods_column_and_row")
{
    const std::string base = R"(
DialogItem Menu(0, 0, 800, 600, None, 0, 0)
{
	Slot(col_slot, 100, 200, 280, 400, column, 10)
	Slot(row_slot, 50, 500, 750, 540, row, 8)
}
)";
    const std::vector<std::string> active = {"first_mod", "second_mod"};
    const std::vector<gp::PatchSource> patches = {
            {"first_mod", "mods/first_mod/gui/main_menu.patch.json", R"json({
                "format": "obl.gui_patch", "version": 1, "ops": [
                    { "op": "add_control", "dialog": "Menu", "slot": "col_slot", "id": "c1",
                      "control": "Button(0, 0, 120, 30, b, None, Mod:first_mod.c1, None)" },
                    { "op": "add_control", "dialog": "Menu", "slot": "row_slot", "id": "r1",
                      "control": "Button(0, 0, 80, 24, b, None, Mod:first_mod.r1, None)" }
                ]
            })json"},
            {"second_mod", "mods/second_mod/gui/main_menu.patch.json", R"json({
                "format": "obl.gui_patch", "version": 1, "ops": [
                    { "op": "add_control", "dialog": "Menu", "slot": "col_slot", "id": "c2",
                      "control": "Button(0, 0, 120, 30, b, None, Mod:second_mod.c2, None)" },
                    { "op": "add_control", "dialog": "Menu", "slot": "row_slot", "id": "r2",
                      "control": "Button(0, 0, 90, 24, b, None, Mod:second_mod.r2, None)" }
                ]
            })json"},
    };

    gp::ApplyResult out;
    std::string error;
    OBL_REQUIRE(gp::ApplyPatches(base, patches, active, 800, 600, out, error));
    // Column: c1 at y=200..230, c2 at y=240..270 (30 + 10 spacing).
    OBL_CHECK(Contains(out.scriptText, "Button(100, 200, 220, 230, b, None, Mod:first_mod.c1"));
    OBL_CHECK(Contains(out.scriptText, "Button(100, 240, 220, 270, b, None, Mod:second_mod.c2"));
    // Row: r1 at x=50..130, r2 at x=138..228 (80 + 8 spacing).
    OBL_CHECK(Contains(out.scriptText, "Button(50, 500, 130, 524, b, None, Mod:first_mod.r1"));
    OBL_CHECK(Contains(out.scriptText, "Button(138, 500, 228, 524, b, None, Mod:second_mod.r2"));
}

OBL_TEST_CASE("gui_patch.anchors_at_4_3_and_16_9")
{
    const gp::Rect box{680, 540, 780, 580}; // 100x40, right margin 20, bottom margin 20 at 800x600
    // 4:3 (800x600): all anchors keep the reference coordinates.
    OBL_CHECK(gp::ApplyAnchor(box, gp::Anchor::Left, 800, 600, 800, 600) == box);
    OBL_CHECK(gp::ApplyAnchor(box, gp::Anchor::Right, 800, 600, 800, 600) == box);
    OBL_CHECK(gp::ApplyAnchor(box, gp::Anchor::BottomRight, 800, 600, 800, 600) == box);
    OBL_CHECK(gp::ApplyAnchor({0, 0, 800, 600}, gp::Anchor::Center, 800, 600, 800, 600) == (gp::Rect{0, 0, 800, 600}));
    OBL_CHECK(gp::ApplyAnchor({0, 0, 800, 600}, gp::Anchor::Stretch, 800, 600, 800, 600) == (gp::Rect{0, 0, 800, 600}));

    // 16:9 (1066x600 and 1280x720).
    OBL_CHECK(gp::ApplyAnchor(box, gp::Anchor::Right, 800, 600, 1066, 600) == (gp::Rect{946, 540, 1046, 580}));
    OBL_CHECK(gp::ApplyAnchor(box, gp::Anchor::BottomRight, 800, 600, 1280, 720) == (gp::Rect{1160, 660, 1260, 700}));
    OBL_CHECK(gp::ApplyAnchor({0, 0, 800, 600}, gp::Anchor::Center, 800, 600, 1066, 600) == (gp::Rect{133, 0, 933, 600}));
    OBL_CHECK(gp::ApplyAnchor({0, 0, 800, 600}, gp::Anchor::Center, 800, 600, 1280, 720) == (gp::Rect{240, 60, 1040, 660}));
    OBL_CHECK(gp::ApplyAnchor({0, 0, 800, 600}, gp::Anchor::Stretch, 800, 600, 1280, 720) == (gp::Rect{0, 0, 1280, 720}));
    OBL_CHECK(gp::ApplyAnchor({20, 10, 780, 590}, gp::Anchor::Stretch, 800, 600, 1066, 600) == (gp::Rect{20, 10, 1046, 590}));

    gp::Anchor parsed = gp::Anchor::None;
    OBL_CHECK(gp::ParseAnchor("centre", parsed) && parsed == gp::Anchor::Center);
    OBL_CHECK(gp::ParseAnchor("center", parsed) && parsed == gp::Anchor::Center);
    OBL_CHECK(gp::ParseAnchor("stretch", parsed) && parsed == gp::Anchor::Stretch);
    OBL_CHECK(!gp::ParseAnchor("diagonal", parsed));
}

OBL_TEST_CASE("gui_patch.errors_name_mod_file_and_operation")
{
    const std::vector<std::string> active = {"good_mod"};
    auto expectFail = [&](std::string_view jsonText, std::string_view expect1, std::string_view expect2) {
        const gp::PatchSource patch{"good_mod", "mods/good_mod/gui/main_menu.patch.json", std::string(jsonText)};
        gp::ApplyResult out;
        std::string error;
        OBL_CHECK(!gp::ApplyPatches(kSampleMenu, std::span(&patch, 1), active, 800, 600, out, error));
        OBL_CHECK(Contains(error, "good_mod"));
        OBL_CHECK(Contains(error, "main_menu.patch.json"));
        OBL_CHECK(Contains(error, expect1));
        OBL_CHECK(Contains(error, expect2));
    };

    expectFail("{bad", "invalid JSON", "good_mod");
    expectFail(R"({"format":"other","version":1,"ops":[]})", "unknown format", "obl.gui_patch");
    expectFail(R"({"format":"obl.gui_patch","version":9,"ops":[]})", "unsupported version", "1");
    expectFail(R"({"format":"obl.gui_patch","version":1,"ops":[{"op":"teleport"}]})", "op #0", "teleport");
    expectFail(
            R"json({"format":"obl.gui_patch","version":1,"ops":[{"op":"add_control","dialog":"NoSuch","control":"Button(0,0,10,10,b,None,ExitDialog,None)"}]})json",
            "add_control", "NoSuch");
    expectFail(
            R"json({"format":"obl.gui_patch","version":1,"ops":[{"op":"add_control","dialog":"MainMenu","slot":"missing_slot","control":"Button(0,0,10,10,b,None,ExitDialog,None)"}]})json",
            "add_control", "missing_slot");
    expectFail(R"json({"format":"obl.gui_patch","version":1,"ops":[{"op":"hide_control","id":"NoSuch.Control"}]})json", "hide_control",
            "NoSuch.Control");
    expectFail(R"json({"format":"obl.gui_patch","version":1,"ops":[{"op":"move_control","id":"NoSuch.Control","x":1,"y":2}]})json",
            "move_control", "NoSuch.Control");
    expectFail(R"json({"format":"obl.gui_patch","version":1,"ops":[{"op":"set_anchor","dialog":"MainMenu","anchor":"bogus"}]})json",
            "set_anchor", "bogus");
    expectFail(
            R"json({"format":"obl.gui_patch","version":1,"ops":[{"op":"add_control","dialog":"MainMenu","control":"Button(0,0,10,10,b,None,Mod:inactive_mod.ping,None)"}]})json",
            "add_control", "inactive_mod");
    expectFail(
            R"json({"format":"obl.gui_patch","version":1,"ops":[{"op":"add_dialog","dialog":"DialogItem MainMenu(0,0,100,100,None,0,0){}"}]})json",
            "add_dialog", "already exists");
}

OBL_TEST_CASE("gui_patch.no_patches_returns_base_text_byte_identical")
{
    const obl::test::TempPath tmp("obl-gui-patch-noop");
    const fs::path pack = tmp.path / "packs" / "l0";
    fs::create_directories(pack / "gui");
    const std::vector<std::string> savedMods = obl::pack_path::ActiveMods();
    obl::pack_path::SetActiveMods({});

    const std::string base = ReadL0Gui("main_menu.gui");
    gp::ApplyResult out;
    std::string error;
    OBL_REQUIRE(gp::ApplyPackPatches(pack, "main_menu", base, {}, 800, 600, out, error));
    OBL_CHECK_EQ(out.scriptText, base);
    OBL_CHECK(out.bindings.empty());

    // Now add a mod with a patch and verify load-order discovery works.
    WriteFile(tmp.path / "mods" / "alpha" / "gui" / "main_menu.patch.json",
            R"({"format":"obl.gui_patch","version":1,"ops":[{"op":"move_control","id":"MainMenu.Button.ShowScenSel","x":12,"y":34}]})");
    WriteFile(tmp.path / "mods" / "beta" / "gui" / "main_menu.patch.json",
            R"({"format":"obl.gui_patch","version":1,"ops":[{"op":"move_control","id":"MainMenu.Button.ShowScenSel","x":56,"y":78}]})");
    obl::pack_path::SetActiveMods({"alpha", "beta"});

    OBL_REQUIRE(gp::ApplyPackPatches(pack, "main_menu", base, obl::pack_path::ActiveMods(), 800, 600, out, error));
    // Later mod ("beta") wins on move_control of the same control.
    OBL_CHECK(Contains(out.scriptText, "Button(56, 78, 236, 118, l0_menu_start"));

    obl::pack_path::SetActiveMods(savedMods);
}

OBL_TEST_CASE("gui_patch.own_layer_adds_mods_button_and_screen_under_mods")
{
    const auto own = obl::own_layer::GuiPatches("main_menu");
    OBL_REQUIRE(own.size() == 1);
    OBL_CHECK(obl::own_layer::GuiPatches("play_gui").empty());

    gp::ApplyResult out;
    std::string error;
    OBL_REQUIRE(gp::ApplyPatches(kSampleMenu, own, {}, 800, 600, out, error));
    OBL_CHECK(Contains(out.scriptText, "OwnButton(290, 0, 530, 38, OBL_MENU_MODS, ModsScreen)"));
    OBL_CHECK(Contains(out.scriptText, "DialogItem ModsScreen"));
    OBL_CHECK(Contains(out.scriptText, "ModsPanel(0, 0, 800, 600)"));

    // A mod sitting above the own layer can move the Mods button by its id.
    const std::vector<gp::PatchSource> both = {
            own[0],
            {"restyle", "main_menu.patch.json",
                    R"({"format":"obl.gui_patch","version":1,"ops":[{"op":"move_control","id":"mods_button","x":10,"y":20}]})"},
    };
    const std::vector<std::string> active = {"restyle"};
    OBL_REQUIRE(gp::ApplyPatches(kSampleMenu, both, active, 800, 600, out, error));
    OBL_CHECK(Contains(out.scriptText, "OwnButton(10, 20, 250, 58, OBL_MENU_MODS, ModsScreen)"));
}
