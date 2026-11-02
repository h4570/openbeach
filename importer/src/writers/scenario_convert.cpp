#include "importer/src/writers/scenario_convert.h"
#include "game/objects/guest_sets.h"
#include "core/ascii_case.h"
#include "importer/src/readers/pis.h"
#include "importer/src/readers/building_list.h"
#include "importer/src/writers/content_key.h"
#include "importer/src/readers/save/save_stream.h"
#include "formats/schemas.h"
#include "importer/src/source/source.h"
#include "importer/src/writers/sprite_convert.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace obl::convert {
namespace {
using Json = nlohmann::ordered_json;
std::string Path(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    return LowerAscii(value);
}
class Pis {
public:
    explicit Pis(std::string_view text) : entries(pis::ParseEntries(text.data(), static_cast<int>(text.size()))) {}
    const pis::Entry* Find(const std::string& key)
    {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
            if (ascii::EqualsNoCase(it->key.c_str(), key.c_str())) {
                used.insert(static_cast<size_t>(it - entries.rbegin()));
                return &*it;
            }
        return nullptr;
    }
    std::string String(const std::string& key)
    {
        const auto* entry = Find(key);
        if (entry && entry->type == pis::String && entry->hasString && entry->string.empty())
            throw std::runtime_error("empty scenario string: " + key);
        return entry && entry->type == pis::String && entry->hasString ? entry->string : std::string();
    }
    int Integer(const std::string& key)
    {
        const auto* entry = Find(key);
        if (!entry || entry->type != pis::Number || !entry->hasNumber) return 0;
        const double value = entry->number;
        if (!std::isfinite(value) || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
            throw std::runtime_error("out-of-range scenario number: " + key);
        return static_cast<int>(value);
    }
    std::vector<pis::Entry> entries;
    std::set<size_t> used;
};
std::string GuestKey(const std::string& path)
{
    if (const char* set = obl::guest_sets::SetForList(path)) return set;
    throw std::runtime_error("unrecognized WORLD_GUESTLIST: " + Path(path));
}
bool Write(PackSink& out, const std::string& path, const Json& doc, std::string& error)
{
    return out.WriteText(path, doc.dump(2) + '\n', error);
}
} // namespace

bool ParseScenario(std::string_view text, int number, bool sandbox, Json& doc, std::vector<std::string>& ignored, std::string& error)
{
    try {
        if (number < 1 || number > 12 || text.size() > (1u << 20)) throw std::runtime_error("invalid scenario input");
        Pis p(text);
        const auto required = [&](const std::string& key) {
            auto value = p.String(key);
            if (value.empty()) throw std::runtime_error("missing scenario string: " + key);
            return value;
        };
        // The engine requires these names even though the target replaces them with references.
        required("WORLD_NAME");
        for (const char* key : {"WORLD_MAPHEIGHTMAP", "WORLD_MAPGRID", "WORLD_SCRIPT"}) required(key);
        if (Path(required("WORLD_BUILDINGLIST")) != "data/script/buildinglist.pis" ||
                Path(required("WORLD_PATHTILEINFO")) != "data/pathtiles.dat")
            throw std::runtime_error("unsupported scenario catalog or path tile source");
        for (const char* key : {"STARTMAPX", "STARTMAPY", "WORLD_HEIGHTMAPSCALE"}) {
            const auto* entry = p.Find(key);
            if (!entry || entry->type != pis::Number || !entry->hasNumber || !std::isfinite(entry->number))
                throw std::runtime_error(std::string("missing scenario number: ") + key);
        }
        // Map metadata is emitted by ConvertMaps, not duplicated in scenario.json.
        for (const char* key : {"WORLD_WATERANIM", "WORLD_DEPTH_PLANE_OFFSET", "WORLD_DEPTH_SLANT_OFFSET"}) p.Find(key);
        const auto map = std::filesystem::path(Path(required("WORLD_MAPFILENAME"))).stem().string();
        if (map.empty() || ContentKey(map) != map) throw std::runtime_error("invalid scenario map key");
        const auto id = (sandbox ? "sandbox" : "s") + std::to_string(number);
        doc = {{"format", "obl.scenario"}, {"version", 1}, {"id", id}, {"kind", sandbox ? "sandbox" : "campaign"},
                {"title", required("SCENARIO_OBJ_TITLE")}, {"description", required("SCENARIO_OBJ_DESCRIPTION")},
                {"objectives_title", required("SCENARIO_OBJ_OBJECTIVES")}};
        doc["status_texts"] = {{"title", required("WORLD_STATUS_TITLE")}, {"area", required("WORLD_STATUS_AREA")},
                {"temperature", required("WORLD_STATUS_TEMP")}, {"weather", required("WORLD_STATUS_WEATHER")},
                {"hazards", required("WORLD_STATUS_HAZARDS")}};
        const auto menu = std::string(sandbox ? "MAINSS_SANDBOX" : "MAINSS_SCENARIO") + std::to_string(number);
        doc["menu_texts"] = {{"title", menu + "_TITLE"}, {"description", menu + "_DESC"}};
        doc["map"] = map;
        doc["start_view"] = {p.Integer("STARTMAPX"), p.Integer("STARTMAPY")};
        doc["catalog"] = "catalog/index.json";
        doc["guests"] = GuestKey(required("WORLD_GUESTLIST"));
        doc["available_buildings"] = Json::array();
        for (int i = 0; i < 128; ++i) {
            auto key = p.String("BUILDING_" + std::to_string(i));
            if (key.empty()) break;
            doc["available_buildings"].push_back(LowerAscii(key));
        }
        const int objectives = p.Integer("SCENARIO_OBJ_NUMBER");
        if (objectives < 0 || objectives > 4) throw std::runtime_error("scenario objective count outside engine slots");
        doc["objectives"] = Json::array();
        for (int i = 0; i < objectives; ++i) doc["objectives"].push_back({{"text", required("SCENARIO_OBJ_" + std::to_string(i))}});
        doc["messages"] = Json::array();
        for (int i = 0; i < static_cast<int>(p.entries.size()); ++i) {
            auto key = p.String("MESSAGE_" + std::to_string(i));
            if (key.empty()) break;
            doc["messages"].push_back(key);
        }
        doc["tilesets"] = Json::object();
        for (const auto& [key, field] : {std::pair{"WORLD_PAVEMENT_TILESET", "pavement"}, {"WORLD_BEACH_EDGE_TILESET", "beach_edge"}}) {
            auto value = p.String(key);
            if (!value.empty()) doc["tilesets"][field] = LowerAscii(value);
        }
        auto radar = p.String("RADAR_IMAGE");
        if (!radar.empty())
            doc["radar"] = {{"image", LowerAscii(radar)}, {"pos", {p.Integer("RADAR_POSX"), p.Integer("RADAR_POSY")}}};
        else {
            p.Find("RADAR_POSX");
            p.Find("RADAR_POSY");
        }
        doc["pictures"] = {{"large", {{"sprite", "ui3_fe_is_pics_large"}, {"frame", number - 1}}},
                {"load", {{"sprite", "ui3_fe_is_pics_small"}, {"frame", number - 1}}},
                {"save", {{"sprite", "ui3_fe_is_pics_small"}, {"frame", number - 1}}}};
        const auto progress = p.String("PROGRESS_DIALOG");
        if (!progress.empty()) doc["progress_dialog"] = progress;
        doc["script"] = "world.wsc";
        ignored.clear();
        for (size_t i = 0; i < p.entries.size(); ++i)
            if (!p.used.contains(p.entries.size() - 1 - i)) ignored.push_back(p.entries[i].key);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool ConvertScenarios(const ByteSource& source, PackSink& out, const json_schema::Registry& schemas, int& files, std::string& error,
        std::vector<std::string>* report)
{
    files = 0;
    try {
        BuildingList catalog;
        if (!ReadBuildingList(source, catalog, error)) return false;
        std::set<std::string> buildings;
        for (const auto& def : catalog.buildings) buildings.insert(LowerAscii(def.key));
        if (report) report->clear();
        // Our own campaign definition (scenarios.md "Campaign behavior"): the installation holds no order or unlock rule.
        Json campaign{{"format", "obl.campaign"}, {"version", 1}, {"campaign", Json::array()}, {"unlock", "sequential"},
                {"initially_available", {"s1", "s2", "s3"}}, {"always_available", {"s1", "s2"}},
                {"sandbox_unlock", "campaign_completed_or_always"}, {"sandboxes", Json::array()},
                {"scenario_screen", "images/screens/scenarioscreen.png"}};
        for (bool sandbox : {false, true})
            for (int n = 1; n <= 12; ++n) {
                const auto name = std::string(sandbox ? "SandScenario" : "Scenario") + std::to_string(n);
                const auto pisPath = FindFile(source, "Data/Scenario/" + name + "/" + name + ".pis");
                if (!pisPath) throw std::runtime_error("missing scenario " + name);
                std::vector<uint8_t> bytes;
                if (!ReadBoundedFile(source, *pisPath, 1 << 20, bytes, error)) return false;
                const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                Json doc;
                std::vector<std::string> ignored;
                if (!ParseScenario(text, n, sandbox, doc, ignored, error)) {
                    error = name + ": " + error;
                    return false;
                }
                Pis p(text);
                const auto script = Path(p.String("WORLD_SCRIPT"));
                if (script != Path("Data/Scenario/" + name + "/" + name + ".lgc"))
                    throw std::runtime_error("scenario script does not match the scripts converter: " + script);
                for (const char* key : {"WORLD_SCRIPT", "WORLD_GUESTLIST", "WORLD_BUILDINGLIST"}) {
                    auto path = std::filesystem::path(Path(p.String(key)));
                    path.replace_extension(".ble");
                    if (!FindFile(source, path.generic_string())) throw std::runtime_error("missing scenario reference " + path.string());
                }
                if (!FindFile(source, Path(p.String("WORLD_MAPFILENAME")))) throw std::runtime_error("missing scenario map");
                for (const auto& key : doc["available_buildings"])
                    if (!buildings.contains(key.get<std::string>()))
                        throw std::runtime_error("unrecognized available building: " + key.get<std::string>());
                if (!formats::ValidateDocument(schemas, "obl.scenario", doc, error)) return false;
                const auto id = doc["id"].get<std::string>();
                if (!Write(out, "scenarios/" + id + "/scenario.json", doc, error)) return false;
                ++files;
                if (report)
                    for (const auto& key : ignored) report->push_back(id + ": ignored " + key);
                campaign[sandbox ? "sandboxes" : "campaign"].push_back({{"scenario", id}});
            }
        if (!formats::ValidateDocument(schemas, "obl.campaign", campaign, error) || !Write(out, "scenarios/campaign.json", campaign, error))
            return false;
        ++files;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
} // namespace obl::convert
