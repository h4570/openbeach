#include "game/ui/renderer_api.h"
#include "game/ui/line_draw.h"
#include "game/objects/guest.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/game/game_accessors.h"
#include "game/objects/guest_shadow.h"
#include "game/ui/map_view_access.h"
#include "game/objects/guest_draw.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "core/log.h"

namespace obl::object {
using game::GetContext;
using game::GetGuiManager;
using game::GetRenderer;
using game::GetWorld;
using ui::GetMapView;

namespace {
using G = object::Guest;
using S = ui::SpriteInstance;
test::CallCounter g_draw("objects.guest-draw"), g_overlay("objects.guest-draw-overlay");
test::CallCounter g_stat("objects.guest-draw-stat-bar"), g_marker("objects.guest-draw-selection-marker");
// Set only by the call-drive: the code runs as in play, but its renderer calls draw nothing. The driver's random points would
// otherwise reach the real renderer, and the game crashed on some of them.
bool g_dryRun = false;
ui::GuiManager* Gui()
{
    return GetGuiManager();
}

void RunStat(G*, int*);
void __cdecl RunMarker(int*, float, S*);

struct Env {
    float Zoom() { return GetRenderer()->m_zoomScale; }
    float Time() { return GetWorld()->m_gameTime; }
    int Day() { return GetWorld()->m_gameDay; }
    float DayTime() { return GetWorld()->m_gameDayTime; }
    bool DevToggle() { return GetContext().view.m_devToggle != 0; }
    int Stat() { return Gui()->m_context[1]; }
    bool ShowStats() { return Gui()->m_context[7] != 0; }
    uint32_t Color(uint32_t r, uint32_t g, uint32_t b) { return obl::ui::renderer::PackColor(GetRenderer(), r, g, b); }
    void Line(int x, int y, int x1, int y1, uint32_t color)
    {
        if (g_dryRun) return;
        ui::pixel_primitives::DrawLine(GetRenderer(), x, y, x1, y1, color);
    }
    void Fill(int x, int y, int w, int h, uint32_t color)
    {
        if (g_dryRun) return;
        int rect[4] = {x, y, w, h};
        obl::ui::renderer::FillRectBySize(GetRenderer(), rect, color);
    }
    void Clipped(S* s, int x, int y)
    {
        if (g_dryRun) return;
        (s)->DrawClipped(x, y, 0xffff, 999999.0f);
    }
    void InWorld(S* s, const float* p, float time)
    {
        if (g_dryRun) return;
        (s)->DrawInWorld(p, time);
    }
    void Toned(S* s, const float* p, float time, float tone, float illness, uint32_t extra)
    {
        if (g_dryRun) return;
        (s)->DrawInWorldTonedWithExtra(p, time, tone, illness, extra);
    }
    void Anchor(G& g, int* out)
    {
        float position[3] = {g.m_WorldXYZ[0], g.m_WorldXYZ[1], g.m_WorldXYZ[2]};
        int p[2];
        const int* at = GetMapView()->ProjectWorldPoint(p, position);
        auto* s = guest_draw::Sprite(g.m_sprite);
        out[0] = at[0] + (s)->GetDrawOffsetX();
        out[1] = at[1] + guest_draw::Anim(*s).m_hotspotY;
        int x, y;
        (s)->FindFirstOpaque(&x, &y);
        out[0] += x;
        out[1] += y;
    }
    void Marker(int* p, float value, S* s) { RunMarker(p, value, s); }
    void StatBar(G& g, int* p) { RunStat(&g, p); }
};

void RunDraw(G* g)
{
    Env env;
    g_draw.Count();
    guest_draw::Draw(*g, env);
}
void RunOverlay(G* g)
{
    Env env;
    g_overlay.Count();
    guest_draw::Overlay(*g, env);
}
void RunStat(G* g, int* p)
{
    Env env;
    g_stat.Count();
    guest_draw::StatBar(*g, p, env);
}
void __cdecl RunMarker(int* p, float value, S* mood)
{
    Env env;
    g_marker.Count();
    guest_draw::Marker(p, value, mood, env);
}

enum class Kind { Draw, Overlay, Stat, Marker };
void Drive(Kind kind, int count)
{
    auto* g = guest_shadow::GuestAt(0);
    if (!g || !g->m_sprite || !g->m_happinessSprite || !g->m_moodIcon) {
        OBL_LOG("autotest: FAILED guest draw fixture missing");
        return;
    }
    guest_shadow::GuestBackup backup(g);
    S attached = *guest_draw::Sprite(g->m_sprite);
    const S body = *guest_draw::Sprite(g->m_sprite), mood = *guest_draw::Sprite(g->m_happinessSprite);
    const auto oldZoom = GetRenderer()->m_zoomScale;
    const auto oldStat = Gui()->m_context[1], oldShow = Gui()->m_context[7];
    const auto oldDev = GetContext().view.m_devToggle;
    const int oldDay = GetWorld()->m_gameDay;
    const float oldTime = GetWorld()->m_gameDayTime;
    constexpr float values[] = {0, 0.2f, 0.5f, 0.8f, 1, 1.2f, -0.2f, 0.19999999f};
    constexpr float zooms[] = {0.5f, 1, 2};
    guest_shadow::Rng rng{0x9c01};
    g_dryRun = true;
    for (int i = 0; i < count; ++i) {
        GetRenderer()->m_zoomScale = zooms[i % 3];
        Gui()->m_context[1] = i % 20;
        Gui()->m_context[7] = (i / 20) % 2;
        g->m_happiness = values[(i / 3) % 8];
        for (size_t j = 0; j < std::size(g->m_opinion); ++j) g->m_opinion[j] = values[(i + j) % 8];
        for (size_t j = 0; j < std::size(g->m_desires); ++j) g->m_desires[j] = values[(i + j) % 8];
        g->m_drunk = g->m_foodIll = g->m_happiness;
        g->m_sunBurnLevel = values[(i / 8) % 8] * -2;
        g->m_DayArrived = oldDay - i % 5;
        g->m_timeArrived = float(i % 24);
        GetWorld()->m_gameDayTime = float((i / 5) % 24);
        g->m_insideBuilding = i % 7 == 0;
        g->m_topless = uint8_t(i % 3);
        GetContext().view.m_devToggle = i % 5 == 0;
        g->m_attachedSprite = i % 2 ? &attached : nullptr;
        g->m_AttachedOffsetX = i % 4 ? int(rng.Next() % 40) : -7;
        g->m_AttachedOffsetY = i % 4 ? int(rng.Next() % 40) : -13;
        guest_draw::Sprite(g->m_sprite)->m_direction = kind == Kind::Draw ? i % 12 : body.m_direction;
        g->m_state = i % 7 == 0 ? 0x2b : i % 7 == 1 ? 0x29 : 0;
        g->m_ActionState = i % 2 ? 14 : 0;
        g->m_SelectLevel = (i / 3) % 5 - 1;
        g->m_moodIconTime = i % 3 ? 2.0f : 0.0f;
        int point[2] = {int(rng.Next() % 2000) - 1000, int(rng.Next() % 2000) - 1000};
        switch (kind) {
        case Kind::Draw: RunDraw(g); break;
        case Kind::Overlay: RunOverlay(g); break;
        case Kind::Stat: RunStat(g, point); break;
        case Kind::Marker: RunMarker(point, g->m_happiness, guest_draw::Sprite(g->m_happinessSprite)); break;
        }
    }
    g_dryRun = false;
    *guest_draw::Sprite(g->m_sprite) = body;
    *guest_draw::Sprite(g->m_happinessSprite) = mood;
    GetRenderer()->m_zoomScale = oldZoom;
    Gui()->m_context[1] = oldStat;
    Gui()->m_context[7] = oldShow;
    GetContext().view.m_devToggle = oldDev;
    GetWorld()->m_gameDay = oldDay;
    GetWorld()->m_gameDayTime = oldTime;
}
void DriveDraw(int n)
{
    Drive(Kind::Draw, n);
}
void DriveOverlay(int n)
{
    Drive(Kind::Overlay, n);
}
void DriveStat(int n)
{
    Drive(Kind::Stat, n);
}
void DriveMarker(int n)
{
    Drive(Kind::Marker, n);
}
} // namespace

void RegisterGuestDrawDrivers()
{
    g_draw.SetDriver(DriveDraw);
    g_overlay.SetDriver(DriveOverlay);
    g_stat.SetDriver(DriveStat);
    g_marker.SetDriver(DriveMarker);
}
} // namespace obl::object

namespace obl::object {
void Guest::Draw()
{
    object::RunDraw(this);
}
void Guest::DrawOverlay()
{
    object::RunOverlay(this);
}
} // namespace obl::object
