#include "game/ui/renderer_api.h"
#include "game/ui/sound_api.h"
#include "game/ui/map_view.h"

#include "core/fatal.h"
#include "game/ui/sound_types.h"
#include "core/log.h"
#include "game/ui/map_camera.h"
#include "game/ui/map_view_access.h"
#include "game/ui/map_view_math.h"

#include <cstdint>
#include "map_view_blocks.h"

namespace obl::ui {
namespace {

using ui::MapView;
using ui::Renderer;

map_camera::Screen ScreenOf(const Renderer* renderer)
{
    if (!renderer) return {};
    return {static_cast<uint32_t>(renderer->m_width), static_cast<uint32_t>(renderer->m_height), renderer->m_zoomLevel,
            renderer->m_zoomScale, renderer->m_bottomTrim};
}

int ZoomOf(const MapView* view)
{
    return view->m_renderer ? view->m_renderer->m_zoomLevel : -1;
}
float GameTime()
{
    const layout::World* world = game::GetWorld();
    return world ? world->m_gameTime : 0.0f;
}

void ClearLightBuffer()
{
    ui::renderer::ClearLightBuffer(game::GetRenderer());
}

void FinishFrame(MapView* view)
{
    view->UnloadFarBlocks();
    view->ProcessRebuildQueue(GameTime());
    ui::renderer::UpdateWaterAnim(view->m_renderer, GameTime());
    map_camera::EndFrame(*view);
}

void ApplyFrame(const map_camera::Frame& frame)
{
    if (frame.listener) {
        float position[3];
        GetMapView()->ScreenToWorldPoint(position, frame.centre);
        if (auto* sound = audio::CurrentSound()) sound->SetListenerPosition(position);
    }
    if (frame.redraw) {
        Renderer* renderer = game::GetRenderer();
        renderer->m_lightsDirty = 1;
        renderer->m_lightsDirtyFrom = 0;
    }
}

} // namespace

void MapView::ClampCamera()
{
    map_camera::Clamp(*this, ZoomOf(this));
}

void MapView::AddCameraVelocity(float x, float y)
{
    map_camera::AddVelocity(*this, x, y);
}

void MapView::SetCameraPos(int x, int y)
{
    ClearLightBuffer();
    map_camera::SetPosition(*this, ZoomOf(this), x, y);
}

void MapView::ScrollToWorldPoint(float x, float y, float unused, float seconds)
{
    const TerrainView terrain(this);
    if (map_camera::ScrollTo(*this, ScreenOf(game::GetRenderer()), terrain.terrain, x, y, seconds, GameTime())) ClearLightBuffer();
}

bool MapView::UpdateCamera()
{
    if (!game::GetWorld()) fatal::UnhandledInput("MapView::UpdateCamera", "a call without a world");

    ApplyFrame(map_camera::Update(*this, ScreenOf(m_renderer), GameTime()));
    FinishFrame(this);
    return true;
}

void MapView::LockCamera()
{
    map_camera::Lock(*this);
}

void MapView::UnlockCamera()
{
    map_camera::Unlock(*this);
}

void MapView::StopCamera()
{
    map_camera::Stop(*this);
}

void MapView::SetViewportSize(int width, int height)
{
    map_camera::SetViewportSize(*this, ScreenOf(game::GetRenderer()), width, height);
}

void MapView::ResetViewport()
{
    map_camera::ResetViewport(*this, ScreenOf(game::GetRenderer()));
    ui::renderer::SetViewport(m_renderer, &m_viewLeft);
}

void MapView::SetDepthPlaneOffset(int value)
{
    m_depthPlaneOffset = value;
}

void MapView::SetDepthSlantOffset(int value)
{
    m_depthSlantOffset = value;
}

uint32_t MapView::LoadWaterAnim(const char* path)
{
    return ui::renderer::LoadWaterAnim(m_renderer, path);
}

} // namespace obl::ui
