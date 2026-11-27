#include "core/pack_path.h"
#include "formats/mod_pack.h"
#include "game/game/scenario_progress.h"
#include "game/world/scenario_registry.h"
#include "unit_test.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
using Json = nlohmann::json;
namespace sl = obl::scenario_load;

struct TempPack {
    std::filesystem::path root;
    TempPack()
        : root(std::filesystem::temp_directory_path() /
                  ("obl-registry-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(root / "scenarios");
    }
    ~TempPack() { std::filesystem::remove_all(root); }

    void Write(const std::filesystem::path& logical, const Json& document) const
    {
        const auto path = root / logical;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << document.dump(2);
    }

    void WriteScenario(const std::string& id, std::string_view kind, Json scenario) const
    {
        scenario["id"] = id;
        scenario["kind"] = kind;
        Write(std::filesystem::path("scenarios") / id / "scenario.json", scenario);
    }
};

Json L0Scenario()
{
    std::ifstream in(std::filesystem::path(OBL_REPO_DIR) / "data/packs/l0/scenarios/s1/scenario.json");
    return Json::parse(in);
}
} // namespace

OBL_TEST_CASE("scenario_registry.loads_from_campaign_and_resolves_ids")
{
    TempPack pack;
    const Json campaign = {
            {"format", "obl.campaign"},
            {"version", 1},
            {"unlock", "sequential"},
            {"sandbox_unlock", "campaign_completed_or_always"},
            {"campaign", {{{"scenario", "s1"}}, {{"scenario", "s2"}}, {{"scenario", "s3"}}}},
            {"sandboxes", {{{"scenario", "sandbox1"}}, {{"scenario", "sandbox2"}}, {{"scenario", "sandbox3"}}}},
            {"initially_available", {"s1", "s2", "s3"}},
            {"always_available", {"s1", "s2"}},
    };
    pack.Write("scenarios/campaign.json", campaign);
    const auto scenario = L0Scenario();
    for (int i = 1; i <= 3; ++i) {
        pack.WriteScenario(sl::ScenarioId(i, false), "campaign", scenario);
        pack.WriteScenario(sl::ScenarioId(i, true), "sandbox", scenario);
    }

    sl::ScenarioRegistry registry;
    std::string error;
    OBL_REQUIRE(sl::ScenarioRegistry::LoadFromPack(pack.root, registry, error));
    OBL_CHECK_EQ(registry.CampaignCount(), 3);
    OBL_CHECK_EQ(registry.SandboxCount(), 3);
    OBL_CHECK_EQ(registry.ListCount(), 6);

    const auto s3 = registry.FindById("s3");
    OBL_REQUIRE(s3.has_value());
    OBL_CHECK(s3->IsValid());
    OBL_CHECK_EQ(registry.Get(*s3).id, "s3");
    OBL_CHECK_EQ(registry.Get(*s3).menuSlot, 2);
    OBL_CHECK(!registry.Get(*s3).IsSandbox());
    const auto& large = std::get<sl::SpritePicture>(registry.Get(*s3).pictures.large);
    OBL_CHECK_EQ(large.sprite, "ui3_fe_is_pics_large");
    OBL_CHECK_EQ(large.frame, 2);
    OBL_CHECK_EQ(registry.Get(*s3).textKeys.title, "MAINSS_SCENARIO3_TITLE");
    OBL_CHECK_EQ(registry.Get(*s3).textKeys.description, "MAINSS_SCENARIO3_DESC");
    OBL_CHECK(registry.ListName(5) == u"Scenario3");

    const auto sb2 = registry.FindById("sandbox2", sl::ScenarioKind::Sandbox);
    OBL_REQUIRE(sb2.has_value());
    OBL_CHECK_EQ(registry.Get(*sb2).id, "sandbox2");
    OBL_CHECK_EQ(registry.Get(*sb2).menuSlot, 1);
    OBL_CHECK(registry.Get(*sb2).IsSandbox());
    OBL_CHECK_EQ(registry.Get(*sb2).textKeys.title, "MAINSS_SANDBOX2_TITLE");
    OBL_CHECK_EQ(registry.Get(*sb2).textKeys.description, "MAINSS_SANDBOX2_DESC");
    for (const auto* picture : {&registry.Get(*sb2).pictures.load, &registry.Get(*sb2).pictures.save}) {
        const auto& sprite = std::get<sl::SpritePicture>(*picture);
        OBL_CHECK_EQ(sprite.sprite, "ui3_fe_is_pics_small");
        OBL_CHECK_EQ(sprite.frame, 1);
    }

    const auto bySlot = registry.FindByMenuSlot(2, sl::ScenarioKind::Campaign);
    OBL_REQUIRE(bySlot.has_value());
    OBL_CHECK(*bySlot == *s3);

    OBL_CHECK(!registry.FindById("does_not_exist").has_value());
    OBL_CHECK(!registry.FindById("s3", sl::ScenarioKind::Sandbox).has_value());
    OBL_CHECK(!registry.FindByMenuSlot(3, sl::ScenarioKind::Campaign).has_value());
    OBL_CHECK(registry.Find(sl::ScenarioRef{}) == nullptr);
}

