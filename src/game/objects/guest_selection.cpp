#include "game/objects/object_static.h"
#include "game/objects/object_hit_test.h"
#include "game/objects/guest.h"
#include "game/ui/sprite_instance_geometry.h"

#include "game/game/game_accessors.h"
#include "core/fatal.h"
#include "game/objects/guest_shadow.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/map_view_access.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/guest_selection.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_manager_state.h"
#include "game/ui/renderer_types.h"
#include "game/ui/sprite.h"
#include "core/call_counter.h"

#include <cstdint>
#include <string>
#include "game/ui/map_view_math.h"
#include "game/ui/map_view.h"

// The selection part of the guest class (src/game/objects/guest_selection.h, guest selection, docs/architecture/objects.md): OnSelect,
// ReduceSelectionToHover, ClearSelection and TrySelectAtScreenPoint. An input these paths cannot
// take (a guest without a sprite) stops the game with a fatal error. OnSelect opens and closes the guest panel, clears the selection
// of every guest and moves the camera; TrySelectAtScreenPoint ends in OnSelect.
namespace obl::object {
using game::GetGuiManager;
using game::GetObjectManager;
using game::GetRenderer;
using ui::GetMapView;

namespace {

using guest_shadow::GuestAt;
using guest_shadow::GuestBackup;
using object::Guest;
using ui::SpriteInstance;

using OnSelectFunc = void (*)(Guest*, uint32_t, uint32_t);

test::CallCounter g_onSelect("objects.guest-on-select");
test::CallCounter g_reduce("objects.guest-reduce-selection-to-hover");
test::CallCounter g_clear("objects.guest-clear-selection");
test::CallCounter g_trySelect("objects.guest-try-select-at-screen-point");

// Set only by ForceGuestSelection (a test helper): GameEnv skips the panel and camera calls and the object manager's probe skips the clear, so
// the selected path runs without opening a panel, clearing other guests or moving the camera.
bool g_skipGuiCalls = false;

ui::GuiManager* Gui()
{
    return GetGuiManager();
}
SpriteInstance* SpriteOf(Guest& guest)
{
    return static_cast<SpriteInstance*>(guest.m_sprite);
}

struct GameEnv {
    Guest& guest;

    bool GlobalKeyline() const { return GetObjectManager()->m_globalKeyline != 0; }
    void ClearGuestSelection() { object_manager::ClearSelectionByClass(GetObjectManager(), guest_select::kGuestClass); }
    void ShowPanel()
    {
        if (!g_skipGuiCalls) Gui()->ShowDialog(guest_select::kGuestPanel, &guest, false);
    }
    void ClosePanel()
    {
        if (!g_skipGuiCalls) Gui()->CloseDialogByProvider(&guest);
    }
    void ForgetGuiSelection() { Gui()->m_selection = -1; }
    void WorldToInt(const float* world, int32_t& x, int32_t& y)
    {
        float copy[3] = {world[0], world[1], world[2]};
        ui::MapView::WorldToInt(copy, reinterpret_cast<int*>(&x), reinterpret_cast<int*>(&y));
    }
    uint32_t ViewWidth() const { return static_cast<uint32_t>(GetRenderer()->m_width); }
    uint32_t ViewHeight() const { return static_cast<uint32_t>(GetRenderer()->m_height); }
    float ZoomScale() const { return GetRenderer()->m_zoomScale; }
    void SetCamera(int32_t x, int32_t y)
    {
        if (!g_skipGuiCalls) GetMapView()->SetCameraPos(x, y);
    }
    void StopCamera()
    {
        if (!g_skipGuiCalls) GetMapView()->StopCamera();
    }

    void WorldToScreen(const float* world, int32_t* out)
    {
        float copy[3] = {world[0], world[1], world[2]};
        int buffer[2] = {};
        const int* at = GetMapView()->ProjectWorldPoint(buffer, copy);
        out[0] = at[0];
        out[1] = at[1];
    }
    int32_t DrawOffsetX() { return (SpriteOf(guest))->GetDrawOffsetX(); }
    int32_t HotspotY()
    {
        const SpriteInstance* sprite = SpriteOf(guest);
        return sprite->m_sprite->m_anims[sprite->m_animIndex]->m_hotspotY;
    }
    void FirstOpaquePixel(int32_t& x, int32_t& y)
    {
        (SpriteOf(guest))->FindFirstOpaque(reinterpret_cast<int*>(&x), reinterpret_cast<int*>(&y));
    }
    bool HitTest(const int32_t* point, int32_t* screenPoint)
    {
        int copy[2] = {point[0], point[1]};
        return object::HitTestSelectionRegion(copy, screenPoint);
    }
    // OnSelect opens the guest panel and optionally centres the camera.
    void Select() { guest.OnSelect(1, 0); }
};

void RunOnSelect(Guest& guest, uint32_t clicked, uint32_t centerCamera)
{
    GameEnv env{guest};
    // Both flags are tested as bytes.
    guest_select::OnSelect(guest, (clicked & 0xff) != 0, (centerCamera & 0xff) != 0, env);
}

bool TrySelect(Guest& guest, int32_t* screenPoint)
{
    GameEnv env{guest};
    return guest_select::TrySelectAtScreenPoint(guest, screenPoint, env);
}

// ---- handlers -----------------------------------------------------------------------------------------------------------------------

// The GUI and camera layers below: they return without calling the game while ForceGuestSelection runs.
bool ProbeClearByClass(object::Manager*, uint32_t)
{
    return g_skipGuiCalls;
}

} // namespace

void ForceGuestSelection()
{
    Guest* guest = GuestAt(0);
    if (!guest || !guest->m_sprite) return;
    GuestBackup backup(guest);
    const uint8_t keyline = SpriteOf(*guest)->m_keyline;
    // Exercise the selected path without opening a panel or clearing other guests.
    g_skipGuiCalls = true;
    guest->m_SelectLevel = 0;
    guest->OnSelect(0, 0);
    guest->OnSelect(1, 0);
    guest->ReduceSelectionToHover();
    guest->ClearSelection();
    int32_t point[2] = {-100000, -100000};
    GuestTrySelectAtScreenPoint(*guest, point);
    g_skipGuiCalls = false;
    SpriteOf(*guest)->m_keyline = keyline;
}

bool GuestTrySelectAtScreenPoint(Guest& guest, int32_t* screenPoint)
{
    if (!guest.m_sprite) fatal::UnhandledInput("objects.guest-try-select-at-screen-point", "guest without a sprite");
    g_trySelect.Count();
    return TrySelect(guest, screenPoint);
}

void SetGuestClearSelectionProbe()
{
    object_manager::g_clearSelectionProbe = &ProbeClearByClass;
}

} // namespace obl::object

namespace obl::object {
void Guest::OnSelect(uint32_t clicked, uint32_t centerCamera)
{
    if (!m_sprite) fatal::UnhandledInput("objects.guest-on-select", "guest without a sprite");
    g_onSelect.Count();
    RunOnSelect(*this, clicked, centerCamera);
}
void Guest::ReduceSelectionToHover()
{
    g_reduce.Count();
    guest_select::ReduceSelectionToHover(*this);
}
void Guest::ClearSelection()
{
    const bool global = GetObjectManager()->m_globalKeyline != 0;
    if (!global && !m_sprite)
        fatal::UnhandledInput("objects.guest-clear-selection", "no sprite and no global keyline: the old code crashed");
    g_clear.Count();
    guest_select::ClearSelection(*this, global);
}
} // namespace obl::object
