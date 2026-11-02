#pragma once

#include "game/world/world_state.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// ConstructWorld step 2: what the constructor reads from the scenario's `.pis`.
// The reads are interleaved with the subsystem construction (header keys -> texts -> subsystems -> MESSAGE_<n>/BUILDING_<n> ->
// script), so the parts are separate functions the assembling function calls in that order. All of them are pure logic over an `Env`:
//   const char* GetString(const char* key)      null when the key is absent (or has the wrong type)
//   const float* GetNumber(const char* key)     null likewise
//   std::string Duplicate(const char* text)     the world's own copy of the text (a new allocation and a copy)
//   [[noreturn]] void MissingKey(const char* key)   FatalError("no <key> in world script ..."): ends the process in the game
//   int BuildingId(const char* name)            world_params::GetBuildingId(world params, name)
//   void AddScenarioMessage(const char* text)   feedback::Feedback::AddScenarioMessage(world.m_feedbackManager, text)
// An optional key that is read without a check (a null string would go straight into strlen) is not guarded here either: `Duplicate` gets
// the null and the game side decides (the shipped scenarios define all of them).
namespace obl::world_construct_pis {

// The keys of the first block, in the order they are read. The first eleven are required; the rest are optional (null).
struct Header {
    const char* name; // WORLD_NAME
    const char* mapFile; // WORLD_MAPFILENAME
    const char* script; // WORLD_SCRIPT
    const char* buildingList; // WORLD_BUILDINGLIST
    const char* guestList; // WORLD_GUESTLIST
    const char* heightmap; // WORLD_MAPHEIGHTMAP
    const char* grid; // WORLD_MAPGRID
    const char* pathTileInfo; // WORLD_PATHTILEINFO
    const char* waterAnim; // WORLD_WATERANIM (optional)
    const char* pavementTileset; // WORLD_PAVEMENT_TILESET (optional)
    const char* beachTileset; // WORLD_BEACH_EDGE_TILESET (optional)
    const float* startMapX; // STARTMAPX
    const float* startMapY; // STARTMAPY
    const float* heightScale; // WORLD_HEIGHTMAPSCALE
    const float* depthPlane; // WORLD_DEPTH_PLANE_OFFSET (optional)
    const float* depthSlant; // WORLD_DEPTH_SLANT_OFFSET (optional)
};

// Reads the header keys and checks the required ones in this order (name, map file, heightmap, grid, path tiles, start X, start Y,
// script, building list, guest list, height scale): the first missing one is the one `MissingKey` reports.
template<class Env> Header ReadHeader(Env& env)
{
    Header h{};
    h.name = env.GetString("WORLD_NAME");
    h.mapFile = env.GetString("WORLD_MAPFILENAME");
    h.script = env.GetString("WORLD_SCRIPT");
    h.buildingList = env.GetString("WORLD_BUILDINGLIST");
    h.guestList = env.GetString("WORLD_GUESTLIST");
    h.heightmap = env.GetString("WORLD_MAPHEIGHTMAP");
    h.grid = env.GetString("WORLD_MAPGRID");
    h.pathTileInfo = env.GetString("WORLD_PATHTILEINFO");
    h.waterAnim = env.GetString("WORLD_WATERANIM");
    h.pavementTileset = env.GetString("WORLD_PAVEMENT_TILESET");
    h.beachTileset = env.GetString("WORLD_BEACH_EDGE_TILESET");
    h.startMapX = env.GetNumber("STARTMAPX");
    h.startMapY = env.GetNumber("STARTMAPY");
    h.heightScale = env.GetNumber("WORLD_HEIGHTMAPSCALE");
    h.depthPlane = env.GetNumber("WORLD_DEPTH_PLANE_OFFSET");
    h.depthSlant = env.GetNumber("WORLD_DEPTH_SLANT_OFFSET");
    if (!h.name) env.MissingKey("WORLD_NAME");
    if (!h.mapFile) env.MissingKey("WORLD_MAPFILENAME");
    if (!h.heightmap) env.MissingKey("WORLD_MAPHEIGHTMAP");
    if (!h.grid) env.MissingKey("WORLD_MAPGRID");
    if (!h.pathTileInfo) env.MissingKey("WORLD_PATHTILEINFO");
    if (!h.startMapX) env.MissingKey("STARTMAPX");
    if (!h.startMapY) env.MissingKey("STARTMAPY");
    if (!h.script) env.MissingKey("WORLD_SCRIPT");
    if (!h.buildingList) env.MissingKey("WORLD_BUILDINGLIST");
    if (!h.guestList) env.MissingKey("WORLD_GUESTLIST");
    if (!h.heightScale) env.MissingKey("WORLD_HEIGHTMAPSCALE");
    return h;
}

// The texts handed to FatalError, already formatted (the message buffer is a fixed 256 bytes; here the text is cut to
// the caller's buffer): eleven separate strings, one per required key, all of the shape "no <KEY> in world script - <pis path>",
// and the one of the water animation that does not load.
inline void MissingKeyMessage(char* out, size_t size, const char* key, const char* pisPath)
{
    // Truncation to the caller's buffer is the documented behavior (a test hands in a 3-byte one); GCC proves the size once this is inlined there.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
    std::snprintf(out, size, "no %s in world script - %s", key, pisPath);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}
inline void WaterAnimMessage(char* out, size_t size, const char* waterAnim)
{
    std::snprintf(out, size, "Couldn't load water animation - %s", waterAnim);
}

// The map id: a copy into the fixed-size field (the field's size is the bound here,
// a longer id is cut and terminated rather than overflowing into the fields after it).
inline void CopyMapId(world::World& world, const char* mapId)
{
    const size_t length = std::strlen(mapId);
    const size_t n = length < sizeof(world.m_mapId) - 1 ? length : sizeof(world.m_mapId) - 1;
    std::memcpy(world.m_mapId, mapId, n);
    world.m_mapId[n] = '\0';
}

// Float to int by truncation toward zero, with the optional-key defaults of the depth offsets: the plane offset 0, the slant offset 16.
inline int DepthPlaneOffset(const float* value)
{
    return value ? static_cast<int>(*value) : 0;
}
inline int DepthSlantOffset(const float* value)
{
    return value ? static_cast<int>(*value) : 0x10;
}

// The texts after the water animation: island status (WORLD_STATUS_*), the scenario description/objectives, SCENARIO_OBJ_NUMBER and
// SCENARIO_OBJ_<n>, SCENARIO_OBJ_TITLE. The copies are text-table keys the GUI looks up later. The four objective slots (text and status) are
// cleared first; the loop over `number` entries stops at the four slots the layout has (the shipped
// scenarios use at most four). A missing SCENARIO_OBJ_NUMBER counts as 0.
template<class Env> void ReadTexts(world::World& world, Env& env)
{
    world.m_islandTitle = env.Duplicate(env.GetString("WORLD_STATUS_TITLE"));
    world.m_islandArea = env.Duplicate(env.GetString("WORLD_STATUS_AREA"));
    world.m_islandWeather = env.Duplicate(env.GetString("WORLD_STATUS_WEATHER"));
    world.m_islandTemp = env.Duplicate(env.GetString("WORLD_STATUS_TEMP"));
    world.m_islandHazards = env.Duplicate(env.GetString("WORLD_STATUS_HAZARDS"));
    world.m_scenarioDescription = env.Duplicate(env.GetString("SCENARIO_OBJ_DESCRIPTION"));
    world.m_scenarioObjectives = env.Duplicate(env.GetString("SCENARIO_OBJ_OBJECTIVES"));
    const float* number = env.GetNumber("SCENARIO_OBJ_NUMBER");
    world.m_numObjectives = number ? static_cast<uint32_t>(static_cast<int>(*number)) : 0;
    for (int i = 0; i < 4; ++i) {
        world.m_objectiveText[i].clear();
        world.m_objectiveStatus[i] = 0;
    }
    for (uint32_t i = 0; i < world.m_numObjectives && i < 4; ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "SCENARIO_OBJ_%d", static_cast<int>(i));
        world.m_objectiveText[i] = env.Duplicate(env.GetString(key));
    }
    world.m_scenarioTitle = env.Duplicate(env.GetString("SCENARIO_OBJ_TITLE"));
}