OBL_TEST_CASE("scenario_registry.reads_presentation_and_script_for_a_named_scenario")
{
    TempPack pack;
    const std::string id = "l0_sample_reef";
    pack.Write("scenarios/campaign.json",
            {{"format", "obl.campaign"}, {"version", 1}, {"unlock", "sequential"}, {"campaign", {{{"scenario", id}}}},
                    {"sandboxes", {{{"scenario", "s1"}}}}});
    const auto scenarioDir = pack.root / "scenarios" / id;
    auto scenario = L0Scenario();
    scenario["menu_texts"] = {{"title", "REEF_MENU_TITLE"}, {"description", "REEF_MENU_DESC"}};
    scenario["pictures"] = {{"large", {{"sprite", "reef_sprites"}, {"frame", 8}}}, {"load", "images/screens/reef.png"},
            {"save", {{"sprite", "reef_save"}, {"frame", 4}}}};
    scenario["script"] = "events/tide.wsc";
    pack.WriteScenario(id, "campaign", scenario);
    pack.WriteScenario("s1", "sandbox", L0Scenario());

    const auto map = std::filesystem::path("maps/scenario1/map.json");
    std::filesystem::create_directories((pack.root / map).parent_path());
    std::filesystem::copy_file(std::filesystem::path(OBL_REPO_DIR) / "data/packs/l0" / map, pack.root / map);
    std::filesystem::create_directories(scenarioDir / "events");
    std::ofstream(scenarioDir / "events/tide.wsc") << "#obl-script 1 world\ncustom script\n";
    std::ofstream(scenarioDir / "world.wsc") << "#obl-script 1 world\nstandard script\n";

    sl::ScenarioRegistry registry;
    std::string error;
    OBL_REQUIRE(sl::ScenarioRegistry::LoadFromPack(pack.root, registry, error));
    const auto ref = registry.FindByStartName(id);
    OBL_REQUIRE(ref);
    const auto& entry = registry.Get(*ref);
    OBL_CHECK_EQ(entry.textKeys.title, "REEF_MENU_TITLE");
    OBL_CHECK_EQ(entry.textKeys.description, "REEF_MENU_DESC");
    const auto& large = std::get<sl::SpritePicture>(entry.pictures.large);
    OBL_CHECK_EQ(large.sprite, "reef_sprites");
    OBL_CHECK_EQ(large.frame, 8);
    OBL_CHECK_EQ(std::get<std::string>(entry.pictures.load), "images/screens/reef.png");
    const auto& save = std::get<sl::SpritePicture>(entry.pictures.save);
    OBL_CHECK_EQ(save.sprite, "reef_save");
    OBL_CHECK_EQ(save.frame, 4);

    sl::Setup setup;
    std::string text;
    OBL_REQUIRE(sl::LoadPack(pack.root, id, setup, error));
    OBL_CHECK(setup.hostScript.empty());
    OBL_CHECK_EQ(setup.script, "scenarios/l0_sample_reef/events/tide.wsc");
    OBL_REQUIRE(sl::ReadWorldScript(pack.root, setup.script, text, error));
    OBL_CHECK(text.starts_with("custom script"));

    scenario.erase("script");
    pack.WriteScenario(id, "campaign", scenario);
    OBL_REQUIRE(sl::LoadPack(pack.root, id, setup, error));
    OBL_CHECK_EQ(setup.script, "scenarios/l0_sample_reef/world.wsc");
    OBL_REQUIRE(sl::ReadWorldScript(pack.root, setup.script, text, error));
    OBL_CHECK(text.starts_with("standard script"));
}

OBL_TEST_CASE("scenario_registry.rejects_missing_campaign_file")
{
    TempPack pack;
    sl::ScenarioRegistry registry;
    std::string error;
    OBL_CHECK(!sl::ScenarioRegistry::LoadFromPack(pack.root, registry, error));
    OBL_CHECK(!error.empty());
}

