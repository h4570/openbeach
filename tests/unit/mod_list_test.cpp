// What the mods screen edits (src/game/mods/mod_list.h): switching mods on and off, their order, why a mod cannot start and the
// conflicts of the list.

#include "unit_test.h"

#include "core/key_code.h"
#include "game/mods/mod_list.h"

#include <string>
#include <vector>

using namespace obl;

namespace {

mod_pack::InstalledMod Mod(const std::string& id, std::vector<std::string> needs = {}, std::vector<std::string> conflicts = {},
        std::vector<std::string> packs = {"l0"}, int api = 0)
{
    mod_pack::InstalledMod mod;
    mod.id = id;
    mod.valid = true;
    mod.manifest.id = id;
    mod.manifest.title = "Title of " + id;
    mod.manifest.packs = std::move(packs);
    mod.manifest.requiredMods = std::move(needs);
    mod.manifest.conflictingMods = std::move(conflicts);
    mod.manifest.api = api;
    return mod;
}

std::string Order(const mods::ModList& list)
{
    std::string text;
    for (const auto& entry : list.Entries()) text += (text.empty() ? "" : " ") + entry.mod.id + (entry.on ? "+" : "");
    return text;
}

} // namespace

OBL_TEST_CASE("mod_list.active_mods_come_first_in_load_order")
{
    const std::vector<std::string> active = {"c", "missing", "a"};
    mods::ModList list({Mod("a"), Mod("b"), Mod("c"), Mod("d")}, "l0", active, 1);
    // A listed mod that is not installed is dropped; the others follow by id.
    OBL_CHECK(Order(list) == "c+ a+ b d");
    OBL_CHECK(list.ActiveIds() == std::vector<std::string>({"c", "a"}));

    mods::Problem why{};
    std::string detail;
    OBL_CHECK(list.SetOn("d", true, why, detail));
    OBL_CHECK(Order(list) == "c+ a+ d+ b");
    OBL_CHECK(list.SetOn("c", false, why, detail));
    OBL_CHECK(Order(list) == "a+ d+ b c");
    OBL_CHECK(list.Move("d", -1) && Order(list) == "d+ a+ b c");
    OBL_CHECK(!list.Move("d", -1));
    OBL_CHECK(!list.Move("a", 1)); // the last active mod
    OBL_CHECK(!list.Move("b", -1)); // not active
}

OBL_TEST_CASE("mod_list.a_mod_that_cannot_start_says_why")
{
    mod_pack::InstalledMod broken;
    broken.id = "broken";
    broken.error = "mod.json is not a JSON object";
    mods::ModList list(
            {Mod("base_only", {}, {}, {"base"}), broken, Mod("future", {}, {}, {"l0"}, 2), Mod("needs", {"lib"}), Mod("lib")}, "l0", {}, 1);
    mods::Problem why{};
    std::string detail;
    OBL_CHECK(!list.SetOn("base_only", true, why, detail) && why == mods::Problem::WrongPack && detail == "base");
    OBL_CHECK(!list.SetOn("broken", true, why, detail) && why == mods::Problem::Invalid && detail == broken.error);
    OBL_CHECK(!list.SetOn("future", true, why, detail) && why == mods::Problem::NeedsApi && detail == "2");
    OBL_CHECK(list.ActiveCount() == 0);

    // A missing requirement does not keep a mod off, but the list cannot start until it is fixed.
    OBL_CHECK(list.SetOn("needs", true, why, detail));
    OBL_CHECK(list.Find("needs")->problem == mods::Problem::MissingRequirement && list.Find("needs")->detail == "lib");
    OBL_CHECK(!list.CanStart());
    OBL_CHECK(list.SetOn("lib", true, why, detail));
    OBL_CHECK(list.Find("needs")->problem == mods::Problem::RequirementLater);
    OBL_CHECK(list.Move("lib", -1));
    OBL_CHECK(list.Find("needs")->problem == mods::Problem::None && list.CanStart());
}

OBL_TEST_CASE("mod_list.mods_naming_each_other_in_conflicts_are_never_both_on")
{
    const std::vector<std::string> active = {"camera"};
    mods::ModList list({Mod("camera", {}, {"zoom"}), Mod("zoom"), Mod("other")}, "l0", active, 1);
    mods::Problem why{};
    std::string detail;
    // The mod that is off shows the reason before the player tries.
    OBL_CHECK(list.Find("zoom")->problem == mods::Problem::ConflictsWith && list.Find("zoom")->detail == "camera");
    OBL_CHECK(!list.SetOn("zoom", true, why, detail) && why == mods::Problem::ConflictsWith && detail == "camera");
    OBL_CHECK(list.SetOn("other", true, why, detail));
    OBL_CHECK(list.SetOn("camera", false, why, detail));
    OBL_CHECK(list.Find("zoom")->problem == mods::Problem::None);
    OBL_CHECK(list.SetOn("zoom", true, why, detail));
    OBL_CHECK(!list.SetOn("camera", true, why, detail) && detail == "zoom");
}

