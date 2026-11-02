#include "unit_test.h"

#include "game/ui/map_view_lifecycle.h"

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using ui::MapView;

namespace {

struct Env {
    std::vector<std::string> calls;
    MapView* instance = nullptr;
    int renderWidth = 800, renderHeight = 600;
    int mapCacheAllocations = 0;
    std::array<uint8_t, 4> mapCache{};

    void SetInstance(MapView* view)
    {
        instance = view;
        calls.push_back(view ? "instance set" : "instance cleared");
    }
    void RenderSize(int& width, int& height)
    {
        width = renderWidth;
        height = renderHeight;
        calls.push_back("render size");
    }
    void* CreateMapCache(const char* path)
    {
        ++mapCacheAllocations;
        calls.push_back(std::string("bizmap ") + path);
        return mapCache.data();
    }
    void CloseTempFile(void*) { calls.push_back("close temp file"); }
    void UnmapAllBlocks(MapView&) { calls.push_back("unmap blocks"); }
    void CloseMapCache(void*) { calls.push_back("close bizmap"); }
    void DeleteMapCache(void*) { calls.push_back("delete bizmap"); }
};

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

} // namespace

OBL_TEST_CASE("map_view_lifecycle.construct_sets_the_view")
{
    Env env;
    env.renderWidth = 1024;
    env.renderHeight = 768;
    MapView view;
    view.m_blocksPerRow = view.m_blocksPerColumn = static_cast<int32_t>(0xcdcdcdcdu);
    int owner = 0, renderer = 0;
    MapView* result = ConstructMapView(env, view, &owner, reinterpret_cast<ui::Renderer*>(&renderer), "biz");
    OBL_CHECK(result == &view);
    OBL_CHECK(env.instance == &view);
    OBL_CHECK(view.m_owner == &owner);
    OBL_CHECK(view.m_renderer == reinterpret_cast<ui::Renderer*>(&renderer));
    OBL_CHECK_EQ(view.m_viewLeft, 0);
    OBL_CHECK_EQ(view.m_viewTop, 0);
    OBL_CHECK_EQ(view.m_viewWidth, 1024);
    OBL_CHECK_EQ(view.m_viewHeight, 668);
    OBL_CHECK_EQ(view.m_reuseTempMap, 1);
    OBL_CHECK_EQ(view.m_depthSlantOffset, 0x10);
    OBL_CHECK_EQ(view.m_depthPlaneOffset, 0);
    OBL_CHECK_EQ(view.m_guestObjectsDrawn, 0);
    OBL_CHECK_EQ(view.m_blocksMapped, 0);
    OBL_CHECK_EQ(view.m_mapLocked, 0);
    OBL_CHECK_EQ(view.m_movingMap, 0);
    OBL_CHECK(view.m_blocks == nullptr);
    OBL_CHECK(view.m_mappedFile == nullptr);
    OBL_CHECK(view.m_tempFile == nullptr);
    OBL_CHECK_EQ(view.m_mapWidth, 0);
    OBL_CHECK_EQ(view.m_blockSize, 0);
    OBL_CHECK_EQ(Bits(view.m_cameraPositionX), 0u);
    OBL_CHECK_EQ(Bits(view.m_lastUnBurnTime), 0u);
    OBL_CHECK(view.m_mapCache == env.mapCache.data());
    OBL_CHECK(view.m_burnQueue.empty());

    OBL_CHECK_EQ(static_cast<uint32_t>(view.m_blocksPerRow), 0xcdcdcdcdu);
    OBL_CHECK_EQ(static_cast<uint32_t>(view.m_blocksPerColumn), 0xcdcdcdcdu);
    OBL_CHECK(env.calls == std::vector<std::string>({"instance set", "render size", "bizmap biz"}));
}

OBL_TEST_CASE("map_view_lifecycle.destruct_returns_what_the_view_owns")
{
    Env env;
    MapView view{};
    int file = 0;
    view.m_tempFile = &file;
    view.m_blocksMapped = 1;
    view.m_mapCache = env.mapCache.data();
    DestructMapView(env, view);
    OBL_CHECK(env.calls ==
            std::vector<std::string>({"close temp file", "unmap blocks", "close bizmap", "delete bizmap", "instance cleared"}));
    OBL_CHECK(env.instance == nullptr);

    Env bare;
    MapView empty{};
    DestructMapView(bare, empty);
    OBL_CHECK(bare.calls == std::vector<std::string>({"instance cleared"}));
}
