#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/renderer_api.h"
#include "game/ui/sprite_anim.h"
#include "game/ui/blit.h"
#include "game/world/world_messages.h"
#include "game/game/settings_store.h"
#include "game/ui/sprite_instance_preview.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/objects/object_building.h"
#include "game/objects/building_methods.h"
#include "game/objects/building_queue.h"

#include "game/map/grid_state.h"
#include "game/game/game_accessors.h"
#include "game/objects/building_draw.h"
#include "game/construction/construction_manager.h"
#include "game/ui/map_view_access.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/map_view.h"
#include "game/ui/renderer_types.h"
#include "core/rect.h"
#include "core/call_counter.h"
#include "game/ui/sprite_instance_bake.h"
#include "game/ui/map_view_math.h"
#include "game/ui/renderer_ops.h"

#include <bit>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// The screen side of buildings (the logic is building_draw.h): drawing, selection, screen rectangle and cells, the light mask, the
// beach-mode switch of towers and the start of a demolition.
namespace obl::object {
using game::GetGrid;
using game::GetGuiManager;
using game::GetObjectManager;
using game::GetRenderer;
using game::GetWorld;
using ui::GetMapView;

namespace {

using messages::Message;
using object::Building;
using tables::BuildingDef;
using ui::Renderer;
using ui::SpriteAnim;
using ui::SpriteInstance;
using S = SpriteInstance;

test::CallCounter g_draw("objects.building-draw"), g_overlay("objects.building-overlay"), g_lightMask("objects.building-light-mask");

ui::GuiManager* Gui()
{
    return GetGuiManager();
}

// ---- The game side of the Env of building_draw.h ----------------------------------------------------------------------------------------------

struct Env {
    bool Building(int32_t id) { return ProgressIsNonZero(construction::runtime::IsBuilding(GetWorld()->m_constructionManager, id)); }
    bool Demolishing(int32_t id) { return ProgressIsNonZero(construction::runtime::IsDemolishing(GetWorld()->m_constructionManager, id)); }
    float BuildProgress(int32_t id) { return construction::runtime::BuildProgress(GetWorld()->m_constructionManager, id); }
    float DemolishProgress(int32_t id) { return construction::runtime::DemolishProgress(GetWorld()->m_constructionManager, id); }
    float Time() { return GetWorld()->m_gameTime; }
    float DayTime() { return GetWorld()->m_gameDayTime; }
    float LastDayTime() { return GetWorld()->m_previousDayTime; }
    bool LightEffects() { return game::settings_store::Current().m_lightEffects != 0; }

