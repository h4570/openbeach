#include "game/ui/map_cache.h"
#include "game/ui/renderer_api.h"
#include "game/ui/map_view_blocks.h"

#include "core/config.h"
#include "core/log.h"
#include "game/ui/map_blocks.h"
#include "game/ui/memory_mapping.h"
#include "game/ui/map_view_access.h"
#include "core/watchdog.h"
#include "game/game/diagnostics.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace obl::ui {
namespace {

using ui::MapBlock;
using ui::MapView;

constexpr int kFixedTableSide = 0x20;

MapBlock& Block(MapView* view, int x, int y)
{
    return view->m_blocks[map_blocks::BlockIndex(view->m_blocksPerRow, x, y)];
}

bool LoadedByColumnStride(MapView* view, int x, int y)
{
    return view->m_blocks[map_blocks::BlockIndex(view->m_blocksPerColumn, x, y)].m_loaded != 0;
}

void* HeadlessBlockView(uint32_t size)
{
    static std::vector<uint32_t> scratch;
    if (scratch.size() < size / sizeof(uint32_t)) scratch.resize(size / sizeof(uint32_t));
    return scratch.data();
}

struct MapCache {
    void* mapping = nullptr;
    uint32_t size = 0;
    ~MapCache()
    {
        if (mapping) ui::CloseMemoryMapping(mapping);
    }
};
MapCache g_mapCache;

void* MapBlockView(MapView* view, int x, int y)
{
    const map_blocks::BlockSpan span = map_blocks::SpanOf(view->m_blocksPerRow, view->m_blockSize, x, y);
    if (config::Get().headless) return HeadlessBlockView(span.size);
    return ui::MapMemoryView(view->m_mappedFile, ui::MappingAccess::All, span.offset, span.size);
}

void UnmapBlockView(MapView*, void* mapped)
{
    if (config::Get().headless) return;
    ui::UnmapMemoryView(mapped);
}

void LoadBlockImpl(MapView* view, int x, int y)
{
    Block(view, x, y).m_loaded = 1;
    void* mapped = MapBlockView(view, x, y);
    Block(view, x, y).m_pixels = static_cast<uint32_t*>(mapped);
}

void UnmapBlockAt(MapView* view, int x, int y)
{
    MapBlock& block = Block(view, x, y);
    if (block.m_loaded != 0 && block.m_pixels != nullptr) {
        UnmapBlockView(view, block.m_pixels);
        Block(view, x, y).m_pixels = nullptr;
    }
    Block(view, x, y).m_loaded = 0;
}

void UnmapFixedTable(MapView* view)
{
    for (int y = 0; y < kFixedTableSide; ++y)
        for (int x = 0; x < kFixedTableSide; ++x) UnmapBlockAt(view, x, y);
}

bool BuildMapCache(MapView* view, float fade)
{
    WatchdogLongCommand building(true);
    const int blocksPerRow = view->m_blocksPerRow, blocksPerColumn = view->m_blocksPerColumn, blockSize = view->m_blockSize;
    const size_t pixels = static_cast<size_t>(static_cast<uint32_t>(blockSize) * static_cast<uint32_t>(blockSize));

    const size_t rowBytes = pixels * map_blocks::kTempMapRowBlocks * map_blocks::kBytesPerPixel;
    std::vector<uint16_t> row(std::max(rowBytes, pixels * static_cast<size_t>(std::max(blocksPerRow, 0)) * map_blocks::kBytesPerPixel) / 2);
    std::vector<uint8_t> column(4 * 1024 * 1024);
    const int total = blocksPerColumn * blocksPerRow;
    uint32_t done = 0;
    for (int y = 0; y < blocksPerColumn; ++y) {
        static_cast<ui::MapCache*>(view->m_mapCache)->LoadColumn(y, column.data());
        for (int x = 0; x < blocksPerRow; ++x) {
            map_blocks::InterleaveColumnRow(column.data() + static_cast<size_t>(x) * map_blocks::kColumnBlockBytes, 1, pixels,
                    row.data() + static_cast<size_t>(x) * pixels * 2);
            if ((++done & 7) == 0) ui::renderer::DrawLoadingBar(game::GetRenderer(), map_blocks::TempMapProgress(done, total, fade));
        }
        void* mapped = ui::MapMemoryView(g_mapCache.mapping, ui::MappingAccess::Write, static_cast<uint32_t>(y * rowBytes), rowBytes);
        if (!mapped) return false;
        std::memcpy(mapped, row.data(), rowBytes);
        ui::UnmapMemoryView(mapped);
    }
    return true;
}

bool BuildTempMapImpl(MapView* view, int blocksPerRow, int blocksPerColumn, int blockSize, float fade)
{
    if (view->m_blocksMapped) view->UnmapAllBlocks();
    view->m_mapWidth = 0x1000;
    view->m_mapHeight = 0x1000;
    view->m_blocksPerRow = blocksPerRow;
    view->m_blocksPerColumn = blocksPerColumn;
    view->m_blockSize = blockSize;
    view->m_blocks = static_cast<MapBlock*>(::operator new(static_cast<uint32_t>(blocksPerRow * blocksPerColumn) * sizeof(MapBlock)));

    for (int y = 0; y < kFixedTableSide; ++y) {
        for (int x = 0; x < kFixedTableSide; ++x) {
            MapBlock& block = Block(view, x, y);
            block.m_loaded = 0;
            block.m_pixels = nullptr;
            block.m_usesWaterMask = 0;
        }
    }
    if (config::Get().headless) {
        view->m_mappedFile = nullptr;
        view->m_blocksMapped = 1;
        return true;
    }
    const uint32_t bytes = static_cast<uint32_t>(blocksPerColumn) * map_blocks::kTempMapRowBlocks * static_cast<uint32_t>(blockSize) *
            static_cast<uint32_t>(blockSize) * map_blocks::kBytesPerPixel;
    if (!g_mapCache.mapping || !view->m_reuseTempMap || g_mapCache.size != bytes) {
        if (g_mapCache.mapping) ui::CloseMemoryMapping(g_mapCache.mapping);
        g_mapCache.mapping = ui::CreateMemoryMapping(bytes);
        g_mapCache.size = bytes;
        if (!g_mapCache.mapping || !BuildMapCache(view, fade)) {
            if (g_mapCache.mapping) ui::CloseMemoryMapping(g_mapCache.mapping);
            g_mapCache.mapping = nullptr;
            game::FatalError("Could not build the in-memory map cache");
            return false;
        }
    }
    OBL_LOG("Map cache: unnamed pagefile mapping (%u bytes)", bytes);
    view->m_mappedFile = g_mapCache.mapping;
    view->m_blocksMapped = 1;
    return true;
}

std::atomic<bool> g_fadeSeen{false}, g_fadeLoadingSave{false};
std::atomic<float> g_startupFade{0.0f};

void RecordStartupFade(float fade)
{
    const world_data::World* world = game::GetWorld();
    if (!world || !world->m_startup) return;
    g_startupFade = fade;
    g_fadeLoadingSave = world->m_loadingSave != 0;
    g_fadeSeen = true;
}

} // namespace