// After the subsystems exist: MESSAGE_0, MESSAGE_1, ... go to the feedback manager until the first missing number.
template<class Env> void ReadMessages(Env& env)
{
    for (int i = 0;; ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "MESSAGE_%d", i);
        const char* text = env.GetString(key);
        if (!text) break;
        env.AddScenarioMessage(text);
    }
}

// Read numbered BUILDING keys into the cleared availability list. Its 128-entry capacity bounds the count;
// additional keys are ignored even if the scenario supplies more.
template<class Env> void ReadBuildings(world::World& world, Env& env)
{
    world.m_numValidBuildings = 0;
    std::memset(world.m_validBuildingIDs, 0, sizeof(world.m_validBuildingIDs));
    for (int i = 0;; ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "BUILDING_%d", i);
        const char* name = env.GetString(key);
        if (!name) break;
        if (world.m_numValidBuildings < 0x80) {
            world.m_validBuildingIDs[world.m_numValidBuildings] = env.BuildingId(name);
            ++world.m_numValidBuildings;
        }
    }
}

// The loading-bar fraction after the script: `scriptResult` is what WorldScriptHost::LoadScript returned (an int, a work count); a loaded save
// shows 0.5 and a new game 1 - n * 6.873372e-06 * 0.5 (the game's float constants; the product is computed in double
// precision, like wider registers), never below 0.5. It feeds MapView::BuildTempMap and StartNewGame(1 - f).
inline float LoadFraction(int scriptResult, bool loadingSave)
{
    if (loadingSave) return 0.5f;
    const double product = static_cast<double>(scriptResult) * 6.873372058180394e-06 * 0.5;
    const float fraction = static_cast<float>(1.0 - product);
    return fraction < 0.5f ? 0.5f : fraction;
}

} // namespace obl::world_construct_pis
