#include "unit_test.h"
#include "game/world/world_state.h"
#include "game/game/game_core.h"
#include "game/objects/guest_sets.h"
#include "formats/map_pack.h"
#include "core/ascii_case.h"
#include "importer/src/readers/pis.h"
#include "game/world/scenario_load.h"
#include "game/world/world_construct_pis.h"
#include "game/world/world_construct_scenario.h"
#include "importer/src/readers/building_list.h"
#include "importer/src/readers/save/save_stream.h"
#include "importer/src/writers/scenario_convert.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "importer/src/writers/sprite_convert.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>

namespace {
using Json = nlohmann::ordered_json;
std::string Read(const std::filesystem::path& file)
{
    std::vector<uint8_t> bytes;
    std::string error;
    OBL_REQUIRE(obl::convert::ReadBoundedFile(file.string(), 1 << 20, bytes, error));
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
std::string Installed(const std::filesystem::path& root, const std::string& file)
{
    const auto path = obl::convert::FindInstalledFile(root, file);
    OBL_REQUIRE(path);
    return Read(*path);
}
std::string Synthetic()
{
    return "'WORLD_NAME' \"Test\"\r\n"
           "'WORLD_MAPFILENAME' \"Data/Scenario/Scenario1/Scenario1.biz\"\r\n"
           "'WORLD_MAPHEIGHTMAP' \"height.tga\"\r\n'WORLD_MAPGRID' \"grid.dat\"\r\n"
           "'WORLD_SCRIPT' \"test.lgc\"\r\n'WORLD_BUILDINGLIST' \"Data/Script/BuildingList.pis\"\r\n"
           "'WORLD_GUESTLIST' \"Data/Script/Guests03.pis\"\r\n'WORLD_PATHTILEINFO' \"data/pathtiles.dat\"\r\n"
           "'STARTMAPX' 12.9\r\n'STARTMAPY' -3.8\r\n'WORLD_HEIGHTMAPSCALE' 1\r\n"
           "'SCENARIO_OBJ_TITLE' \"TITLE\"\r\n'SCENARIO_OBJ_DESCRIPTION' \"DESC\"\r\n"
           "'SCENARIO_OBJ_OBJECTIVES' \"GOALS\"\r\n'SCENARIO_OBJ_NUMBER' 1.9\r\n'SCENARIO_OBJ_0' \"GOAL0\"\r\n"
           "'WORLD_STATUS_TITLE' \"ISLAND\"\r\n'WORLD_STATUS_AREA' \"AREA\"\r\n"
           "'WORLD_STATUS_TEMP' \"TEMP\"\r\n'WORLD_STATUS_WEATHER' \"WEATHER\"\r\n'WORLD_STATUS_HAZARDS' \"HAZARDS\"\r\n"
           "'BUILDING_0' \"HOTEL\"\r\n'BUILDING_1' \"HOTEL\"\r\n'BUILDING_3' \"IGNORED\"\r\n"
           "'MESSAGE_0' \"SUPPRESS\"\r\n'MESSAGE_2' \"IGNORED\"\r\n"
           "'RADAR_IMAGE' \"SCANNER1\"\r\n'RADAR_POSX' -2.9\r\n'RADAR_POSY' 9.8\r\n"
           "'WORLD_PAVEMENT_TILESET' \"PathTile_07\"\r\n'PROGRESS_DIALOG' \"Scenario1Progress\"\r\n";
}
struct Env {
    explicit Env(std::string_view text)
    {
        entries = obl::pis::ParseEntries(text.data(), static_cast<int>(text.size()));
        for (auto& e : entries)
            raw.push_back({e.key.data(), e.type, e.hasString ? e.string.data() : nullptr, e.hasNumber ? &e.number : nullptr});
    }
    const char* GetString(const char* key) { return obl::pis::GetString(raw.data(), static_cast<int>(raw.size()), key); }
    const float* GetNumber(const char* key) { return obl::pis::GetNumber(raw.data(), static_cast<int>(raw.size()), key); }
    std::string Duplicate(const char* value) { return value ? value : ""; }
    void MissingKey(const char*) { OBL_REQUIRE(false); }
    int BuildingId(const char* key)
    {
        for (size_t i = 0; i < buildings.size(); ++i)
            if (obl::ascii::EqualsNoCase(buildings[i].key.c_str(), key)) return static_cast<int>(i);
        return -1;
    }
    void AddScenarioMessage(const char* key) { messages.emplace_back(key); }
    std::vector<obl::pis::Entry> entries;
    std::vector<obl::pis::RawEntry> raw;
    std::vector<obl::convert::BlBuilding> buildings;
    std::vector<std::string> messages;
};
void CheckEngine(const std::string& text, const Json& doc, const std::vector<obl::convert::BlBuilding>& buildings)
{
    Env env(text);
    env.buildings = buildings;
    const auto header = obl::world_construct_pis::ReadHeader(env);
    auto world = std::make_unique<obl::layout::World>();
    obl::world_construct_pis::ReadTexts(*world, env);
    obl::world_construct_pis::ReadBuildings(*world, env);
    obl::world_construct_pis::ReadMessages(env);
    OBL_CHECK_EQ(doc["title"], world->m_scenarioTitle);
    OBL_CHECK_EQ(doc["description"], world->m_scenarioDescription);
    OBL_CHECK_EQ(doc["objectives_title"], world->m_scenarioObjectives);
    OBL_CHECK_EQ(doc["status_texts"]["title"], world->m_islandTitle);
    OBL_CHECK_EQ(doc["status_texts"]["area"], world->m_islandArea);
    OBL_CHECK_EQ(doc["status_texts"]["temperature"], world->m_islandTemp);
    OBL_CHECK_EQ(doc["status_texts"]["weather"], world->m_islandWeather);
    OBL_CHECK_EQ(doc["status_texts"]["hazards"], world->m_islandHazards);
    OBL_CHECK_EQ(doc["start_view"][0], static_cast<int>(*header.startMapX));
    OBL_CHECK_EQ(doc["start_view"][1], static_cast<int>(*header.startMapY));
    OBL_CHECK_EQ(doc["objectives"].size(), world->m_numObjectives);
    for (uint32_t i = 0; i < world->m_numObjectives; ++i) OBL_CHECK_EQ(doc["objectives"][i]["text"], world->m_objectiveText[i]);
    OBL_CHECK_EQ(doc["available_buildings"].size(), static_cast<size_t>(world->m_numValidBuildings));
    for (int i = 0; i < world->m_numValidBuildings; ++i) {
        const auto id = world->m_validBuildingIDs[i];
        OBL_REQUIRE(id >= 0 && static_cast<size_t>(id) < buildings.size());
        OBL_CHECK_EQ(doc["available_buildings"][i], obl::convert::LowerAscii(buildings[id].key));
    }
    OBL_CHECK(doc["messages"].get<std::vector<std::string>>() == env.messages);
    for (const auto& [key, field] : {std::pair{"WORLD_PAVEMENT_TILESET", "pavement"}, {"WORLD_BEACH_EDGE_TILESET", "beach_edge"}}) {
        if (const auto* value = env.GetString(key))
            OBL_CHECK_EQ(doc["tilesets"][field], obl::convert::LowerAscii(value));
        else
            OBL_CHECK(!doc["tilesets"].contains(field));
    }
    if (const auto* value = env.GetString("RADAR_IMAGE")) {
        OBL_CHECK_EQ(doc["radar"]["image"], obl::convert::LowerAscii(value));
        OBL_CHECK_EQ(doc["radar"]["pos"][0], env.GetNumber("RADAR_POSX") ? static_cast<int>(*env.GetNumber("RADAR_POSX")) : 0);
        OBL_CHECK_EQ(doc["radar"]["pos"][1], env.GetNumber("RADAR_POSY") ? static_cast<int>(*env.GetNumber("RADAR_POSY")) : 0);
    }
    if (const auto* value = env.GetString("PROGRESS_DIALOG")) OBL_CHECK_EQ(doc["progress_dialog"], value);
}
} // namespace

OBL_TEST_CASE("convert.scenario_effective_values_lists_and_schema")
{
    Json doc;
    std::vector<std::string> ignored;
    std::string error;
    const auto text = Synthetic() + "'startmapx' 23.7\r\n'MESSAGE_1' 4\r\n'UNUSED' 1\r\n";
    OBL_REQUIRE(obl::convert::ParseScenario(text, 3, false, doc, ignored, error));
    OBL_CHECK_EQ(doc["start_view"][0], 23);
    OBL_CHECK_EQ(doc["guests"], "campaign_03");
    OBL_CHECK_EQ(doc["map"], "scenario1");
    OBL_CHECK_EQ(doc["menu_texts"]["title"], "MAINSS_SCENARIO3_TITLE");
    OBL_CHECK_EQ(doc["pictures"]["large"]["frame"], 2);
    OBL_CHECK_EQ(doc["available_buildings"].size(), 2u);
    OBL_CHECK_EQ(doc["messages"].size(), 1u);
    OBL_CHECK(std::find(ignored.begin(), ignored.end(), "STARTMAPX") != ignored.end());
    OBL_CHECK(std::find(ignored.begin(), ignored.end(), "BUILDING_3") != ignored.end());
    OBL_CHECK(std::find(ignored.begin(), ignored.end(), "MESSAGE_2") != ignored.end());
    OBL_CHECK(std::find(ignored.begin(), ignored.end(), "UNUSED") != ignored.end());
    obl::convert::BlBuilding building;
    building.key = "HOTEL";
    CheckEngine(text, doc, {building});
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    OBL_REQUIRE(obl::formats::ValidateDocument(schemas, "obl.scenario", doc, error));
}
OBL_TEST_CASE("convert.scenario_defaults_sandbox_and_wrong_types")
{
    Json doc;
    std::vector<std::string> ignored;
    std::string error;
    const auto text = Synthetic() +
            "'WORLD_GUESTLIST' \"DATA/SCRIPT/SandboxH_Guests.pis\"\r\n"
            "'WORLD_PAVEMENT_TILESET' 4\r\n'SCENARIO_OBJ_NUMBER' \"wrong\"\r\n'RADAR_POSX' \"wrong\"\r\n";
    OBL_REQUIRE(obl::convert::ParseScenario(text, 12, true, doc, ignored, error));
    OBL_CHECK_EQ(doc["id"], "sandbox12");
    OBL_CHECK_EQ(doc["guests"], "sandbox_hard");
    OBL_CHECK_EQ(doc["menu_texts"]["title"], "MAINSS_SANDBOX12_TITLE");
    OBL_CHECK(doc["objectives"].empty());
    OBL_CHECK(doc["tilesets"].empty());
    OBL_CHECK_EQ(doc["radar"]["pos"][0], 0);
}
OBL_TEST_CASE("convert.scenario_reject_unrepresentable_sources")
{
    Json doc;
    std::vector<std::string> ignored;
    std::string error;
    for (const auto& suffix : {"'STARTMAPX' \"wrong\"\r\n", "'SCENARIO_OBJ_NUMBER' 5\r\n", "'SCENARIO_OBJ_NUMBER' -1\r\n",
                 "'WORLD_GUESTLIST' \"custom.pis\"\r\n", "'WORLD_BUILDINGLIST' \"custom.pis\"\r\n", "'MESSAGE_0' \"\"\r\n"}) {
        OBL_CHECK(!obl::convert::ParseScenario(Synthetic() + suffix, 1, false, doc, ignored, error));
        OBL_CHECK(!error.empty());
    }
}
OBL_TEST_CASE("convert.scenario_engine_building_limit_and_objective_slots")
{
    Json doc;
    std::vector<std::string> ignored;
    std::string error;
    auto text = Synthetic();
    for (int i = 0; i <= 128; ++i) text += "'BUILDING_" + std::to_string(i) + "' \"HOTEL\"\r\n";
    text += "'SCENARIO_OBJ_NUMBER' 4.9\r\n'SCENARIO_OBJ_1' \"GOAL1\"\r\n"
            "'SCENARIO_OBJ_2' \"GOAL2\"\r\n'SCENARIO_OBJ_3' \"GOAL3\"\r\n";
    OBL_REQUIRE(obl::convert::ParseScenario(text, 1, false, doc, ignored, error));
    OBL_CHECK_EQ(doc["available_buildings"].size(), 128u);
    OBL_CHECK_EQ(doc["objectives"].size(), 4u);
    OBL_CHECK(std::find(ignored.begin(), ignored.end(), "BUILDING_128") != ignored.end());
    obl::convert::BlBuilding building;
    building.key = "HOTEL";
    CheckEngine(text, doc, {building});
}
OBL_TEST_CASE("data.convert_scenarios_match_engine_loaders")
{
    const std::filesystem::path root = OBL_GAME_DIR;
    if (!obl::convert::FindInstalledFile(root, "Data/Scenario/Scenario1/Scenario1.pis")) OBL_SKIP("installation scenarios unavailable");
    std::string error;
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    const char* supplied = std::getenv("OBL_SCENARIO_ORACLE_PACK");
    const auto out = supplied
            ? std::filesystem::path(supplied)
            : root / "tmp" / ("convert-scenarios-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!supplied) {
        int files = 0;
        const bool converted = obl::convert::ConvertScenarios(
                *obl::convert::OpenDirectorySource(root), *obl::convert::OpenDirectorySink(out), schemas, files, error);
        if (!converted) obl::unit::ReportFailure(__FILE__, __LINE__, error);
        OBL_REQUIRE(converted);
        OBL_CHECK_EQ(files, 25);
    }
    obl::convert::BuildingList catalog;
    OBL_REQUIRE(obl::convert::ReadBuildingList(*obl::convert::OpenDirectorySource(root), catalog, error));
    const auto campaign = Json::parse(Read(out / "scenarios/campaign.json"));
    OBL_REQUIRE(obl::formats::ValidateDocument(schemas, "obl.campaign", campaign, error));
    int32_t progress[12], selected;
    obl::game_core::Reset(progress, selected);
    OBL_CHECK_EQ(campaign["unlock"], "sequential");
    OBL_CHECK_EQ(campaign["sandbox_unlock"], "campaign_completed_or_always");
    OBL_CHECK_EQ(campaign["scenario_screen"], "images/screens/scenarioscreen.png");
    OBL_CHECK_EQ(campaign["campaign"].size(), 12u);
    OBL_CHECK_EQ(campaign["sandboxes"].size(), 12u);
    OBL_CHECK_EQ(campaign["initially_available"].size(), 3u);
    OBL_CHECK_EQ(campaign["always_available"].size(), 2u);
    for (bool sandbox : {false, true})
        for (int n = 1; n <= 12; ++n) {
            const auto id = std::string(sandbox ? "sandbox" : "s") + std::to_string(n);
            const auto name = std::string(sandbox ? "SandScenario" : "Scenario") + std::to_string(n);
            const auto text = Installed(root, "Data/Scenario/" + name + "/" + name + ".pis");
            const auto doc = Json::parse(Read(out / "scenarios" / id / "scenario.json"));
            OBL_REQUIRE(obl::formats::ValidateDocument(schemas, "obl.scenario", doc, error));
            OBL_CHECK_EQ(doc["id"], id);
            OBL_CHECK_EQ(doc["kind"], sandbox ? "sandbox" : "campaign");
            OBL_CHECK_EQ(campaign[sandbox ? "sandboxes" : "campaign"][n - 1]["scenario"], id);
            CheckEngine(text, doc, catalog.buildings);
            Env env(text);
            const auto guest = doc["guests"].get<std::string>();
            std::string guestSource;
            if (guest.starts_with("campaign_"))
                guestSource = "data/script/guests" + guest.substr(9) + ".pis";
            else if (guest == "sandbox_easy")
                guestSource = "data/script/sandboxe_guests.pis";
            else if (guest == "sandbox_medium")
                guestSource = "data/script/sandboxm_guests.pis";
            else if (guest == "sandbox_hard")
                guestSource = "data/script/sandboxh_guests.pis";
            OBL_CHECK(obl::ascii::EqualsNoCase(guestSource.c_str(), env.GetString("WORLD_GUESTLIST")));
            std::string map = env.GetString("WORLD_MAPFILENAME");
            std::replace(map.begin(), map.end(), '\\', '/');
            OBL_CHECK_EQ(doc["map"], obl::convert::LowerAscii(std::filesystem::path(map).stem().string()));
            OBL_CHECK_EQ(doc["catalog"], "catalog/index.json");
            OBL_CHECK_EQ(doc["script"], "world.wsc");
            const auto menu = std::string(sandbox ? "MAINSS_SANDBOX" : "MAINSS_SCENARIO") + std::to_string(n);
            OBL_CHECK_EQ(doc["menu_texts"]["title"], menu + "_TITLE");
            OBL_CHECK_EQ(doc["menu_texts"]["description"], menu + "_DESC");
            for (const auto& field : {"large", "load", "save"}) {
                OBL_CHECK_EQ(
                        doc["pictures"][field]["sprite"], std::string(field) == "large" ? "ui3_fe_is_pics_large" : "ui3_fe_is_pics_small");
                OBL_CHECK_EQ(doc["pictures"][field]["frame"], n - 1);
            }
            if (!sandbox) {
                const bool initial = std::find(campaign["initially_available"].begin(), campaign["initially_available"].end(), id) !=
                        campaign["initially_available"].end();
                const bool always = std::find(campaign["always_available"].begin(), campaign["always_available"].end(), id) !=
                        campaign["always_available"].end();
                OBL_CHECK_EQ(initial, obl::game_core::Selectable(progress[n - 1], false));
                OBL_CHECK_EQ(always, obl::game_core::Selectable(progress[n - 1], true));
                OBL_CHECK(!campaign["campaign"][n - 1].contains("completion_screen"));
            }
        }
    if (!supplied) std::filesystem::remove_all(out);
}

namespace {
void CheckText(const std::string& engine, const std::optional<std::string>& loaded)
{
    OBL_CHECK_EQ(engine, loaded.value_or(std::string()));
}
} // namespace

OBL_TEST_CASE("data.scenario_load_matches_engine")
{
    namespace sl = obl::scenario_load;
    const std::filesystem::path root = OBL_GAME_DIR;
    if (!obl::convert::FindInstalledFile(root, "Data/Scenario/Scenario1/Scenario1.pis")) OBL_SKIP("installation scenarios unavailable");
    const auto pack = root / "packs" / "base";
    if (!std::filesystem::exists(pack / "scenarios" / "s1" / "scenario.json")) OBL_SKIP("converted scenarios unavailable");
    std::string error;
    obl::convert::BuildingList catalog;
    OBL_REQUIRE(obl::convert::ReadBuildingList(*obl::convert::OpenDirectorySource(root), catalog, error));
    for (bool sandbox : {false, true})
        for (int n = 1; n <= 12; ++n) {
            const auto id = sl::ScenarioId(n, sandbox);
            const auto name = std::string(sandbox ? "SandScenario" : "Scenario") + std::to_string(n);
            const auto text = Installed(root, "Data/Scenario/" + name + "/" + name + ".pis");
            sl::Setup setup;
            OBL_REQUIRE(sl::LoadPack(pack, id, setup, error));
            OBL_CHECK_EQ(setup.id, id);

            Env engine(text);
            engine.buildings = catalog.buildings;
            const auto header = obl::world_construct_pis::ReadHeader(engine);
            auto reference = std::make_unique<obl::layout::World>();
            obl::world_construct_pis::ReadTexts(*reference, engine);
            obl::world_construct_pis::ReadMessages(engine);
            obl::world_construct_pis::ReadBuildings(*reference, engine);

            // The loader: the same steps from the converted scenario.
            Env loaded(text);
            loaded.buildings = catalog.buildings;
            auto world = std::make_unique<obl::layout::World>();
            obl::world_construct_scenario::ApplyTexts(*world, setup, loaded);
            obl::world_construct_scenario::ApplyMessages(setup, loaded);
            obl::world_construct_scenario::ApplyBuildings(*world, setup, loaded);

            OBL_CHECK_EQ(setup.mapFile, std::string(header.mapFile));
            OBL_CHECK_EQ(setup.mapId, obl::map_pack::BizMapId(header.mapFile));
            OBL_CHECK_EQ(setup.script, std::string(header.script));
            OBL_CHECK_EQ(setup.heightMap, std::string(header.heightmap));
            OBL_CHECK_EQ(setup.grid, std::string(header.grid));
            OBL_CHECK_EQ(setup.waterAnim, std::string(header.waterAnim));
            OBL_CHECK_EQ(setup.startX, static_cast<int>(*header.startMapX));
            OBL_CHECK_EQ(setup.startY, static_cast<int>(*header.startMapY));
            // The height scale is the engine's only where the map has a height map; a flat map's scale is 0 (grid_lifecycle.h).
            bool hasHeight = false;
            std::string heightError;
            OBL_REQUIRE(obl::map_pack::HasHeightMap(pack, setup.grid, hasHeight, heightError));
            if (hasHeight)
                OBL_CHECK_EQ(setup.heightScale, *header.heightScale);
            else
                OBL_CHECK_EQ(setup.heightScale, 0.0f);
            OBL_CHECK_EQ(setup.depthPlane, obl::world_construct_pis::DepthPlaneOffset(header.depthPlane));
            OBL_CHECK_EQ(setup.depthSlant, obl::world_construct_pis::DepthSlantOffset(header.depthSlant));
            const char* set = obl::guest_sets::SetForList(header.guestList);
            OBL_CHECK_EQ(set ? std::string(set) : std::string(), setup.guestSet);

            CheckText(reference->m_islandTitle, setup.islandTitle);
            CheckText(reference->m_islandArea, setup.islandArea);
            CheckText(reference->m_islandWeather, setup.islandWeather);
            CheckText(reference->m_islandTemp, setup.islandTemp);
            CheckText(reference->m_islandHazards, setup.islandHazards);
            OBL_CHECK_EQ(std::string(reference->m_scenarioDescription), setup.description);
            OBL_CHECK_EQ(std::string(reference->m_scenarioObjectives), setup.objectivesTitle);
            OBL_CHECK_EQ(std::string(reference->m_scenarioTitle), setup.title);
            OBL_CHECK_EQ(reference->m_numObjectives, world->m_numObjectives);
            for (uint32_t i = 0; i < 4; ++i) {
                OBL_CHECK_EQ(reference->m_objectiveText[i], world->m_objectiveText[i]);
            }
            OBL_CHECK(loaded.messages == engine.messages);
            OBL_CHECK_EQ(reference->m_numValidBuildings, world->m_numValidBuildings);
            for (int i = 0; i < world->m_numValidBuildings; ++i)
                OBL_CHECK_EQ(world->m_validBuildingIDs[i], reference->m_validBuildingIDs[i]);

            if (const auto* image = engine.GetString("RADAR_IMAGE")) {
                OBL_REQUIRE(setup.radar);
                OBL_CHECK(obl::ascii::EqualsNoCase(image, setup.radar->image.c_str()));
                OBL_CHECK_EQ(setup.radar->posX, engine.GetNumber("RADAR_POSX") ? static_cast<int>(*engine.GetNumber("RADAR_POSX")) : 0);
                OBL_CHECK_EQ(setup.radar->posY, engine.GetNumber("RADAR_POSY") ? static_cast<int>(*engine.GetNumber("RADAR_POSY")) : 0);
            } else {
                OBL_CHECK(!setup.radar);
            }
            CheckText(engine.GetString("PROGRESS_DIALOG"), setup.progressDialog);
        }
}

OBL_TEST_CASE("data.campaign_load_matches_engine")
{
    namespace sl = obl::scenario_load;
    const std::filesystem::path root = OBL_GAME_DIR;
    if (!obl::convert::FindInstalledFile(root, "Data/Scenario/Scenario1/Scenario1.pis")) OBL_SKIP("installation scenarios unavailable");
    const auto pack = root / "packs" / "base";
    if (!std::filesystem::exists(pack / "scenarios" / "campaign.json")) OBL_SKIP("converted campaign unavailable");
    sl::Campaign campaign;
    std::string error;
    OBL_REQUIRE(sl::LoadCampaign(pack, campaign, error));

    // The order the engine built in before it read campaign.json: menu index n-1 is s<n> and sandbox<n>.
    for (int32_t index = 0; index < 12; ++index) {
        OBL_CHECK_EQ(campaign.Id(index, false), sl::ScenarioId(index + 1, false));
        OBL_CHECK_EQ(campaign.Id(index, true), sl::ScenarioId(index + 1, true));
    }

    // The start progress of the file gives the table the built-in Reset gives.
    int32_t built[12], fromFile[12], builtSelected = -1, fileSelected = -1;
    obl::game_core::Reset(built, builtSelected);
    obl::game_core::Reset(fromFile, fileSelected, campaign.initiallyAvailable, campaign.alwaysAvailable);
    OBL_CHECK_EQ(fileSelected, builtSelected);
    for (int32_t index = 0; index < 12; ++index) OBL_CHECK_EQ(fromFile[index], built[index]);
}

OBL_TEST_CASE("data.campaign_order_comes_from_the_file")
{
    namespace sl = obl::scenario_load;
    const auto dir = std::filesystem::temp_directory_path() /
            ("obl-campaign-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir / "scenarios");
    const auto file = dir / "scenarios" / "campaign.json";
    // A campaign that starts at s12 and goes backwards; the availability names its own menu indices.
    Json campaign = {{"format", "obl.campaign"}, {"version", 1}, {"campaign", Json::array()}, {"unlock", "sequential"},
            {"initially_available", {"s12", "s11", "s10"}}, {"always_available", {"s12", "s11"}},
            {"sandbox_unlock", "campaign_completed_or_always"}, {"sandboxes", Json::array()}};
    for (int n = 12; n >= 1; --n) {
        campaign["campaign"].push_back({{"scenario", "s" + std::to_string(n)}});
        campaign["sandboxes"].push_back({{"scenario", "sandbox" + std::to_string(n)}});
    }
    {
        std::ofstream out(file);
        out << campaign.dump(2);
    }

    sl::Campaign loaded;
    std::string error;
    OBL_REQUIRE(sl::LoadCampaign(dir, loaded, error));
    OBL_CHECK_EQ(loaded.Id(0, false), "s12");
    OBL_CHECK_EQ(loaded.Id(11, false), "s1");
    OBL_CHECK_EQ(loaded.Id(0, true), "sandbox12");
    int32_t progress[12], selected = -1;
    obl::game_core::Reset(progress, selected, loaded.initiallyAvailable, loaded.alwaysAvailable);
    OBL_CHECK_EQ(progress[0], 4);
    OBL_CHECK_EQ(progress[1], 4);
    OBL_CHECK_EQ(progress[2], 2); // s10, initially available
    OBL_CHECK_EQ(progress[9], 1); // s3 is locked in this order
    OBL_CHECK_EQ(progress[11], 1); // s1 is locked too

    // The two lists must have the same size; a mismatch is refused and leaves the result untouched.
    campaign["campaign"].erase(campaign["campaign"].size() - 1);
    {
        std::ofstream out(file);
        out << campaign.dump(2);
    }
    sl::Campaign refused;
    refused.campaign = {"keep"};
    OBL_CHECK(!sl::LoadCampaign(dir, refused, error));
    OBL_CHECK(!error.empty());
    OBL_CHECK_EQ(refused.campaign.size(), 1u);

    // A one-entry campaign (the L0 scenario) loads; the other progress slots stay unavailable.
    Json minimalCampaign = {{"format", "obl.campaign"}, {"version", 1}, {"unlock", "sequential"}, {"campaign", {{{"scenario", "l0"}}}},
            {"sandboxes", {{{"scenario", "l0"}}}}, {"always_available", {"l0"}}};
    {
        std::ofstream out(file);
        out << minimalCampaign.dump(2);
    }
    sl::Campaign one;
    OBL_REQUIRE(sl::LoadCampaign(dir, one, error));
    OBL_CHECK_EQ(one.campaign.size(), 1u);
    const auto count = static_cast<int32_t>(one.campaign.size());
    obl::game_core::Reset(progress, selected, one.initiallyAvailable, one.alwaysAvailable);
    OBL_CHECK_EQ(progress[0], 4);
    OBL_CHECK_EQ(progress[1], 1);
    obl::game_core::Complete(progress, 0, count);
    OBL_CHECK_EQ(progress[0], 12);
    OBL_CHECK_EQ(progress[1], 1); // no next entry to unlock
    OBL_CHECK_EQ(obl::game_core::SelectAfterLoad(progress, false, count), 0);
    obl::game_core::UnlockAll(progress, false, count);
    OBL_CHECK_EQ(progress[0], 4);
    for (int i = 1; i < 12; ++i) OBL_CHECK(!obl::game_core::Selectable(progress[i], false));
    obl::game_core::UnlockAll(progress, true, count);
    OBL_CHECK_EQ(progress[0], 8);
    for (int i = 1; i < 12; ++i) OBL_CHECK(!obl::game_core::Selectable(progress[i], true));
    std::filesystem::remove_all(dir);
}

namespace {
std::vector<uint8_t> Encoded(std::string_view text)
{
    std::vector<uint8_t> bytes(text.begin(), text.end());
    for (auto& byte : bytes) byte ^= 0xff;
    return bytes;
}
// The 12 campaign scenarios and 12 sandboxes with the files they refer to; the catalog holds the one building they offer.
obl::convert::MemorySource ScenarioSource()
{
    obl::convert::MemorySource source;
    source.Add("Data/Script/BuildingList.ble",
            Encoded("\"BUILD_HOTEL\" \"HOTEL_NAME\" \"Hotel\" \"HOTEL_SPRITE\" \"HotelSprite\" \"HOTEL_GRIDWIDTH\" \"1\" "
                    "\"HOTEL_GRIDHEIGHT\" \"1\" \"HOTEL_BUILDON\" \"LAND\" \"HOTEL_CATEGORY\" \"Other\" \"HOTEL_COST2BUILD\" \"50\"\n"));
    source.Add("Data/Script/Guests03.ble", Encoded(""));
    source.Add("Data/Script/sandboxh_guests.ble", Encoded(""));
    source.Add("Data/Scenario/Scenario1/Scenario1.biz", {0});
    for (bool sandbox : {false, true})
        for (int n = 1; n <= 12; ++n) {
            const auto name = std::string(sandbox ? "SandScenario" : "Scenario") + std::to_string(n);
            const auto folder = "Data/Scenario/" + name + "/";
            auto text = Synthetic() + "'WORLD_SCRIPT' \"" + folder + name + ".lgc\"\r\n";
            if (sandbox) text += "'WORLD_GUESTLIST' \"data/script/sandboxh_guests.pis\"\r\n";
            source.Add(folder + name + ".pis", std::vector<uint8_t>(text.begin(), text.end()));
            source.Add(folder + name + ".ble", Encoded(""));
        }
    return source;
}
} // namespace

OBL_TEST_CASE("convert.scenarios_convert_a_synthetic_installation")
{
    std::string error;
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    const auto source = ScenarioSource();
    obl::convert::MemorySink out;
    int files = 0;
    std::vector<std::string> report;
    OBL_REQUIRE(obl::convert::ConvertScenarios(source, out, schemas, files, error, &report));
    OBL_CHECK_EQ(files, 25);
    OBL_CHECK_EQ(out.files().size(), 25u);
    const auto read = [&](std::string_view path) {
        const auto bytes = out.Read(path);
        OBL_REQUIRE(bytes.has_value());
        return Json::parse(bytes->begin(), bytes->end());
    };
    const auto s7 = read("scenarios/s7/scenario.json");
    OBL_CHECK_EQ(s7["kind"], "campaign");
    OBL_CHECK_EQ(s7["guests"], "campaign_03");
    OBL_CHECK_EQ(s7["available_buildings"], Json::array({"hotel", "hotel"}));
    OBL_CHECK_EQ(s7["pictures"]["large"]["frame"], 6);
    const auto sandbox = read("scenarios/sandbox12/scenario.json");
    OBL_CHECK_EQ(sandbox["kind"], "sandbox");
    OBL_CHECK_EQ(sandbox["guests"], "sandbox_hard");
    const auto campaign = read("scenarios/campaign.json");
    OBL_REQUIRE(campaign["campaign"].size() == 12u);
    OBL_REQUIRE(campaign["sandboxes"].size() == 12u);
    OBL_CHECK_EQ(campaign["campaign"][11]["scenario"], "s12");
    OBL_CHECK_EQ(campaign["sandboxes"][0]["scenario"], "sandbox1");
    // Unused entries are reported per scenario: the sandboxes' first guest list was overridden.
    OBL_CHECK(std::find(report.begin(), report.end(), "s1: ignored BUILDING_3") != report.end());
    OBL_CHECK(std::find(report.begin(), report.end(), "sandbox1: ignored WORLD_GUESTLIST") != report.end());
}

OBL_TEST_CASE("convert.scenarios_refuse_missing_references")
{
    std::string error;
    obl::json_schema::Registry schemas;
    OBL_REQUIRE(obl::formats::LoadEmbeddedSchemas(schemas, error));
    int files = 0;
    const auto refused = [&](obl::convert::MemorySource source, std::string_view message) {
        obl::convert::MemorySink out;
        OBL_CHECK(!obl::convert::ConvertScenarios(source, out, schemas, files, error));
        OBL_CHECK(error.find(message) != std::string::npos);
    };
    auto source = ScenarioSource();
    source.Add("Data/Scenario/Scenario4/Scenario4.pis", {});
    refused(source, "Scenario4: missing scenario string: WORLD_NAME");
    source = ScenarioSource();
    source.Add("Data/Scenario/SandScenario2/SandScenario2.pis", [] {
        const auto text = Synthetic() + "'WORLD_SCRIPT' \"Data/Scenario/Scenario1/Scenario1.lgc\"\r\n";
        return std::vector<uint8_t>(text.begin(), text.end());
    }());
    refused(source, "scenario script does not match");
    source = ScenarioSource();
    source.Add("Data/Scenario/Scenario1/Scenario1.pis", [] {
        const auto text = Synthetic() + "'WORLD_SCRIPT' \"Data/Scenario/Scenario1/Scenario1.lgc\"\r\n'BUILDING_2' \"CASTLE\"\r\n";
        return std::vector<uint8_t>(text.begin(), text.end());
    }());
    refused(source, "available building: castle");
    obl::convert::MemorySource incomplete;
    incomplete.Add("Data/Script/BuildingList.ble", *ScenarioSource().ReadAll("Data/Script/BuildingList.ble"));
    refused(incomplete, "missing scenario Scenario1");
}