bool MapView::BuildTempMap(int blocksPerRow, int blocksPerColumn, int blockSize, float fade)
{
    RecordStartupFade(fade);

    return BuildTempMapImpl(this, blocksPerRow, blocksPerColumn, blockSize, fade);
}

void MapView::LoadBlock(int x, int y)
{
    LoadBlockImpl(this, x, y);
}

void MapView::LoadBlockPair(uint32_t x, uint32_t y)
{
    const int bx = static_cast<int>(x), by = static_cast<int>(y);
    if (!LoadedByColumnStride(this, bx, by)) LoadBlockImpl(this, bx, by);
    if (Block(this, bx, by).m_loaded == 0) LoadBlockImpl(this, bx, by);
    auto* out = Block(this, bx, by).m_pixels;
    const auto* depth = static_cast<ui::MapCache*>(m_mapCache)->GetDepthBlock(x, y);
    const auto* colour = static_cast<ui::MapCache*>(m_mapCache)->GetColourBlock(x, y);
    const int pixels = m_blockSize * m_blockSize;
    for (int i = 0; i < pixels; ++i) out[i] = static_cast<uint32_t>(colour[i]) | (static_cast<uint32_t>(depth[i]) << 16);
}

void MapView::UnmapBlocks()
{
    UnmapFixedTable(this);
}

bool MapView::UnmapAllBlocks()
{
    UnmapFixedTable(this);
    m_mappedFile = nullptr;
    m_cameraX = 0;
    m_cameraY = 0;
    m_mapWidth = 0;
    m_mapHeight = 0;
    m_blockSize = 0;
    m_cameraPositionX = 0;
    m_cameraPositionY = 0;
    m_velocityX = 0;
    m_velocityY = 0;
    m_blocksMapped = 0;
    ::operator delete(m_blocks);
    return true;
}

void MapView::UnloadFarBlocks()
{
    const map_blocks::BlockRange range = map_blocks::ViewBlockRange(m_cameraX, m_cameraY, m_blockSize, static_cast<uint32_t>(m_viewWidth),
            static_cast<uint32_t>(m_viewHeight), m_renderer->m_zoomLevel);
    for (int y = 0; y < m_blocksPerColumn; ++y) {
        for (int x = 0; x < m_blocksPerRow; ++x) {
            if (map_blocks::IsOutside(range, x, y)) {
                if (LoadedByColumnStride(this, x, y)) UnmapBlockAt(this, x, y);
            } else if (!LoadedByColumnStride(this, x, y)) {
                LoadBlockImpl(this, x, y);
            }
        }
    }
}

bool GetStartupFade(float& fade, bool& loadingSave)
{
    fade = g_startupFade;
    loadingSave = g_fadeLoadingSave;
    return g_fadeSeen;
}

} // namespace obl::ui
