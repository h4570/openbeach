#include "scenario_load.h"
#include "catalog_schemas.h"
#include "core/json_schema.h"
#include "core/pack_path.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace obl::scenario_load {
namespace {
using nlohmann::json;

// The engine's spelling of a path the `.pis` file named: the folder and file are the map id in lowercase, under `Data/Scenario`.
std::string MapPath(const std::string& map, std::string_view suffix)
{
    return "data/Scenario/" + map + "/" + map + std::string(suffix);
}

json ReadDocument(const std::filesystem::path& path, const json_schema::Registry& schemas, const char* schema)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path.string());
    json doc = json::parse(file, nullptr, false);
    if (doc.is_discarded()) throw std::runtime_error(path.string() + ": invalid JSON");
    const auto errors = schemas.Validate(schema, doc);
    if (!errors.empty()) throw std::runtime_error(path.string() + ": " + errors.front());
    return doc;
}

// The optional text of a converted key: absent (or null) means the lookup gave null.
std::optional<std::string> OptionalText(const json& object, const char* key)
{
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<std::string>();
}

// The map's water animation as the `.pis` file names it: "water/<n>" is data/wateranims/WaterAnim<n>.bin.
std::string WaterAnimPath(const std::string& water)
{
    constexpr std::string_view prefix = "water/";
    if (water.compare(0, prefix.size(), prefix) != 0) throw std::runtime_error("water_anim " + water + " is not water/<n>");
    return "data/wateranims/WaterAnim" + water.substr(prefix.size()) + ".bin";
}

// The folder of a numbered scenario's own files (its script): `scenario<n>` for a campaign, `sandscenario<n>` for a sandbox. A sandbox
// plays on its campaign's island, so its script folder is not its map's.
std::string ScenarioFolder(const std::string& id)
{
    constexpr std::string_view sandbox = "sandbox";
    if (id.compare(0, sandbox.size(), sandbox) == 0) return "sandscenario" + id.substr(sandbox.size());
    return "scenario" + id.substr(1);
}

Setup Fill(const json& scenario, const json& map, const std::string& id)
{
    Setup s;
    s.id = id;
    s.title = scenario.at("title").get<std::string>();
    s.description = scenario.value("description", std::string());
    s.objectivesTitle = scenario.value("objectives_title", std::string());
    if (scenario.contains("status_texts")) {
        const json& status = scenario.at("status_texts");
        s.islandTitle = OptionalText(status, "title");
        s.islandArea = OptionalText(status, "area");
        s.islandTemp = OptionalText(status, "temperature");
        s.islandWeather = OptionalText(status, "weather");
        s.islandHazards = OptionalText(status, "hazards");
    }
    if (scenario.contains("objectives"))
        for (const json& objective : scenario.at("objectives")) s.objectives.push_back(objective.at("text").get<std::string>());
    if (scenario.contains("messages"))
        for (const json& message : scenario.at("messages")) s.messages.push_back(message.get<std::string>());
    for (const json& building : scenario.at("available_buildings")) s.buildings.push_back(building.get<std::string>());

    const std::string mapId = scenario.at("map").get<std::string>();
    s.mapId = mapId;
    s.mapFile = MapPath(mapId, ".biz");
    const std::string folder = ScenarioFolder(id);
    s.script = "data/Scenario/" + folder + "/" + folder + ".lgc";
    s.heightMap = MapPath(mapId, "height.tga");
    s.grid = MapPath(mapId, "grid.dat");
    s.waterAnim = WaterAnimPath(map.at("water_anim").get<std::string>());
    s.heightScale = map.at("height_scale").get<float>();
    s.depthPlane = map.at("depth_plane_offset").get<int>();
    s.depthSlant = map.at("depth_slant_offset").get<int>();

    s.startX = scenario.at("start_view").at(0).get<int>();
    s.startY = scenario.at("start_view").at(1).get<int>();
    s.guestSet = scenario.at("guests").get<std::string>();

    if (scenario.contains("tilesets")) {
        const json& tilesets = scenario.at("tilesets");
        s.pavement = OptionalText(tilesets, "pavement");
        s.beachEdge = OptionalText(tilesets, "beach_edge");
    }
    if (scenario.contains("radar")) {
        const json& radar = scenario.at("radar");
        s.radar = Radar{radar.at("image").get<std::string>(), radar.at("pos").at(0).get<int>(), radar.at("pos").at(1).get<int>()};
    }
    s.progressDialog = OptionalText(scenario, "progress_dialog");
    return s;
}
} // namespace

