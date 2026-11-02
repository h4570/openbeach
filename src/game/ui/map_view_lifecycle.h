#pragma once

#include "game/ui/map_view.h"

#include <cstdint>

namespace obl {

template<class Env> ui::MapView* ConstructMapView(Env& env, ui::MapView& view, void* owner, ui::Renderer* renderer, const char* mapPath)
{
    env.SetInstance(&view);
    view.m_owner = owner;
    view.m_renderer = renderer;
    view.m_tempFile = nullptr;
    view.m_blocksMapped = 0;
    view.m_cameraX = 0;
    view.m_cameraY = 0;
    view.m_mappedFile = nullptr;
    view.m_mapWidth = 0;
    view.m_mapHeight = 0;
    view.m_blockSize = 0;
    view.m_cameraPositionX = 0;
    view.m_cameraPositionY = 0;
    view.m_velocityX = 0;
    view.m_velocityY = 0;
    view.m_lastInputX = 0;
    view.m_lastInputY = 0;
    view.m_blocks = nullptr;
    view.m_lastUnBurnTime = 0;
    view.m_viewLeft = 0;
    view.m_viewTop = 0;
    int renderWidth = 0, renderHeight = 0;
    env.RenderSize(renderWidth, renderHeight);
    view.m_viewWidth = renderWidth;
    view.m_viewHeight = renderHeight - 100;
    view.m_reuseTempMap = 1;
    view.m_mapLocked = 0;
    view.m_movingMap = 0;
    view.m_moveMapDuration = 0;
    view.m_moveMapStartTime = 0;
    view.m_moveMapStartX = 0;
    view.m_moveMapStartY = 0;
    view.m_moveMapDestX = 0;
    view.m_moveMapDestY = 0;
    view.m_mapCache = env.CreateMapCache(mapPath);
    view.m_burnQueue.clear();
    view.m_guestObjectsDrawn = 0;
    view.m_depthPlaneOffset = 0;
    view.m_depthSlantOffset = 0x10;
    return &view;
}

template<class Env> void DestructMapView(Env& env, ui::MapView& view)
{
    if (view.m_tempFile) env.CloseTempFile(view.m_tempFile);
    if (view.m_blocksMapped) env.UnmapAllBlocks(view);
    if (view.m_mapCache) {
        env.CloseMapCache(view.m_mapCache);
        env.DeleteMapCache(view.m_mapCache);
    }
    env.SetInstance(nullptr);
}

} // namespace obl
