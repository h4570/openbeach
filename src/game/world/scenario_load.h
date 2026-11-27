#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// The scenario the engine builds a world from (obl.scenario, maps-scenarios.md "Scenarios"): the converted `scenarios/<id>/scenario.json`
// and the `maps/<map>/map.json` it names. The values are the ones the engine used to read from the `.pis` file (ConstructWorld's
// header, texts, messages and buildings), so the same world results; the engine paths (BIZ, grid, water animation) are derived
// from the map id exactly as the `.pis` file spells them.
namespace obl::scenario_load {

// The campaign and sandbox ids of the numbered setups: "s3", "sandbox3" (the save header's scenario number and sandbox flag).
std::string ScenarioId(int number, bool sandbox);

// The engine-side values: what ConstructWorld read from the `.pis` header. Optional values are absent exactly where the
// lookup gave null (the world keeps an empty text there).
struct Radar {
    std::string image; // lowercase, as the converter writes it
    int posX = 0;
    int posY = 0;
};

struct Setup {
    std::string id; // "s3", "sandbox3"
    std::string title; // SCENARIO_OBJ_TITLE: the text key of the scenario's name
    std::string description; // SCENARIO_OBJ_DESCRIPTION
    std::string objectivesTitle; // SCENARIO_OBJ_OBJECTIVES
    std::optional<std::string> islandTitle; // WORLD_STATUS_TITLE
    std::optional<std::string> islandArea; // WORLD_STATUS_AREA
    std::optional<std::string> islandTemp; // WORLD_STATUS_TEMP
    std::optional<std::string> islandWeather; // WORLD_STATUS_WEATHER
    std::optional<std::string> islandHazards; // WORLD_STATUS_HAZARDS
    std::vector<std::string> objectives; // SCENARIO_OBJ_<n>, in order
    std::vector<std::string> messages; // MESSAGE_<n>: the feedback types suppressed in this scenario
    std::vector<std::string> buildings; // BUILDING_<n>: the engine's spelling of each key (the catalog's "/key"), in order
    std::string mapId; // the pack's map id, the scenario's `map`
    std::string mapFile; // WORLD_MAPFILENAME: data/Scenario/<map>/<map>.biz
    std::string script; // logical pack path, scenarios/<id>/<script>
    std::string hostScript; // WORLD_SCRIPT spelling passed to the host for numbered scenarios
    std::string heightMap; // WORLD_MAPHEIGHTMAP: data/Scenario/<map>/<map>height.tga
    std::string grid; // WORLD_MAPGRID: data/Scenario/<map>/<map>grid.dat
    std::string waterAnim; // WORLD_WATERANIM: data/wateranims/WaterAnim<n>.bin
    float heightScale = 0.0f; // map.json height_scale (the effective value; 0 for a flat map)
    int depthPlane = 0; // map.json depth_plane_offset
    int depthSlant = 0; // map.json depth_slant_offset
    int startX = 0; // start_view[0], truncated toward zero
    int startY = 0; // start_view[1]
    std::string guestSet; // guests: campaign_01..campaign_12, sandbox_easy/medium/hard
    std::optional<std::string> pavement; // WORLD_PAVEMENT_TILESET, lowercase
    std::optional<std::string> beachEdge; // WORLD_BEACH_EDGE_TILESET, lowercase
    std::optional<Radar> radar; // RADAR_IMAGE with RADAR_POSX/Y
    std::optional<std::string> progressDialog; // PROGRESS_DIALOG
};

struct SpritePicture {
    std::string sprite;
    int32_t frame = 0;
};

using ScenarioPicture = std::variant<SpritePicture, std::string>; // sprite frame or logical PNG path

struct ScenarioPictures {
    ScenarioPicture large;
    ScenarioPicture load;
    ScenarioPicture save;
};

struct ScenarioTextKeys {
    std::string title;
    std::string description;
};

// Applies the presentation fields of a validated scenario to the caller's defaults.
bool LoadPresentation(
        const std::filesystem::path& pack, std::string_view id, ScenarioTextKeys& text, ScenarioPictures& pictures, std::string& error);

// Reads a world script at a logical pack path, including mod overrides.
bool ReadWorldScript(const std::filesystem::path& pack, const std::filesystem::path& script, std::string& text, std::string& error);

// Loads `<pack>/scenarios/<id>/scenario.json` and its map's `maps/<map>/map.json` (both validated against their schemas) into `out`.
// Transactional: on failure `out` is untouched and `error` names the file and the reason.
bool LoadPack(const std::filesystem::path& pack, std::string_view id, Setup& out, std::string& error);

// The campaign order and start availability the engine reads from `<pack>/scenarios/campaign.json` (obl.campaign,
// maps-scenarios.md "Campaign"). The menu index (0-based) selects the entry: `campaign[index]` or `sandboxes[index]`.
struct Campaign {
    std::vector<std::string> campaign; // 12 scenario ids in menu order
    std::vector<std::string> sandboxes; // 12 sandbox ids in menu order
    std::vector<int32_t> initiallyAvailable; // menu indices open at the start (progress 2)
    std::vector<int32_t> alwaysAvailable; // menu indices always open (progress 4)

    const std::string& Id(int32_t index, bool sandbox) const { return sandbox ? sandboxes.at(index) : campaign.at(index); }
};

// Loads and validates the campaign. The engine's progress table has 12 slots, so both lists must have the same size, 1 to 12 (the rest stay unavailable); the availability lists name
// entries of the campaign order; only `unlock: sequential` and `sandbox_unlock: campaign_completed_or_always` are supported (game_core.h).
bool LoadCampaign(const std::filesystem::path& pack, Campaign& out, std::string& error);

enum class SetUnlock : uint8_t { Sequential, All };
enum class SandboxUnlock : uint8_t { All, SetCompleted };

// A mod-shipped scenario set (`scenarios/sets/<id>.json`, format `obl.scenario_set` v1).
struct ScenarioSetDefinition {
    std::string id;
    std::string title;
    std::string description;
    std::vector<std::string> scenarios;
    SetUnlock unlock = SetUnlock::Sequential;
    std::vector<std::string> initiallyAvailable;
    std::vector<std::string> requiresCompleted;
    std::vector<std::string> sandboxes;
    SandboxUnlock sandboxUnlock = SandboxUnlock::All;
};

// Loads and validates one `obl.scenario_set` file.
bool LoadScenarioSet(const std::filesystem::path& file, ScenarioSetDefinition& out, std::string& error);

// Mod id with `-` written as `_`, the required prefix for set ids and scenario ids owned by that mod.
std::string ModPrefix(std::string_view modId);
bool HasModPrefix(std::string_view id, std::string_view modId);

// Sorted map keys available in the pack and active layers (subdirectories under `maps/` with `map.json`).
std::vector<std::string> ListMapIds(const std::filesystem::path& pack);

} // namespace obl::scenario_load
