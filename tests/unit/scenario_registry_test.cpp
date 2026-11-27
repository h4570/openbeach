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
