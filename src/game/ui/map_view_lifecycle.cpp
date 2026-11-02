#include "game/ui/map_cache_types.h"
#include "game/ui/map_cache.h"
#include "game/ui/map_view.h"

#include "core/log.h"
#include "game/ui/map_view_access.h"
#include "game/ui/map_view_lifecycle.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "game/ui/map_view_blocks.h"

namespace obl::ui {
namespace {

using ui::MapView;
using ui::Renderer;

constexpr size_t kMapCacheBytes = sizeof(ui::MapCache);

class LifecycleEnv {
public:
    void SetInstance(MapView* view) { SetMapView(view); }
    void RenderSize(int& width, int& height)
    {
        const Renderer* renderer = game::GetRenderer();
        width = renderer->m_width;
        height = renderer->m_height;
    }
    void* CreateMapCache(const char* path)
    {
        void* memory = ::operator new(kMapCacheBytes);
        return memory ? static_cast<ui::MapCache*>(memory)->Construct(path) : nullptr;
    }

    void CloseTempFile(void* file) { std::fclose(static_cast<std::FILE*>(file)); }
    void UnmapAllBlocks(MapView& view) { (view).UnmapAllBlocks(); }
    void CloseMapCache(void* mapCache) { static_cast<ui::MapCache*>(mapCache)->Close(); }
    void DeleteMapCache(void* mapCache) { ::operator delete(mapCache); }

    void UpdateCamera(MapView& view) { (view).UpdateCamera(); }
};

} // namespace

MapView* MapView::Construct(void* owner, Renderer* renderer, const char* path)
{
    LifecycleEnv env;
    return ConstructMapView(env, *this, owner, renderer, path);
}

void MapView::Destruct()
{
    LifecycleEnv env;
    DestructMapView(env, *this);
}

void MapView::RestoreAfterLoad(std::span<const int32_t> burnQueue)
{
    LifecycleEnv env;
    m_burnQueue.insert(m_burnQueue.end(), burnQueue.begin(), burnQueue.end());
    env.UpdateCamera(*this);
}

} // namespace obl::ui