OBL_TEST_CASE("scenario_registry.resolves_start_names")
{
    sl::Campaign campaign;
    campaign.campaign = {"s1", "s2", "lagoon"};
    campaign.sandboxes = {"s1", "s2", "dunes"};
    const auto registry = sl::ScenarioRegistry::FromCampaign(campaign);

    const auto name = [&](std::string_view text) { return registry.FindByStartName(text); };
    OBL_CHECK(name("s2") == registry.FindByMenuSlot(1, sl::ScenarioKind::Campaign));
    OBL_CHECK(name("2") == name("s2"));
    OBL_CHECK(name("sandbox3") == registry.FindByMenuSlot(2, sl::ScenarioKind::Sandbox));
    OBL_CHECK(name("sandbox3") != name("3"));
    OBL_CHECK(registry.Get(*name("lagoon")).id == "lagoon");
    OBL_CHECK(!registry.Get(*name("lagoon")).IsSandbox());
    OBL_CHECK(registry.Get(*name("dunes")).IsSandbox());
    // Unknown ids and malformed or out-of-range numbers resolve to nothing.
    for (const char* bad : {"", "s", "s0", "s01", "s-1", "s1x", "s4", "4", "sandbox", "sandbox0", "sandbox4", "S1", "x1", "mesa", "0", "01",
                 "+1", "-1", "scenario1"})
        OBL_CHECK(!name(bad).has_value());
}

