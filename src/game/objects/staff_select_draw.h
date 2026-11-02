#pragma once

#include "core/float_to_int.h"
#include "core/ascii_case.h"
#include "game/objects/guest_draw.h"
#include "game/objects/guest_selection.h"
#include "game/objects/object_staff.h"
#include "game/ui/sprite.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// Staff selection and drawing. The GUI, the camera, the renderer and the sprites stay calls into
// the game, through an Env per function:
//   OnSelect (OnSelect of the base class; ProfessionOnSelect wraps it for the lifeguard, the representative and the others):
//     env.ForgetGuiSelection(), env.WorldToInt(world, x, y), env.ViewWidth(), env.ViewHeight(), env.ZoomScale(), env.SetCamera(x, y), env.StopCamera()
//     (the same calls as the guest's, src/game/objects/guest_selection.h); the wrappers also env.ClearStaffSelection(),
//     env.ShowDialog(name) / env.CloseDialog() (open the dialog with the member as provider / close it), env.Base(clicked, center).
//   Draw (ServiceDraw, LifeguardDraw, RepDraw):
//     env.Time(), env.InWorld(sprite, world, time) (draw in the world); the lifeguard also env.Object(id) (object by id),
//     env.GuestIllness(guest), env.GuestTone(guest) (called in this order), env.Toned(sprite, world, time, tone, illness) (draw in the world, toned).
//   DrawOverlay: env.WorldToScreen(world, out), env.DrawOffsetX(), env.FirstOpaquePixel(x, y), and the happiness marker
//     env.HappinessMarker(point, happiness) (HappinessMarker, the guest's marker on a short-lived "UI3_hud_smile" sprite; its own Env:
//     env.CreateMood(), env.ReleaseMood(sprite),
//     env.Color(r, g, b), env.Zoom(), env.Line(...), env.Clipped(...) as in guest_draw::Marker.
namespace obl::staff_draw {

using object::Staff;
using ui::SpriteInstance;

constexpr const char* kLifeguardDialog = "LifeguardDialog"; // *
constexpr const char* kRepDialog = "RepDialog"; // *
constexpr const char* kCleanerDialog = "CleanerDialog"; // *
constexpr const char* kMechanicDialog = "MechanicDialog"; // *
constexpr const char* kSecurityDialog = "SecurityDialog"; // *
constexpr const char* kBuilderDialog = "BuilderDialog"; // *
constexpr char kNoSprite[] = "NONE"; // k_GuiTableButtonNoAction
constexpr const char* kMoodSprite = "UI3_hud_smile"; // *
constexpr int32_t kMoodAnimType = 3; // SpriteInstance +8 of the marker's face

inline SpriteInstance* SpriteOf(void* sprite)
{
    return static_cast<SpriteInstance*>(sprite);
}
inline const ui::SpriteAnim& AnimOf(const SpriteInstance& sprite)
{
    return guest_draw::Anim(sprite);
}

// OnSelect: only the request to centre matters (`clicked` is not read). The camera goes to the member's tile minus half a view, as the guest's.
template<class Env> void OnSelect(Staff& member, bool centerCamera, Env& env)
{
    if (!centerCamera) return;
    env.ForgetGuiSelection();
    const float world[3] = {member.m_WorldXYZ[0], member.m_WorldXYZ[1], member.m_WorldXYZ[2]};
    int32_t x = 0, y = 0;
    env.WorldToInt(world, x, y);
    x -= guest_select::HalfView(env.ViewWidth(), env.ZoomScale());
    y -= guest_select::HalfView(env.ViewHeight(), env.ZoomScale());
    env.SetCamera(x, y);
    env.StopCamera();
}

// ProfessionOnSelect, for the lifeguard, the representative and the cleaner, mechanic's, security guard's and builder's (the same code, a different dialog): a hover marks an unselected member; a click opens the dialog of an
// unselected or hovered one (after clearing the selection of the staff) and closes it on a selected one; every path ends in the base class's OnSelect.
template<class Env> void ProfessionOnSelect(Staff& member, bool clicked, bool centerCamera, const char* dialog, Env& env)
{
    const int32_t level = member.m_SelectLevel;
    if (clicked) {
        if (level >= 0) {
            if (level < 2) {
                env.ClearStaffSelection();
                member.m_SelectLevel = 2;
                env.ShowDialog(dialog);
            } else if (level == 2) {
                env.CloseDialog();
                member.m_SelectLevel = 0;
            }
        }
    } else if (level == 0) {
        member.m_SelectLevel = 1;
    }
    env.Base(clicked, centerCamera);
}

// ServiceDraw: a definition whose sprite is "NONE" has nothing to draw.
template<class Env> void ServiceDraw(object::ServiceStaff& member, Env& env)
{
    if (ascii::EqualsNoCase(member.m_staffDefinition->m_Sprite, kNoSprite)) return;
    env.InWorld(SpriteOf(member.m_sprite), member.m_WorldXYZ, env.Time());
}

// The sprite the lifeguard and the representative carry (a tool, a briefcase): drawn first, at the member's position minus the saved offsets (zero-extended
// offsets), looking where the member looks, or in the last direction it has when it has fewer.
template<class Env> void DrawAttached(const Staff& member, void* attached, uint32_t offsetX, uint32_t offsetY, Env& env)
{
    SpriteInstance* sprite = SpriteOf(attached);
    if (!sprite) return;
    const float position[3] = {
            float(double(member.m_WorldXYZ[0]) - offsetX), float(double(member.m_WorldXYZ[1]) - offsetY), member.m_WorldXYZ[2]};
    const int32_t directions = static_cast<uint8_t>(guest_draw::Anim(*sprite).m_directions);
    int32_t direction = SpriteOf(member.m_sprite)->m_direction;
    if (direction >= directions) direction = directions - 1;
    sprite->m_direction = direction;
    env.InWorld(sprite, position, env.Time());
}

// RepDraw: the representative and the briefcase it carries.
template<class Env> void RepDraw(object::Rep& member, Env& env)
{
    if (member.m_insideBuilding) return;
    DrawAttached(member, member.m_attachedSprite, static_cast<uint32_t>(member.m_AttachedOffsetX),
            static_cast<uint32_t>(member.m_AttachedOffsetY), env);
    env.InWorld(SpriteOf(member.m_sprite), member.m_WorldXYZ, env.Time());
}

// LifeguardDraw: the guest being rescued (`m_DrawGuest`) is drawn on the lifeguard, in the guest's own tone and illness.
template<class Env> void LifeguardDraw(object::Lifeguard& member, Env& env)
{
    if (member.m_insideBuilding) return;
    DrawAttached(member, member.m_attachedSprite, static_cast<uint32_t>(member.m_AttachedOffsetX),
            static_cast<uint32_t>(member.m_AttachedOffsetY), env);
    if (member.m_DrawGuest) {
        object::Base* guest = env.Object(member.m_saving_id);
        const float time = env.Time();
        const float illness = env.GuestIllness(guest);
        const float tone = env.GuestTone(guest);
        env.Toned(SpriteOf(guest->m_sprite), member.m_WorldXYZ, time, tone, illness);
    }
    env.InWorld(SpriteOf(member.m_sprite), member.m_WorldXYZ, env.Time());
}

// HappinessMarker: the guest's marker on a face sprite made for the call and released after it.
template<class Env> void HappinessMarker(int32_t* point, float happiness, Env& env)
{
    SpriteInstance* mood = SpriteOf(env.CreateMood());
    mood->m_animType = kMoodAnimType;
    guest_draw::Marker(reinterpret_cast<int*>(point), happiness, mood, env);
    env.ReleaseMood(mood);
}

// DrawOverlay: a hovered or selected, visible member gets the happiness marker over its head (the point is the first opaque pixel of its sprite).
template<class Env> void DrawOverlay(Staff& member, Env& env)
{
    if (!(member.m_SelectLevel > 0 && member.m_SelectLevel < 3) || member.m_visible == 0) return;
    const float world[3] = {member.m_WorldXYZ[0], member.m_WorldXYZ[1], member.m_WorldXYZ[2]};
    int32_t point[2] = {};
    env.WorldToScreen(world, point);
    point[0] += env.DrawOffsetX();
    point[1] += guest_draw::Anim(*SpriteOf(member.m_sprite)).m_hotspotY;
    int32_t pixelX = 0, pixelY = 0;
    env.FirstOpaquePixel(pixelX, pixelY);
    point[0] += pixelX;
    point[1] += pixelY;
    env.HappinessMarker(point, member.m_happiness);
}

} // namespace obl::staff_draw
