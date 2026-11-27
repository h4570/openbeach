#include "scenario_load.h"
#include "catalog_schemas.h"
#include "core/json_schema.h"
#include "core/pack_path.h"
#include "formats/data_patch.h"
#include "formats/script_pack.h"

#include <algorithm>
#include <fstream>
#include <set>
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
    data_patch::Input file(path);
    if (!file) throw std::runtime_error("cannot open " + path.string());
    json doc = json::parse(file, nullptr, false);
    if (doc.is_discarded()) throw std::runtime_error(path.string() + ": invalid JSON");
    const auto errors = schemas.Validate(schema, doc);
    if (!errors.empty()) throw std::runtime_error(path.string() + ": " + errors.front());
    return doc;
}

const json_schema::Registry& GetSchemas()
{
    // Validation caches references and patterns, so reuse the schemas across the entries in a campaign scan.
    static thread_local const auto schemas = [] {
        json_schema::Registry registry;
        std::string error;
        for (const char* schema : catalog::kSchemas)
            if (!registry.Add(json::parse(schema), error)) throw std::runtime_error(error);
        return registry;
    }();
    return schemas;
}

std::string ScenarioPath(std::string_view id)
{
    return "scenarios/" + std::string(id) + "/";
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

// Keep the name the script host received for numbered scenarios. The file lookup uses the JSON path.
std::string HostScriptPath(std::string_view id)
{
    const bool sandbox = id.starts_with("sandbox");
    if (!sandbox && !id.starts_with('s')) return {};
    const std::string_view number = id.substr(sandbox ? 7 : 1);
    if (number.empty() || number.find_first_not_of("0123456789") != std::string_view::npos) return {};
    return MapPath((sandbox ? "sandscenario" : "scenario") + std::string(number), ".lgc");
}

ScenarioPicture ReadPicture(const json& picture)
{
    if (picture.is_string()) return picture.get<std::string>();
    return SpritePicture{picture.at("sprite").get<std::string>(), picture.at("frame").get<int32_t>()};
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
    s.script = ScenarioPath(id) + scenario.value("script", "world.wsc");
    s.hostScript = HostScriptPath(id);
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

bool LoadPresentation(
        const std::filesystem::path& pack, std::string_view id, ScenarioTextKeys& text, ScenarioPictures& pictures, std::string& error)
{
    error.clear();
    const auto file = pack_path::Resolve(pack, ScenarioPath(id) + "scenario.json");
    try {
        const json scenario = ReadDocument(file, GetSchemas(), "obl.scenario");
        if (const auto menu = scenario.find("menu_texts"); menu != scenario.end()) {
            text.title = menu->at("title").get<std::string>();
            text.description = menu->at("description").get<std::string>();
        }
        if (const auto fields = scenario.find("pictures"); fields != scenario.end()) {
            if (fields->contains("large")) pictures.large = ReadPicture(fields->at("large"));
            if (fields->contains("load")) pictures.load = ReadPicture(fields->at("load"));
            if (fields->contains("save")) pictures.save = ReadPicture(fields->at("save"));
        }
        return true;
    } catch (const std::exception& failure) {
        error = file.string() + ": " + failure.what();
        return false;
    }
}

bool ReadWorldScript(const std::filesystem::path& pack, const std::filesystem::path& script, std::string& text, std::string& error)
{
    return script_pack::ReadScript(pack_path::Resolve(pack, script), script_pack::Kind::World, text, error);
}

bool LoadPack(const std::filesystem::path& pack, std::string_view id, Setup& out, std::string& error)
{
    error.clear();
    const auto file = pack_path::Resolve(pack, ScenarioPath(id) + "scenario.json");
    try {
        const auto& schemas = GetSchemas();
        const json scenario = ReadDocument(file, schemas, "obl.scenario");
        const std::string mapId = scenario.at("map").get<std::string>();
        const auto mapFile = pack_path::Resolve(pack, std::filesystem::path("maps") / mapId / "map.json");
        std::error_code ec;
        if (!std::filesystem::is_regular_file(mapFile, ec)) {
            std::string message = "unknown map \"" + mapId + "\"";
            const auto maps = ListMapIds(pack);
            if (!maps.empty()) {
                message += " (available maps: ";
                for (size_t i = 0; i < maps.size(); ++i) {
                    if (i > 0) message += ", ";
                    message += maps[i];
                }
                message += ")";
            }
            throw std::runtime_error(message);
        }
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
        const auto& schemas = GetSchemas();
        out = ReadCampaign(ReadDocument(file, schemas, "obl.campaign"));
        return true;
    } catch (const std::exception& failure) {
        error = file.string() + ": " + failure.what();
        return false;
    }
}

bool LoadScenarioSet(const std::filesystem::path& file, ScenarioSetDefinition& out, std::string& error)
{
    error.clear();
    try {
        const json doc = ReadDocument(file, GetSchemas(), "obl.scenario_set");
        ScenarioSetDefinition parsed;
        parsed.id = doc.at("id").get<std::string>();
        parsed.title = doc.at("title").get<std::string>();
        parsed.description = doc.value("description", std::string());
        for (const json& entry : doc.value("scenarios", json::array())) parsed.scenarios.push_back(entry.at("scenario").get<std::string>());
        const std::string unlock = doc.value("unlock", std::string("sequential"));
        parsed.unlock = unlock == "all" ? SetUnlock::All : SetUnlock::Sequential;
        for (const json& id : doc.value("initially_available", json::array())) parsed.initiallyAvailable.push_back(id.get<std::string>());
        for (const json& id : doc.value("requires_completed", json::array())) parsed.requiresCompleted.push_back(id.get<std::string>());
        for (const json& entry : doc.value("sandboxes", json::array())) parsed.sandboxes.push_back(entry.at("scenario").get<std::string>());
        const std::string sandboxUnlock = doc.value("sandbox_unlock", std::string("all"));
        parsed.sandboxUnlock = sandboxUnlock == "set_completed" ? SandboxUnlock::SetCompleted : SandboxUnlock::All;
        out = std::move(parsed);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

std::string ModPrefix(std::string_view modId)
{
    std::string prefix(modId);
    for (char& c : prefix)
        if (c == '-') c = '_';
    return prefix;
}

bool HasModPrefix(std::string_view id, std::string_view modId)
{
    const std::string prefix = ModPrefix(modId);
    if (prefix.empty()) return false;
    return id == prefix || (id.size() > prefix.size() && id.starts_with(prefix) && id[prefix.size()] == '_');
}

std::vector<std::string> ListMapIds(const std::filesystem::path& pack)
{
    std::set<std::string> ids;
    for (const auto& layer : pack_path::Layers(pack)) {
        const auto mapsDir = layer / "maps";
        std::error_code ec;
        if (!std::filesystem::is_directory(mapsDir, ec)) continue;
        for (std::filesystem::directory_iterator it(mapsDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            if (std::filesystem::is_regular_file(it->path() / "map.json", ec)) ids.insert(it->path().filename().string());
        }
    }
    return {ids.begin(), ids.end()};
}

} // namespace obl::scenario_load