std::string ScenarioId(int number, bool sandbox)
{
    return (sandbox ? "sandbox" : "s") + std::to_string(number);
}

bool LoadPack(const std::filesystem::path& pack, std::string_view id, Setup& out, std::string& error)
{
    error.clear();
    const auto file = pack_path::Resolve(pack, "scenarios/" + std::string(id) + "/scenario.json");
    try {
        json_schema::Registry schemas;
        std::string schemaError;
        for (const char* schema : catalog::kSchemas)
            if (!schemas.Add(json::parse(schema), schemaError)) throw std::runtime_error(schemaError);
        const json scenario = ReadDocument(file, schemas, "obl.scenario");
        const std::string mapId = scenario.at("map").get<std::string>();
        const auto mapFile = pack_path::Resolve(pack, std::filesystem::path("maps") / mapId / "map.json");
        const json map = ReadDocument(mapFile, schemas, "obl.map");
        out = Fill(scenario, map, std::string(id));
        return true;
    } catch (const std::exception& failure) {
        error = file.string() + ": " + failure.what();
        return false;
    }
}

namespace {
// The menu index of each id in `order`; a name that is not in the campaign order is refused.
std::vector<int32_t> IndicesOf(const json& names, const std::vector<std::string>& order, const char* key)
{
    std::vector<int32_t> indices;
    for (const json& name : names) {
        const std::string id = name.get<std::string>();
        const auto found = std::find(order.begin(), order.end(), id);
        if (found == order.end()) throw std::runtime_error(std::string(key) + " " + id + " is not in the campaign order");
        indices.push_back(static_cast<int32_t>(found - order.begin()));
    }
    return indices;
}

Campaign ReadCampaign(const json& doc)
{
    // The engine's progress table and its unlock rules are code (game_core.h); a file asking for another rule cannot be honored.
    constexpr size_t kEntries = 12; // at most; a smaller campaign leaves the remaining progress slots unavailable
    if (doc.at("unlock").get<std::string>() != "sequential") throw std::runtime_error("the engine supports unlock sequential only");
    if (doc.contains("sandbox_unlock") && doc.at("sandbox_unlock").get<std::string>() != "campaign_completed_or_always")
        throw std::runtime_error("the engine supports sandbox_unlock campaign_completed_or_always only");
    Campaign out;
    for (const json& entry : doc.at("campaign")) out.campaign.push_back(entry.at("scenario").get<std::string>());
    for (const json& entry : doc.value("sandboxes", json::array())) out.sandboxes.push_back(entry.at("scenario").get<std::string>());
    if (out.campaign.empty() || out.campaign.size() > kEntries || out.sandboxes.size() != out.campaign.size())
        throw std::runtime_error("the campaign and the sandboxes need the same number of entries, 1 to 12");
    if (doc.contains("initially_available"))
        out.initiallyAvailable = IndicesOf(doc.at("initially_available"), out.campaign, "initially_available");
    if (doc.contains("always_available")) out.alwaysAvailable = IndicesOf(doc.at("always_available"), out.campaign, "always_available");
    return out;
}
} // namespace

bool LoadCampaign(const std::filesystem::path& pack, Campaign& out, std::string& error)
{
    error.clear();
    const auto file = pack_path::Resolve(pack, "scenarios/campaign.json");
    try {
        json_schema::Registry schemas;
        std::string schemaError;
        for (const char* schema : catalog::kSchemas)
            if (!schemas.Add(json::parse(schema), schemaError)) throw std::runtime_error(schemaError);
        out = ReadCampaign(ReadDocument(file, schemas, "obl.campaign"));
        return true;
    } catch (const std::exception& failure) {
        error = file.string() + ": " + failure.what();
        return false;
    }
}

} // namespace obl::scenario_load
