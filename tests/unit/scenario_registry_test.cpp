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
};
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
    {
        std::ofstream out(pack.root / "scenarios" / "campaign.json");
        out << campaign.dump(2);
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
    OBL_CHECK_EQ(registry.Get(*s3).pictures.frame, 2);
    OBL_CHECK_EQ(registry.Get(*s3).textKeys.title, "MAINSS_SCENARIO3_TITLE");

    const auto sb2 = registry.FindById("sandbox2", sl::ScenarioKind::Sandbox);
    OBL_REQUIRE(sb2.has_value());
    OBL_CHECK_EQ(registry.Get(*sb2).id, "sandbox2");
    OBL_CHECK_EQ(registry.Get(*sb2).menuSlot, 1);
    OBL_CHECK(registry.Get(*sb2).IsSandbox());
    OBL_CHECK_EQ(registry.Get(*sb2).textKeys.title, "MAINSS_SANDBOX2_TITLE");

    const auto bySlot = registry.FindByMenuSlot(2, sl::ScenarioKind::Campaign);
    OBL_REQUIRE(bySlot.has_value());
    OBL_CHECK(*bySlot == *s3);

    OBL_CHECK(!registry.FindById("does_not_exist").has_value());
    OBL_CHECK(!registry.FindById("s3", sl::ScenarioKind::Sandbox).has_value());
    OBL_CHECK(!registry.FindByMenuSlot(3, sl::ScenarioKind::Campaign).has_value());
    OBL_CHECK(registry.Find(sl::ScenarioRef{}) == nullptr);
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
