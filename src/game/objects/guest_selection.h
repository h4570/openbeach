#pragma once

#include "core/float_to_int.h"
#include "game/objects/object_guest.h"
#include "game/ui/sprite.h"

#include <cstdint>

// The selection part of the guest class (guest selection, docs/architecture/objects.md): OnSelect, ReduceSelectionToHover, ClearSelection and TrySelectAtScreenPoint. The GUI, the camera and the sprite queries stay calls into the game:
//   OnSelect:  env.GlobalKeyline()                     ObjectManager::m_globalKeyline (every object is outlined: the guest leaves its sprite alone)
//              env.ClearGuestSelection()               clears the selection of every guest (class 3)
//              env.ShowPanel(), env.ClosePanel()       opens GuestPanel for the guest or closes its panel
//              env.ForgetGuiSelection()                clears the GUI selection (-1)
//              env.WorldToInt(world, x, y)             integer world coordinates
//              env.ViewWidth(), env.ViewHeight(), env.ZoomScale()   renderer width / height (read as unsigned) / zoom
//              env.SetCamera(x, y), env.StopCamera()   setting the camera position / stopping camera movement
//   TrySelect: env.WorldToScreen(world, out)           world-to-screen projection
//              env.DrawOffsetX(), env.HotspotY()       the sprite drawing offset; the current animation's hot spot (a signed 16-bit value)
//              env.FirstOpaquePixel(x, y)              the first opaque sprite pixel
//              env.HitTest(point, screenPoint)         the selection region hit test
//              env.Select()                            the guest's own OnSelect(1, 0) through the vtable
namespace obl::guest_select {

enum SelectLevel : int32_t { NotSelected = 0, Hovered = 1, Selected = 2 };

constexpr const char* kGuestPanel = "GuestPanel";
constexpr uint8_t kGuestClass = 3; // the object class whose selection a click on a guest clears

inline ui::SpriteInstance* SpriteOf(object::Guest& guest)
{
    return static_cast<ui::SpriteInstance*>(guest.m_sprite);
}

// A selected guest that the selection moved away from stays outlined as hovered.
inline void ReduceSelectionToHover(object::Guest& guest)
{
    if (guest.m_SelectLevel == Selected) guest.m_SelectLevel = Hovered;
}

inline void ClearSelection(object::Guest& guest, bool globalKeyline)
{
    guest.m_SelectLevel = NotSelected;
    if (!globalKeyline) SpriteOf(guest)->m_keyline = 0;
}

// Half of the visible map along one axis: the (unsigned) screen size over twice the zoom scale, truncated.
inline int32_t HalfView(uint32_t pixels, float zoomScale)
{
    return FloatToIntWrap(static_cast<double>(pixels) / (static_cast<double>(zoomScale) + static_cast<double>(zoomScale)));
}

// OnSelect. Hovering (`clicked` false) only outlines an unselected guest. A click opens the guest panel on an unselected or hovered guest
// (the first also centres the camera on request) and closes it on a selected one; any other level is left alone. The global keyline flag is read
// after the GUI calls, which may change it.
template<class Env> void OnSelect(object::Guest& guest, bool clicked, bool centerCamera, Env& env)
{
    const int32_t level = guest.m_SelectLevel;
    const auto outline = [&](uint8_t on) {
        if (!env.GlobalKeyline()) SpriteOf(guest)->m_keyline = on;
    };
    if (!clicked) {
        if (level != NotSelected) return;
        guest.m_SelectLevel = Hovered;
        outline(1);
        return;
    }
    if (level == Selected) {
        env.ClosePanel();
        guest.m_SelectLevel = NotSelected;
        outline(0);
        return;
    }
    if (level != NotSelected && level != Hovered) return;
    env.ClearGuestSelection();
    env.ShowPanel();
    guest.m_SelectLevel = Selected;
    outline(1);
    if (level != NotSelected || !centerCamera) return;
    env.ForgetGuiSelection();
    const float world[3] = {guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]};
    int32_t x = 0, y = 0;
    env.WorldToInt(world, x, y);
    x -= HalfView(env.ViewWidth(), env.ZoomScale());
    y -= HalfView(env.ViewHeight(), env.ZoomScale());
    env.SetCamera(x, y);
    env.StopCamera();
}

// TrySelectAtScreenPoint: the guest's selection region hangs off the first opaque pixel of its sprite as drawn; a hit selects it.
template<class Env> bool TrySelectAtScreenPoint(object::Guest& guest, int32_t* screenPoint, Env& env)
{
    const float world[3] = {guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]};
    int32_t point[2] = {};
    env.WorldToScreen(world, point);
    point[0] += env.DrawOffsetX();
    point[1] += env.HotspotY();
    int32_t pixelX = 0, pixelY = 0;
    env.FirstOpaquePixel(pixelX, pixelY);
    point[0] += pixelX;
    point[1] += pixelY;
    if (!env.HitTest(point, screenPoint)) return false;
    env.Select();
    return true;
}

} // namespace obl::guest_select
