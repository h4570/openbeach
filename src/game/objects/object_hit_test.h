#pragma once

#include "game/objects/object_base.h"
#include "game/ui/sprite.h"

#include <cstdint>
#include <cstring>

// The three hit-test predicates (docs/architecture/objects.md, docs/architecture/data-formats/converter/gui-runtime.md).
// The selectable cursor hit test ignores the caller context.
// The unrestricted cursor hit test also ignores context and skips selectability and the building pick animation.
// The selection region test rewrites both sprite and screen points in place.
//
// Env, for the two cursor tests:
//   bool IsSelectable(const Entity&)              IsSelectable
//   bool ClipObjectRect(const Entity&)            clipping the object rectangle of g_MapView
//   void WorldToScreen(const float world[3], int32_t out[2])
//   int32_t DrawOffsetX(const SpriteInstance&)        the sprite drawing offset
//   void Cursor(int32_t& x, int32_t& y)               the cursor position in game pixels
//   float Zoom()                                      the renderer's zoom scale: 2, 1 or 0.5
//   bool SpriteHitTest(SpriteInstance&, int x, int y) the sprite pixel hit test
// The cursor is divided by the zoom in double precision and truncated: the zoom values are powers of two, so a double gives the
// same integers.
namespace obl::object {
// The game-bound hit tests (prop.cpp): the object under the cursor, with or without the selectable check, and the
// selection region of a sprite point.
bool HitTestCursor(Base* object);
bool HitTestCursorUnselectable(Base* object);
bool HitTestSelectionRegion(int32_t* spritePoint, int32_t* screenPoint);
} // namespace obl::object

namespace obl::object_hit_test {

using ui::SpriteAnim;
using ui::SpriteInstance;

inline const SpriteAnim& CurrentAnim(const SpriteInstance& sprite)
{
    return *sprite.m_sprite->m_anims[sprite.m_animIndex];
}

// The part both cursor tests share once the object passed its checks: the cursor, relative to the current animation's frame rectangle.
template<class Env> bool CursorInFrame(const object::Base& object, Env& env)
{
    float world[3];
    std::memcpy(world, object.m_WorldXYZ, sizeof(world));
    int32_t screen[2];
    env.WorldToScreen(world, screen);
    auto& sprite = *static_cast<SpriteInstance*>(object.m_sprite);
    const int32_t left = screen[0] + env.DrawOffsetX(sprite);
    const int32_t top = screen[1] + CurrentAnim(sprite).m_hotspotY;
    int32_t cursorX, cursorY;
    env.Cursor(cursorX, cursorY);
    const double zoom = env.Zoom();
    const int32_t x = static_cast<int32_t>(static_cast<double>(cursorX) / zoom) - left;
    const int32_t y = static_cast<int32_t>(static_cast<double>(cursorY) / zoom) - top;
    const SpriteAnim& anim = CurrentAnim(sprite);
    if (x < 0 || x >= anim.m_width || y < 0 || y >= anim.m_height) return false;
    return env.SpriteHitTest(sprite, x, y);
}

template<class Env> bool HitTestCursorUnselectable(const object::Base& object, Env& env)
{
    if (!env.ClipObjectRect(object) || !object.m_visible) return false;
    return CursorInFrame(object, env);
}

// A building shows its "pick" animation (the picking silhouette) to be hit-tested: the first animation of that name is selected, and the frame
// restarts when that changes the animation.
inline void SelectPickAnimation(SpriteInstance& sprite)
{
    for (int32_t i = 0; i < sprite.m_sprite->m_animCount; ++i) {
        if (std::strcmp("pick", sprite.m_sprite->m_anims[i]->m_name) != 0) continue;
        if (sprite.m_animIndex != i) {
            sprite.m_animIndex = i;
            sprite.m_frameIndex = 0;
        }
        return;
    }
}

template<class Env> bool HitTestCursor(const object::Base& object, Env& env)
{
    if (!env.IsSelectable(object) || !env.ClipObjectRect(object) || !object.m_visible) return false;
    if (object.m_type == object::ObjectType::Building) SelectPickAnimation(*static_cast<SpriteInstance*>(object.m_sprite));
    return CursorInFrame(object, env);
}

// Both points are scaled by the zoom and truncated into the arrays; the verdict is a box around the sprite point: half-width 25 * zoom to both
// sides in x, up to 1 half-width above and 2.5 below in y (screen y grows downward). Env: float Zoom().
template<class Env> bool HitTestSelectionRegion(int32_t spritePoint[2], int32_t screenPoint[2], Env& env)
{
    const double zoom = env.Zoom();
    screenPoint[0] = static_cast<int32_t>(screenPoint[0] * zoom);
    screenPoint[1] = static_cast<int32_t>(screenPoint[1] * zoom);
    spritePoint[0] = static_cast<int32_t>(spritePoint[0] * zoom);
    spritePoint[1] = static_cast<int32_t>(spritePoint[1] * zoom);
    const int32_t centreX = spritePoint[0];
    const int32_t centreY = spritePoint[1];
    const int32_t half = static_cast<int32_t>(zoom * 25.0);
    return screenPoint[0] > centreX - half && screenPoint[0] < centreX + half && screenPoint[1] > centreY - half &&
            static_cast<double>(screenPoint[1]) < half * 2.5 + centreY;
}

} // namespace obl::object_hit_test
