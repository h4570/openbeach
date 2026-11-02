#include "game/map/grid_lifecycle.h"

#include "core/call_counter.h"
#include "core/char16.h"
#include "core/pack_path.h"
#include "core/wide_path.h"
#include "game/game/diagnostics.h"
#include "game/game/game_accessors.h"
#include "game/map/grid_cell.h"
#include "game/map/grid_state.h"
#include "formats/map_pack.h"
#include "game/ui/map_view.h"
#include "game/ui/map_view_math.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace obl::map {
namespace {

test::CallCounter g_lifecycleCalls("map.grid-lifecycle");
std::filesystem::path g_packDirectory;

// The game side of the Env contract (grid_lifecycle.h).
class GameEnv {
public:
    void SetGlobalGrid(Grid* grid) { game::SetGrid(grid); }
    // The map's metadata says whether it has a height map; a missing or unreadable map.json stops the game like missing terrain.
    bool HasHeightMap(const char* gridPath)
    {
        bool has = false;
        std::string error;
        if (!map_pack::HasHeightMap(g_packDirectory, gridPath, has, error)) {
            game::FatalError("%s", error.c_str());
            return false;
        }
        return has;
    }
    void RememberHeightMapName(const char* path) { game::GetContext().heightMapPathCopy = path; }
    bool LoadHeightMap(const char* gridPath, map_pack::HeightSamples& out)
    {
        std::string error;
        if (!map_pack::ReadHeightSamples(g_packDirectory, gridPath, out, error)) {
            game::FatalError("%s", error.c_str());
            return false;
        }
        return true;
    }
    // The terrain comes from the converted pack only; a missing or refused file stops the game (docs/build/game-data.md, "No fallback").
    const uint32_t* ReadGridFile(const char* path)
    {
        std::vector<uint32_t> words;
        std::string error;
        if (!map_pack::ReadTerrainWords(g_packDirectory, path, words, error)) {
            game::FatalError("%s", error.c_str());
            return nullptr;
        }
        auto* block = new uint32_t[words.size()];
        std::memcpy(block, words.data(), words.size() * sizeof(uint32_t));
        return block;
    }
    void FreeGridFile(const uint32_t* words) { delete[] words; }
    void ScreenToWorld(const int32_t screen[2], float world[3]) { ui::GetMapView()->ScreenToWorldPoint(world, screen); }
    void WorldToTile(const float world[3], int32_t tile[2]) { ui::MapView::WorldToTile(tile, world); }
    uint32_t FogStride() { return static_cast<uint32_t>(ui::GetMapView()->m_blocksPerColumn); }
    void MarkFog(uint32_t index) { ui::GetMapView()->m_blocks[index].m_usesWaterMask = 1; }
};

} // namespace

Grid* CreateGrid(uint32_t width, uint32_t height, int32_t tileSize, const char* heightMapPath, float heightScale, const char* gridFilePath)
{
    g_lifecycleCalls.Count();
    auto* grid = new Grid;
    GameEnv env;
    grid_lifecycle::ConstructGrid(*grid, env, width, height, tileSize, heightMapPath, heightScale, gridFilePath);
    return grid;
}

void DestroyGrid(Grid* grid)
{
    if (!grid) return;
    g_lifecycleCalls.Count();
    GameEnv env;
    grid_lifecycle::DestructGrid(*grid, env);
    delete grid;
}

void SetPackDirectory(const Text16& gameDirectory)
{
    g_packDirectory = WidePath(gameDirectory) / pack_path::Selected();
}

const std::filesystem::path& PackDirectory()
{
    return g_packDirectory;
}

} // namespace obl::map
