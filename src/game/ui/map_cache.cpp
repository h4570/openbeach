#include "core/char16.h"
#include "core/pack_path.h"
#include "game/ui/map_cache.h"

#include "game/game/game_accessors.h"
#include "core/config.h"
#include "game/ui/map_cache_types.h"
#include "game/ui/renderer_types.h"
#include "formats/map_pack.h"
#include "core/call_counter.h"
#include "core/wide_path.h"
#include "game/game/diagnostics.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace obl::ui {
namespace {

using ui::MapCache;
namespace cache = ui::map_cache;

test::CallCounter g_callsAny("ui.map-cache");

std::filesystem::path g_packDirectory;

struct IslandCache {
    std::string id;
    map_pack::IslandPlanes planes;
    bool loaded = false;
    std::string error;
};
IslandCache g_island;

struct GameEnv {
    bool Open(const char* path)
    {
        const std::string id = map_pack::BizMapId(path);
        if (id.empty()) {
            g_island.error = std::string(path) + ": not a map .biz path";
            return false;
        }
        if (g_island.loaded && g_island.id == id) return true;
        g_island = IslandCache{};
        if (!map_pack::ReadIslandPlanes(g_packDirectory, id, g_island.planes, g_island.error)) {
            g_island = IslandCache{};
            return false;
        }
        g_island.id = id;
        g_island.loaded = true;
        return true;
    }
    void CouldNotOpen(const char* path) { game::FatalError("Couldn't load the island images of %s. %s", path, g_island.error.c_str()); }
    const map_pack::IslandPlanes* Planes() { return g_island.loaded ? &g_island.planes : nullptr; }
    void Close() { g_island = IslandCache{}; }
    bool Rgb555() { return game::GetRenderer()->m_rgb555 == 1; }
};

struct HeadlessEnv {
    bool Open(const char*) { return true; }
    void CouldNotOpen(const char*) {}
    const map_pack::IslandPlanes* Planes() { return nullptr; }
    void Close() {}
    bool Rgb555() { return false; }
};

template<class F> auto WithEnv(F&& body)
{
    g_callsAny.Count();
    if (config::Get().headless) {
        HeadlessEnv env;
        return body(env);
    }
    GameEnv env;
    return body(env);
}

} // namespace

MapCache* MapCache::Construct(const char* path)
{
    if (config::Get().headless) std::memset(this, 0, sizeof(MapCache));
    return WithEnv([&](auto& env) { return cache::Construct(*this, path, env); });
}
void MapCache::Close()
{
    WithEnv([&](auto& env) { cache::Close(*this, env); });
}
const uint16_t* MapCache::GetDepthBlock(uint32_t x, uint32_t y)
{
    return WithEnv([&](auto& env) { return cache::GetBlock<true>(*this, x, y, env); });
}
const uint16_t* MapCache::GetColourBlock(uint32_t x, uint32_t y)
{
    return WithEnv([&](auto& env) { return cache::GetBlock<false>(*this, x, y, env); });
}
void MapCache::LoadColumn(int column, void* destination)
{
    WithEnv([&](auto& env) { cache::LoadColumn(*this, column, static_cast<uint16_t*>(destination), env); });
}

bool SetMapPackDirectory(const Text16& gameDirectory)
{
    g_packDirectory = WidePath(gameDirectory) / pack_path::Selected();
    return true;
}

} // namespace obl::ui