    void WorldToScreen(int32_t* out, const float* world)
    {
        const int* at = GetMapView()->ProjectWorldPoint(out, world);
        out[0] = at[0];
        out[1] = at[1];
    }
    uint32_t Depth(const float* world) { return GetMapView()->GetDepth(world); }
    int32_t DrawOffsetX(S* s) { return s->GetDrawOffsetX(); }
    void ScreenCells(object::Building& b) { b.UpdateScreenCells(b.m_WorldXYZ[0], b.m_WorldXYZ[1], b.m_WorldXYZ[2]); }
    int32_t BlockSize() { return GetMapView()->m_blockSize; }
    bool Intersect(const screen::Rect& a, const screen::Rect& b, screen::Rect& out) { return renderer_ops::IntersectRects(a, b, out); }
    int32_t BuildingCount() { return static_cast<int32_t>(GetObjectManager()->m_buildings.size()); }
    object::Base* BuildingAt(int32_t index) { return GetObjectManager()->m_buildings[index]; }
    void CopyRle(uint8_t* dst, uint8_t* runs, int pitch, int xBytes, int y, int w, int h, screen::Rect* clip)
    {
        ui::pixel_primitives::RleCopy32(dst, runs, pitch, xBytes, y, w, h, *clip);
    }
    void LightMask(object::Building& b, uint8_t* buffer) { b.DrawLightMask(buffer); }
    void Bake(S* s, const float* world, int a, int c) { s->BakeGhost(world, a, c); }
    void Preview(S* s, const float* world, int a, int c, char ghost) { s->DrawBuildingPreview(world, a, c, ghost); }
    void PreviewAlt(S* s, const float* world, int a, int c, float progress, char lower)
    {
        (s)->DrawBuildingPreviewAlt(world, a, c, progress, lower);
    }
    void InWorld(S* s, const float* world, float time) { (s)->DrawInWorld(world, time); }
    void Clipped(S* s, int x, int y, uint32_t depth, float time) { (s)->DrawClipped(x, y, depth, time); }
    bool ClipRect(int* x, int* y, int* l, int* t, int* w, int* h)
    {
        return obl::ui::renderer::ClipRect(GetRenderer(), x, y, l, t, w, h) != 0;
    }
    uint8_t* Frame(SpriteAnim* anim, int frame, int direction) { return (anim)->GetFrame(frame, direction); }
    void Blit(uint8_t* dst, uint8_t* frame, uint32_t palette, int pitch, int x, int y, int w, int h, int l, int t, int cw, int ch)
    {
        ui::pixel_primitives::RleToBytes8(dst, frame, palette, pitch, x, y, w, h, l, t, cw, ch);
    }
    void BlitZoomOut(uint8_t* dst, uint8_t* frame, uint32_t palette, int pitch, int x, int y, int w, int h, int l, int t, int cw, int ch)
    {
        ui::pixel_primitives::RleToBytes8ZoomOut(dst, frame, palette, pitch, x, y, w, h, l, t, cw, ch);
    }
    S* CreateSprite(const char* name)
    {
        return static_cast<S*>((static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(name));
    }
    void ReleaseSprite(S* s) { (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(s); }

    void LightsDirty()
    {
        GetRenderer()->m_lightsDirty = 1;
        GetRenderer()->m_lightsDirtyFrom = 0;
    }
    bool RenderLights() { return GetRenderer()->m_lightsEnabled != 0; }
    int32_t ZoomLevel() { return GetRenderer()->m_zoomLevel; }
    float ZoomScale() { return GetRenderer()->m_zoomScale; }
    int32_t Width() { return GetRenderer()->m_width; }
    int32_t Height() { return GetRenderer()->m_height; }
    uint8_t* LightBuffer() { return GetRenderer()->m_lightBuffer; }
    // The 8-bit RLE blitters never read the palette word.
    uint32_t Palette() { return 0u; }

    void ForgetKey() { Gui()->m_selection = -1; }
    void WorldToInt(float* world, int32_t* x, int32_t* y) { ui::MapView::WorldToInt(world, x, y); }
    void SetCameraPos(int32_t x, int32_t y) { GetMapView()->SetCameraPos(x, y); }
    void StopCamera() { GetMapView()->StopCamera(); }
    int32_t CameraX() { return GetMapView()->m_cameraX; }
    int32_t CameraY() { return GetMapView()->m_cameraY; }
    void ShowDialog(const char* name, object::Building* provider) { Gui()->ShowDialog(name, provider, false); }

    bool AreaOpen(int kind, int area) { return GetGrid()->IsAreaOpen(kind, area); }
    void SetAreaOpen(int kind, int area) { GetGrid()->SetAreaOpen(kind, area); }
    void SetAreaClosed(int kind, int area) { GetGrid()->SetAreaClosed(kind, area); }
    void PostClass(Message& message, int cls) { world::PostMessageToWorldClass(*GetWorld(), message, cls); }
    void PostBuildings(Message& message) { world::PostMessageToBuildings(*GetWorld(), message); }
    void ClearQueue(void* queue) { BuildingQueueClearForClosing(queue); }
    void HideStaff(object::Building& b) { BuildingHideStaffAndEvents(&b); }
    void Send(object::Building& b, const Message& message) { b.Send(message); }
};

} // namespace

void Building::Draw()
{
    g_draw.Count();
    Env env;
    building_draw::Draw(env, *this);
}
void Building::DrawOverlay()
{
    g_overlay.Count();
    Env env;
    building_draw::DrawOverlay(env, *this);
}
void Building::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    Env env;
    building_draw::OnSelect(env, *this, clicked != 0, centerCamera != 0);
}
void* Building::UpdateScreenRect()
{
    Env env;
    return building_draw::UpdateScreenRect(env, *this);
}
void Building::SetBeachOpenMode(int32_t mode)
{
    Env env;
    building_draw::SetBeachOpenMode(env, *this, mode);
}
void Building::BeginDemolish()
{
    Env env;
    building_draw::BeginDemolish(env, *this);
}
bool Building::IsBeachOpenMode(int32_t mode)
{
    Env env;
    return building_draw::IsBeachOpenMode(env, m_InfluenceArea, mode) != 0;
}
void Building::UpdateScreenCells(float x, float y, float z)
{
    Env env;
    building_draw::UpdateScreenCells(env, *this, x, y, z);
}
void Building::DrawLightMask(uint8_t* lightBuffer)
{
    g_lightMask.Count();
    Env env;
    building_draw::DrawLightMask(env, *this, lightBuffer);
}
} // namespace obl::object