OBL_TEST_CASE("mod_list.conflicts_follow_the_order_and_every_source")
{
    const std::vector<std::string> active = {"early", "late"};
    mods::ModList list({Mod("early"), Mod("late"), Mod("solo")}, "l0", active, 1);
    int asked = 0;
    // A stand-in for the data patches: the later of `early` and `late` wins `/cash`.
    list.AddConflictSource([&](std::span<const std::string> ids) {
        ++asked;
        std::vector<mods::Conflict> found;
        std::vector<std::string> both;
        for (const auto& id : ids)
            if (id == "early" || id == "late") both.push_back(id);
        if (both.size() == 2) found.push_back({mods::ConflictKind::Value, both[1], both[0], "rules/game_parameters.json /cash"});
        return found;
    });
    OBL_CHECK(asked == 1);
    OBL_CHECK(list.Conflicts().size() == 1 && list.Conflicts()[0].winner == "late" && list.Conflicts()[0].loser == "early");
    OBL_CHECK(list.Move("late", -1));
    OBL_CHECK(list.Conflicts().size() == 1 && list.Conflicts()[0].winner == "early");
    mods::Problem why{};
    std::string detail;
    OBL_CHECK(list.SetOn("early", false, why, detail));
    OBL_CHECK(list.Conflicts().empty());
    // A second source adds its own conflicts after the first one's.
    OBL_CHECK(list.SetOn("early", true, why, detail));
    list.AddConflictSource([](std::span<const std::string> ids) {
        return std::vector<mods::Conflict>{{mods::ConflictKind::Value, ids.back(), ids.front(), "second source"}};
    });
    OBL_CHECK(list.Conflicts().size() == 2 && list.Conflicts()[1].what == "second source");
}

OBL_TEST_CASE("mod_list.take_over_conflicts_name_the_winner_and_follow_order")
{
    auto first = Mod("touch_a");
    first.manifest.scope = mod_pack::Scope::Presentation;
    first.manifest.api = 1;
    first.manifest.takesOver = {"input.touch", "action.game.speed_up"};
    auto second = Mod("touch_b");
    second.manifest.scope = mod_pack::Scope::Presentation;
    second.manifest.api = 1;
    second.manifest.takesOver = {"input.touch", "camera.edge_scroll", "action.game.speed_up"};
    const std::vector<std::string> active = {"touch_a", "touch_b"};
    mods::ModList list({first, second}, "l0", active, 1);
    list.AddConflictSource([&](std::span<const std::string> ids) { return mods::FindTakeOverConflicts(list, ids); });
    OBL_REQUIRE(list.Conflicts().size() == 2);
    OBL_CHECK(list.Conflicts()[0].kind == mods::ConflictKind::TakeOver);
    OBL_CHECK(list.Conflicts()[0].winner == "touch_b" && list.Conflicts()[0].loser == "touch_a");
    OBL_CHECK(list.Conflicts()[0].what == "input.touch");
    OBL_CHECK(list.Conflicts()[1].what == "action.game.speed_up");
    OBL_CHECK(list.Move("touch_b", -1));
    OBL_REQUIRE(list.Conflicts().size() == 2);
    OBL_CHECK(list.Conflicts()[0].winner == "touch_a" && list.Conflicts()[0].loser == "touch_b");
}

OBL_TEST_CASE("mod_list.key_conflicts_follow_load_order_and_player_overrides")
{
    auto first = Mod("keys_a");
    first.manifest.scope = mod_pack::Scope::Presentation;
    first.manifest.api = 1;
    first.manifest.actions = {{"fit", "Fit", {input::F9}}};
    auto second = Mod("keys_b");
    second.manifest.scope = mod_pack::Scope::Presentation;
    second.manifest.api = 1;
    second.manifest.actions = {{"snap", "Snap", {input::F9}}};
    const std::vector<std::string> active = {"keys_a", "keys_b"};
    mods::ModList list({first, second}, "l0", active, 1);
    list.AddConflictSource([&](std::span<const std::string> ids) { return mods::FindKeyConflicts(list, ids); });
    OBL_REQUIRE(list.Conflicts().size() == 1);
    OBL_CHECK(list.Conflicts()[0].kind == mods::ConflictKind::Key);
    OBL_CHECK(list.Conflicts()[0].what.find("\"keys_b.snap\" wins (mod \"keys_b\")") != std::string::npos);

    // Moving keys_b earlier flips the winner.
    OBL_CHECK(list.Move("keys_b", -1));
    OBL_REQUIRE(list.Conflicts().size() == 1);
    OBL_CHECK(list.Conflicts()[0].what.find("\"keys_a.fit\" wins (mod \"keys_a\")") != std::string::npos);

    // A single active mod can still conflict with an engine default key (P is game.pause).
    auto solo = Mod("pause_clash");
    solo.manifest.scope = mod_pack::Scope::Presentation;
    solo.manifest.api = 1;
    solo.manifest.actions = {{"ping", "Ping", {input::P}}};
    const std::vector<std::string> one = {"pause_clash"};
    mods::ModList single({solo}, "l0", one, 1);
    single.AddConflictSource([&](std::span<const std::string> ids) { return mods::FindKeyConflicts(single, ids); });
    OBL_REQUIRE(single.Conflicts().size() == 1);
    OBL_CHECK(single.Conflicts()[0].kind == mods::ConflictKind::Key);
    OBL_CHECK(single.Conflicts()[0].what.find("\"pause_clash.ping\" wins") != std::string::npos);

    // A player `[Keys]` override beats every mod.
    mods::ModList withPlayer({solo}, "l0", one, 1);
    withPlayer.AddConflictSource([&](std::span<const std::string> ids) {
        return mods::FindKeyConflicts(withPlayer, ids, [](std::string_view name) -> std::optional<std::string> {
            if (name == "game.pause") return std::string("P");
            return std::nullopt;
        });
    });
    OBL_REQUIRE(withPlayer.Conflicts().size() == 1);
    OBL_CHECK(withPlayer.Conflicts()[0].what.find("\"game.pause\" wins (the player's binding)") != std::string::npos);
}