namespace {
struct ActiveModsGuard {
    std::filesystem::path gameDir;
    std::filesystem::path packDir;
    ActiveModsGuard()
        : gameDir(std::filesystem::temp_directory_path() /
                  ("obl-sets-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          packDir(gameDir / "packs" / "l0")
    {
        std::filesystem::create_directories(gameDir / "mods");
        std::filesystem::create_directories(packDir.parent_path());
        std::filesystem::copy(std::filesystem::path(OBL_REPO_DIR) / "data/packs/l0", packDir, std::filesystem::copy_options::recursive);
    }
    ~ActiveModsGuard()
    {
        std::string ignored;
        obl::mod_pack::Activate(gameDir, "l0", "", ignored);
        std::filesystem::remove_all(gameDir);
    }
    void CopyMod(const std::filesystem::path& source, const std::string& id) const
    {
        std::filesystem::copy(source, gameDir / "mods" / id, std::filesystem::copy_options::recursive);
    }
    bool Activate(std::string_view list, std::string& error) const { return obl::mod_pack::Activate(gameDir, "l0", list, error); }
};
} // namespace

OBL_TEST_CASE("scenario_registry.loads_multiple_mod_scenario_sets_and_progress")
{
    ActiveModsGuard env;
    const auto modsRoot = std::filesystem::path(OBL_REPO_DIR) / "tests/mods";
    env.CopyMod(modsRoot / "l0_scenarios_a", "l0_scenarios_a");
    env.CopyMod(modsRoot / "l0_scenarios_b", "l0_scenarios_b");
    env.CopyMod(modsRoot / "l0_scenarios_c", "l0_scenarios_c");

    std::string error;
    OBL_REQUIRE(env.Activate("l0_scenarios_a,l0_scenarios_b,l0_scenarios_c", error));

    sl::ScenarioRegistry registry;
    OBL_REQUIRE(sl::ScenarioRegistry::LoadFromPack(env.packDir, registry, error));
    OBL_CHECK_EQ(registry.CampaignCount(), 1);
    OBL_CHECK_EQ(registry.SandboxCount(), 1);
    OBL_CHECK_EQ(registry.Sets().size(), 3u);
    OBL_CHECK_EQ(registry.Entries().size(), 7u);

    // Pack entries stay at the front; extra entries follow in mod load order then set order with menuSlot = -1.
    OBL_CHECK_EQ(registry.Entries()[0].id, "s1");
    OBL_CHECK_EQ(registry.Entries()[0].menuSlot, 0);
    OBL_CHECK_EQ(registry.Entries()[1].id, "s1");
    OBL_CHECK_EQ(registry.Entries()[1].menuSlot, 0);
    OBL_CHECK_EQ(registry.Entries()[2].id, "l0_scenarios_a_first");
    OBL_CHECK_EQ(registry.Entries()[2].menuSlot, -1);
    OBL_CHECK_EQ(registry.Entries()[3].id, "l0_scenarios_a_second");
    OBL_CHECK_EQ(registry.Entries()[4].id, "l0_scenarios_b_locked");
    OBL_CHECK_EQ(registry.Entries()[5].id, "l0_scenarios_b_free");
    OBL_CHECK(registry.Entries()[5].IsSandbox());
    OBL_CHECK_EQ(registry.Entries()[6].id, "l0_scenarios_c_pic");
    OBL_CHECK_EQ(std::get<std::string>(registry.Entries()[6].pictures.large), "scenarios/l0_scenarios_c_pic/preview.png");

    const auto refA1 = registry.FindByStartName("l0_scenarios_a_first");
    const auto refA2 = registry.FindByStartName("l0_scenarios_a_second");
    const auto refB1 = registry.FindByStartName("l0_scenarios_b_locked");
    const auto refBFree = registry.FindByStartName("l0_scenarios_b_free");
    const auto refC = registry.FindByStartName("l0_scenarios_c_pic");
    const auto refS1 = registry.FindById("s1", sl::ScenarioKind::Campaign);
    OBL_REQUIRE(refA1 && refA2 && refB1 && refBFree && refC && refS1);

    obl::scenario_progress::ScenarioProgress progress;
    const int32_t initial[] = {0};
    const int32_t always[] = {0};
    progress.Reset(initial, always);

    // Sequential unlock in set A, requires_completed in set B, unlock: all in set C.
    OBL_CHECK(progress.IsSelectable(registry, *refA1, false));
    OBL_CHECK(!progress.IsSelectable(registry, *refA2, false));
    OBL_CHECK(!progress.IsSelectable(registry, *refB1, false));
    OBL_CHECK(progress.IsSelectable(registry, *refC, false));

    // Completing A1 opens A2 and writes "extra" to progress.json.
    progress.Complete(registry, *refA1, registry.CampaignCount());
    OBL_CHECK(progress.IsComplete(registry, *refA1));
    OBL_CHECK(progress.IsSelectable(registry, *refA2, false));
    const std::string saved = progress.ToJson();
    OBL_CHECK(saved.find("\"extra\"") != std::string::npos);
    OBL_CHECK(saved.find("\"l0_scenarios_a_first\"") != std::string::npos);
    OBL_CHECK(saved.find("\"l0_scenarios_a_second\"") != std::string::npos);

    // Completing s1 satisfies requires_completed for set B.
    progress.Complete(registry, *refS1, registry.CampaignCount());
    OBL_CHECK(progress.IsSelectable(registry, *refB1, false));
    OBL_CHECK(progress.IsSelectable(registry, *refBFree, true));

    // Inactive mod entries in progress.json are preserved across round-trip.
    auto doc = Json::parse(progress.ToJson());
    doc["extra"]["inactive_mod_scen"] = 10;
    obl::scenario_progress::ScenarioProgress reloaded;
    OBL_REQUIRE(reloaded.FromJson(doc.dump()));
    OBL_CHECK(reloaded.ToJson().find("\"inactive_mod_scen\": 10") != std::string::npos);

    // UnlockAll and Reset cover extras.
    reloaded.Reset(initial, always);
    OBL_CHECK(!reloaded.IsSelectable(registry, *refA2, false));
    reloaded.UnlockAll(registry, false, registry.CampaignCount());
    OBL_CHECK(reloaded.IsSelectable(registry, *refA2, false));
}

OBL_TEST_CASE("scenario_registry.refuses_broken_scenario_sets")
{
    const auto brokenRoot = std::filesystem::path(OBL_REPO_DIR) / "tests/mods/broken";
    struct Case {
        const char* mod;
        const char* expectedSubstring;
    };
    for (const Case& item : {
                 Case{"bad_scenario_set_prefix", "must start with"},
                 Case{"bad_scenario_set_duplicate", "duplicate scenario id"},
                 Case{"bad_scenario_set_two_sets", "duplicate scenario id"},
                 Case{"bad_scenario_set_map", "available maps: scenario1"},
         }) {
        ActiveModsGuard env;
        env.CopyMod(brokenRoot / item.mod, item.mod);
        std::string error;
        OBL_REQUIRE(env.Activate(item.mod, error));
        sl::ScenarioRegistry registry;
        OBL_CHECK(!sl::ScenarioRegistry::LoadFromPack(env.packDir, registry, error));
        OBL_CHECK(error.find(item.mod) != std::string::npos);
        OBL_CHECK(error.find(item.expectedSubstring) != std::string::npos);
    }
}
