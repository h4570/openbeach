#pragma once

#include "game/ui/sprite.h"
#include "game/tables/building_behavior.h"
#include "game/objects/building_queries.h"
#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>
#include <utility>

// The screen side of a Building (docs/architecture/objects.md): Draw, DrawOverlay, UpdateScreenRect, OnSelect, BeginDemolish and the lifeguard tower's beach mode IsBeachOpenMode / SetBeachOpenMode. Every call to the renderer, the map view, the GUI, the construction manager and the message queues goes
// through `Env` (src/game/objects/building_draw.cpp binds it to the game services; the unit test records the calls), so the order and the arguments of
// the boundary calls are what the unit test checks. Intermediate arithmetic runs in `double` between the loads and the
// store to a float, which keeps the results the tests pin.
//
//   Env (all by value or by pointer exactly as the game function takes them):
//     bool Building(id) / Demolishing(id)           the construction manager (answer non-zero, progress non-zero)
//     float BuildProgress(id) / DemolishProgress(id)
//     float Time() / DayTime() / LastDayTime()      elapsed game seconds, current hour and previous frame hour
//     bool LightEffects()                           whether lighting effects are enabled
//     void WorldToScreen(int out[2], const float* world)       map view: world to screen
//     uint32_t Depth(const float* world)                       map view: depth
//     int DrawOffsetX(SpriteInstance*)                          draw offset of the current frame
//     void ScreenCells(Building&)                         UpdateScreenCells (m_rScrExtents, cells of the map blocks)
//     void LightMask(Building&, uint8_t* lightBuffer)     DrawLightMask (the light mask of the building)
//     (the two above are what Draw calls; their own bodies, UpdateScreenCells and DrawLightMask below, use the entries marked [mask])
//     int32_t BlockSize()                                       [mask] the map view's block size, the divisor of the cells
// bool Intersect(const Rect& a, const Rect& b, Rect& out) [mask] rectangle intersection (out is written only on overlap)
//     int32_t BuildingCount() / Entity* BuildingAt(i)       [mask] ObjectManager's building list (a read at index == count is allowed)
//     void CopyRle(dst, runs, pitch, xBytes, y, w, h, Rect* clip)   [mask] run-length copy into 32-bit pixels
//     void Bake(S*, world, a, c) / Preview(S*, world, a, c, ghost) / PreviewAlt(S*, world, a, c, progress, lower)
//     void InWorld(S*, world, time) / Clipped(S*, x, y, depth, time)
//     bool ClipRect(int* x, int* y, int* l, int* t, int* w, int* h)         renderer clip
//     uint8_t* Frame(SpriteAnim*, int frame, int direction)               frame pixels of an animation
//     void Blit(dst, frame, palette, pitch, x, y, w, h, l, t, cw, ch) / BlitZoomOut(...)
//     SpriteInstance* CreateSprite(const char*) / void ReleaseSprite(SpriteInstance*)
//     renderer: LightsDirty() (m_lightsDirty = 1, m_lightsDirtyFrom = 0), RenderLights() (m_lightsEnabled), ZoomLevel(), ZoomScale(), Width(), Height(),
//               LightBuffer(), Palette(); gui/map: ForgetKey() (the GUI forgets its key focus), WorldToInt(world, &x, &y), SetCameraPos(x, y), StopCamera(),
//               CameraX() / CameraY() (the map view's camera); ShowDialog(name, provider)
// beach: AreaOpen(kind, area) (the grid's area state), SetAreaOpen(kind, area), SetAreaClosed(kind, area), PostClass(Message&, cls),
//            PostBuildings(Message&); BeginDemolish: ClearQueue(queue), HideStaff(building), Send(building, Message)
namespace obl::building_draw {

using messages::Message;
using object::Building;
using tables::BuildingDef;
using ui::SpriteAnim;
using ui::SpriteInstance;

inline SpriteInstance* MainSprite(Building& b)
{
    return static_cast<SpriteInstance*>(b.m_sprite);
}
inline SpriteInstance* GhostSprite(Building& b)
{
    return static_cast<SpriteInstance*>(b.m_constructionSprite);
}
inline SpriteAnim& Anim(SpriteInstance& s)
{
    return *s.m_sprite->m_anims[s.m_animIndex];
}

// `x < y` as below-or-unordered: true when x is less than y or either is NaN.
inline bool Below(double x, double y)
{
    return !(x >= y);
}

// The animation is set only when it differs, and then the frame restarts.
inline void SetAnim(SpriteInstance& s, int32_t index)
{
    if (s.m_animIndex != index) {
        s.m_animIndex = index;
        s.m_frameIndex = 0;
    }
}

// ---- The beach mode of a lifeguard tower ---------------------------------------------------------------------------------------------------------

// IsBeachOpenMode(mode): 0 both beach areas open, 1 the first open and the second closed, 2 the first closed and the second
// open; any other mode answers (mode - 2) & ~0xff, a leftover of the compare chain that is non-zero only for a mode outside 0xff.
template<class Env> uint32_t IsBeachOpenMode(Env& env, int32_t area, int32_t mode)
{
    if (mode == 0) return env.AreaOpen(0, area) && env.AreaOpen(1, area) ? 1 : 0;
    if (mode == 1) return env.AreaOpen(0, area) && !env.AreaOpen(1, area) ? 1 : 0;
    if (mode - 2 != 0) return static_cast<uint32_t>(mode - 2) & 0xffffff00u;
    return !env.AreaOpen(0, area) && !env.AreaOpen(1, area) ? 1 : 0;
}

// SetBeachOpenMode(mode): opens/closes the two areas of the grid and tells the world (MT_SEA_STATE_CHANGE to the class 3) and the
// buildings (MT_RE_POLL_FLAG_STATE). The last three words of the messages are zero.
template<class Env> void SetBeachOpenMode(Env& env, Building& b, int32_t mode)
{
    uint32_t flag = 0;
    const int32_t area = b.m_InfluenceArea;
    if (mode == 0) {
        env.SetAreaOpen(0, area);
        env.SetAreaOpen(1, area);
        flag = 1;
    } else if (mode == 1) {
        env.SetAreaOpen(0, area);
        env.SetAreaClosed(1, area);
    } else if (mode == 2) {
        env.SetAreaClosed(0, area);
        env.SetAreaClosed(1, area);
    }
    const int32_t id = static_cast<int32_t>(b.m_id);
    Message sea{};
    sea.sender = id;
    sea.receiver = id;
    sea.type = messages::MT_SEA_STATE_CHANGE;
    sea.flags = static_cast<int32_t>(flag);
    sea.data = area;
    env.PostClass(sea, 3);
    Message poll{};
    poll.sender = id;
    poll.receiver = id;
    poll.type = messages::MT_RE_POLL_FLAG_STATE;
    env.PostBuildings(poll);
}

// ---- Demolition, selection, screen rectangle ------------------------------------------------------------------------------------------------------

// BeginDemolish: the building closes (m_OpenStatus = 1), its queue is cleared, the staff hidden and it gets MT_STATECHANGE
// with state 2 through its own virtual SendMessage.
template<class Env> void BeginDemolish(Env& env, Building& b)
{
    b.m_OpenStatus = 1;
    env.ClearQueue(b.m_queue);
    env.HideStaff(b);
    const int32_t id = static_cast<int32_t>(b.m_id);
    Message state{};
    state.sender = id;
    state.receiver = id;
    state.type = messages::MT_STATECHANGE;
    state.flags = 0;
    state.data = 2;
    env.Send(b, state);
}

// OnSelect(clicked, centerCamera): clicking opens the construction, demolition or the building's own dialog; centering puts the
// camera on the building (the map position of its world point minus half of the view, in map pixels).
template<class Env> void OnSelect(Env& env, Building& b, bool clicked, bool centerCamera)
{
    const int32_t id = static_cast<int32_t>(b.m_id);
    if (clicked) {
        if (env.Building(id)) {
            env.ShowDialog("ConstructBuilding", &b);
            b.m_SelectLevel = 1;
        } else if (env.Demolishing(id)) {
            env.ShowDialog("DemolishBuilding", &b);
            b.m_SelectLevel = 1;
        } else {
            env.ShowDialog(b.m_definition->m_Menu.c_str(), &b);
            b.m_SelectLevel = 2;
        }
    }
    if (centerCamera) {
        env.ForgetKey();
        float world[3] = {b.m_WorldXYZ[0], b.m_WorldXYZ[1], b.m_WorldXYZ[2]};
        int32_t x = 0, y = 0;
        env.WorldToInt(world, &x, &y);
        const double twice = static_cast<double>(env.ZoomScale()) + static_cast<double>(env.ZoomScale());
        x -= FloatToIntWrap(static_cast<double>(static_cast<uint32_t>(env.Width())) / twice);
        y -= FloatToIntWrap(static_cast<double>(static_cast<uint32_t>(env.Height())) / twice);
        env.SetCameraPos(x, y);
        env.StopCamera();
    }
}

// UpdateScreenRect: the building's rectangle on the map (m_rScrRect) from the screen point of its world position, the draw
// offset and hotspot of the current animation and its size; answers the rectangle.
template<class Env> screen::Rect* UpdateScreenRect(Env& env, Building& b)
{
    float world[3] = {b.m_WorldXYZ[0], b.m_WorldXYZ[1], b.m_WorldXYZ[2]};
    int32_t screen[2];
    env.WorldToScreen(screen, world);
    SpriteInstance& sprite = *MainSprite(b);
    const int32_t x = screen[0] + env.DrawOffsetX(&sprite) + env.CameraX();
    const SpriteAnim& anim = Anim(sprite);
    const int32_t y = screen[1] + anim.m_hotspotY + env.CameraY();
    b.m_rScrRect.top = y;
    b.m_rScrRect.left = x;
    b.m_rScrRect.bottom = anim.m_height + y;
    b.m_rScrRect.right = anim.m_width + x;
    return &b.m_rScrRect;
}

// UpdateScreenCells(x, y, z): the building's extents in cells of the map blocks (m_rScrExtents). The screen point of the world
// point, the draw offset and the hotspot come from the animation 0 (put there for the two reads and given back right after, so the extents'
// size is the one of the animation the building had); right and bottom are one cell past the last pixel.
template<class Env> void UpdateScreenCells(Env& env, Building& b, float x, float y, float z)
{
    float world[3] = {x, y, z};
    int32_t screen[2];
    env.WorldToScreen(screen, world);
    SpriteInstance& sprite = *MainSprite(b);
    const int32_t previous = sprite.m_animIndex;
    if (previous != 0) {
        sprite.m_animIndex = 0;
        sprite.m_frameIndex = 0;
    }
    int32_t left = screen[0] + env.DrawOffsetX(&sprite);
    int32_t top = screen[1] + Anim(sprite).m_hotspotY;
    SetAnim(sprite, previous);
    left += env.CameraX();
    top += env.CameraY();
    b.m_rScrExtents.top = top / env.BlockSize();
    b.m_rScrExtents.left = left / env.BlockSize();
    const SpriteAnim& anim = Anim(sprite);
    b.m_rScrExtents.bottom = (static_cast<int32_t>(anim.m_height) + top) / env.BlockSize() + 1;
    b.m_rScrExtents.right = (static_cast<int32_t>(anim.m_width) + left) / env.BlockSize() + 1;
}

// A building that lights its own animation (m_lightMapOffset) into the mask: open (m_OpenStatus 0) and not in the night window of its lights
// (the day time below the switch-on time and above the switch-off time reads as daylight; a NaN compares as below).
template<class Env> bool CastsLight(Env& env, const Building& o)
{
    if (o.m_OpenStatus != 0) return false;
    const double day = env.DayTime();
    return !(Below(day, static_cast<double>(o.m_lightsOn)) && day > static_cast<double>(o.m_lightsOff));
}

// DrawLightMask(lightBuffer): copies the run-length coded light maps of the buildings that come before this one in the
// object list and overlap its screen rectangle into the light buffer, clipped to this building's pick-mask animation (m_pickOffset). The
// sprite of a lit building goes back to its animation only on the paths that blit or miss the rectangle: the paths that clip
// the copy away leave that sprite on its light map animation.
template<class Env> void DrawLightMask(Env& env, Building& b, uint8_t* lightBuffer)
{
    SpriteInstance& sprite = *MainSprite(b);
    if (b.m_pickOffset != -1) SetAnim(sprite, b.m_pickOffset);
    const int32_t width = env.Width(), height = env.Height();
    const int32_t camX = env.CameraX(), camY = env.CameraY();
    int32_t screen[2];
    env.WorldToScreen(screen, b.m_WorldXYZ);
    int32_t x = screen[0] + camX;
    int32_t y = screen[1] + camY;
    x += env.DrawOffsetX(&sprite);
    y += Anim(sprite).m_hotspotY;
    const screen::Rect own{x, y, static_cast<int32_t>(Anim(sprite).m_width) + x, static_cast<int32_t>(Anim(sprite).m_height) + y};
    const screen::Rect view{camX, camY, width + camX, height + camY};
    screen::Rect scratch;
    if (!env.Intersect(own, view, scratch)) return;

    int32_t index = 0;
    while (env.BuildingAt(index) != &b && index < env.BuildingCount()) ++index;
    for (int32_t i = index - 1; i >= 0; --i) {
        Building& other = *static_cast<Building*>(env.BuildingAt(i));
        const int32_t lightMap = other.m_lightMapOffset;
        if (!CastsLight(env, other) || lightMap == -1) continue;
        SpriteInstance& lit = *MainSprite(other);
        const int32_t previous = lit.m_animIndex;
        SetAnim(lit, lightMap);
        float world[3] = {other.m_WorldXYZ[0], other.m_WorldXYZ[1], other.m_WorldXYZ[2]};
        int32_t at[2];
        env.WorldToScreen(at, world);
        int32_t litX = at[0] + camX;
        int32_t litY = at[1] + camY;
        litX += env.DrawOffsetX(&lit);
        litY += Anim(lit).m_hotspotY;
        const screen::Rect litRect{
                litX, litY, static_cast<int32_t>(Anim(lit).m_width) + litX, static_cast<int32_t>(Anim(lit).m_height) + litY};
        screen::Rect clip;
        if (env.Intersect(own, litRect, clip)) {
            // The overlap in the pixels of this building's rectangle, then where it lands in the buffer.
            clip.left -= x;
            clip.bottom -= y;
            clip.top -= y;
            clip.right -= x;
            int32_t dstX = (clip.left - camX) + x;
            int32_t dstY = (clip.top - camY) + y;
            if ((clip.right - clip.left) + dstX < 0 || (clip.bottom - clip.top) + dstY < 0 || dstX > width - 1 || dstY > height - 1)
                continue;
            if (dstX < 0) {
                clip.left -= dstX;
                dstX = 0;
            }
            if (dstY < 0) {
                clip.top -= dstY;
                dstY = 0;
            }
            if ((clip.right - clip.left) + dstX >= width) clip.right = (clip.left - dstX) + width;
            // The bottom itself is added here, not the height of the overlap as it does for the right edge.
            if (clip.bottom + dstY >= height) clip.bottom = (clip.top - dstY) + height;
            if (clip.left == clip.right || clip.top == clip.bottom) continue;
            SpriteAnim* pick = sprite.m_sprite->m_anims[b.m_pickOffset];
            uint8_t* frame = env.Frame(pick, sprite.m_frameIndex, sprite.m_direction);
            const int32_t pickWidth = Anim(sprite).m_width, pickHeight = Anim(sprite).m_height;
            if (clip.bottom > pickHeight) clip.bottom = pickHeight;
            if (clip.right > pickWidth) clip.right = pickWidth;
            env.CopyRle(lightBuffer, frame, width, dstX, dstY, pickWidth, pickHeight, &clip);
        }
        SetAnim(lit, previous);
    }
}

// ---- Overlay -----------------------------------------------------------------------------------------------------------------------------------------

// The no-power sign (or the repair spanner) above the building's top point, drawn with the shortage sprite's size.
template<class Env> void DrawOverlay(Env& env, Building& b)
{
    const int32_t id = static_cast<int32_t>(b.m_id);
    if (env.Demolishing(id)) return;
    SpriteInstance* sign;
    if (b.m_closedForPower)
        sign = b.m_shortageSprite;
    else if (b.m_RepairState == 3)
        sign = b.m_spannerSprite;
    else
        return;
    int32_t screen[2];
    env.WorldToScreen(screen, b.m_TopWorldPos);
    const double zoom = static_cast<double>(env.ZoomScale());
    const int32_t x = FloatToIntWrap(static_cast<double>(screen[0]) * zoom);
    const int32_t y = FloatToIntWrap(static_cast<double>(screen[1]) * zoom);
    const SpriteAnim& size = Anim(*b.m_shortageSprite);
    env.Clipped(sign, x - (size.m_width >> 1), y - size.m_height * 2, 0, env.Time());
}

// ---- Draw -----------------------------------------------------------------------------------------------------------------------------------------------

// The coordinate read of ANIMSPRITE entry `i`: ints are read from the entry's X quad (isY false) or Y quad (isY true) at
// `direction` ints further on. Directions 0..3 are the entry's own four values, 4..7 read the other quad and 8 reads the flag
// (direction 9 on would lie past the entry, in the name text, which the record does not model: 0 here).
inline int32_t AnimSpriteInt(const BuildingDef& def, bool isY, uint32_t i, int32_t direction)
{
    const int32_t slot = (isY ? 4 : 0) + direction;
    if (i >= std::size(def.m_AnimSprites) || slot < 0 || slot > 8) return 0;
    const auto& sprite = def.m_AnimSprites[i];
    if (slot == 8) return sprite.m_animated;
    return slot < 4 ? sprite.m_X[slot] : sprite.m_Y[slot - 4];
}

// The building under construction or demolition shows the construction sprite (m_constructionSprite, the ghost) and the finished sprite as two
// partial previews. The ghost takes the direction of the finished sprite (clamped to its own direction count) and the progress is scaled by
// the ratio of the two sprites' heights so both end at the same time. `build` selects the build or the demolition arithmetic (the
// ratio is rounded to a float in only one of the two height orders of each).
template<class Env>
void ConstructionPreview(Env& env, Building& b, SpriteInstance& sprite, float progress, bool build, int32_t a, int32_t c)
{
    SpriteInstance& ghost = *GhostSprite(b);
    int32_t direction = sprite.m_direction;
    const int32_t directions = Anim(ghost).m_directions;
    if (direction >= directions) direction = directions - 1;
    ghost.m_direction = direction;
    const int32_t ghostHeight = Anim(ghost).m_height;
    const int32_t spriteHeight = Anim(sprite).m_height;
    const float* world = b.m_WorldXYZ;
    if (build) {
        if (ghostHeight > spriteHeight) {
            const double ratio = static_cast<double>(spriteHeight) / static_cast<double>(ghostHeight);
            env.PreviewAlt(&ghost, world, a, c, static_cast<float>(ratio * static_cast<double>(progress)), 0);
            env.PreviewAlt(&sprite, world, a, c, progress, 1);
        } else {
            const float ratio = static_cast<float>(static_cast<double>(ghostHeight) / static_cast<double>(spriteHeight));
            env.PreviewAlt(&ghost, world, a, c, progress, 0);
            env.PreviewAlt(&sprite, world, a, c, static_cast<float>(static_cast<double>(ratio) * static_cast<double>(progress)), 1);
        }
    } else {
        const float inverse = static_cast<float>(1.0 - static_cast<double>(progress));
        if (ghostHeight > spriteHeight) {
            const float ratio = static_cast<float>(static_cast<double>(ghostHeight) / static_cast<double>(spriteHeight));
            env.PreviewAlt(&ghost, world, a, c, inverse, 0);
            env.PreviewAlt(&sprite, world, a, c, static_cast<float>(static_cast<double>(inverse) * static_cast<double>(ratio)), 1);
        } else {
            const double ratio = static_cast<double>(spriteHeight) / static_cast<double>(ghostHeight);
            env.PreviewAlt(&ghost, world, a, c, static_cast<float>(static_cast<double>(inverse) * ratio), 0);
            env.PreviewAlt(&sprite, world, a, c, inverse, 1);
        }
    }
}

// The light map of the building (m_lightMapOffset animation, Blit into the light buffer), at the screen point of the world position.
template<class Env> void DrawLightMap(Env& env, Building& b, SpriteInstance& sprite, uint8_t* lightBuffer)
{
    SetAnim(sprite, b.m_lightMapOffset);
    const int32_t offset = env.DrawOffsetX(&sprite);
    const int32_t hotspotY = Anim(sprite).m_hotspotY;
    int32_t screen[2];
    env.WorldToScreen(screen, b.m_WorldXYZ);
    int32_t x = screen[0] + offset;
    int32_t y = screen[1] + hotspotY;
    int32_t left = 0, top = 0;
    int32_t width = Anim(sprite).m_width;
    int32_t height = Anim(sprite).m_height;
    if (env.ClipRect(&x, &y, &left, &top, &width, &height)) {
        SpriteAnim& anim = Anim(sprite);
        uint8_t* frame = env.Frame(&anim, sprite.m_frameIndex, sprite.m_direction);
        const int32_t zoom = env.ZoomLevel();
        const int32_t animWidth = Anim(sprite).m_width, animHeight = Anim(sprite).m_height;
        if (zoom == 1 || zoom == 0)
            env.Blit(lightBuffer, frame, env.Palette(), env.Width(), x, y, animWidth, animHeight, left, top, width, height);
        else if (zoom == 2)
            env.BlitZoomOut(lightBuffer, frame, env.Palette(), env.Width(), x, y, animWidth, animHeight, left, top, width, height);
    }
    SetAnim(sprite, 0);
}

template<class Env> void Draw(Env& env, Building& b)
{
    SpriteInstance& sprite = *MainSprite(b);
    const BuildingDef& def = *b.m_definition;
    const int32_t id = static_cast<int32_t>(b.m_id);
    const float* world = b.m_WorldXYZ;
    int32_t a = def.m_GridWidth, c = def.m_GridHeight;
    env.DrawOffsetX(&sprite);
    if (sprite.m_direction & 1) std::swap(a, c);

    // A burned-down site is baked into the background once; the finished building is not (yet) in the way.
    if (b.m_burn && !env.Building(id)) {
        env.ScreenCells(b);
        SetAnim(sprite, 0);
        env.Bake(&sprite, world, a, c);
        b.m_burn = 0;
        env.Preview(&sprite, world, a, c, 0);
    }

    if (!env.Building(id)) {
        if (!env.Demolishing(id)) {
            if (b.m_burnLimbo) {
                env.Preview(&sprite, world, a, c, 0);
                if (static_cast<double>(env.Time()) > static_cast<double>(b.m_timeInLimbo)) {
                    b.m_burn = 1;
                    b.m_burnLimbo = 0;
                    b.m_timeInLimbo = 0;
                }
            }
        } else {
            const float progress = env.DemolishProgress(id);
            if (!def.m_customConstruct)
                env.PreviewAlt(
                        &sprite, world, a, c, static_cast<float>((1.0 - static_cast<double>(progress)) - static_cast<double>(0.0001f)), 1);
            else
                ConstructionPreview(env, b, sprite, progress, false, a, c);
        }
    } else {
        const float progress = env.BuildProgress(id);
        if (!def.m_customConstruct)
            env.PreviewAlt(&sprite, world, a, c, progress, 1);
        else
            ConstructionPreview(env, b, sprite, progress, true, a, c);
    }

    int32_t screen[2];
    env.WorldToScreen(screen, world);
    if (!env.Building(id) && !env.Demolishing(id)) {
        if (env.LightEffects()) {
            const double now = env.DayTime(), last = env.LastDayTime();
            const double on = b.m_lightsOn, off = b.m_lightsOff;
            if (b.m_lightMapOffset != -1) {
                // The light map has to be rebuilt in the frame the lights switch on or off.
                if ((!Below(now, on) && Below(last, on)) || (!Below(now, off) && Below(last, off))) env.LightsDirty();
            }
            uint8_t* lightBuffer = env.LightBuffer();
            if (env.ZoomLevel() != 2) env.LightMask(b, lightBuffer);
            const bool daylight = Below(now, on) && now > off;
            if (b.m_OpenStatus != 0 || daylight || b.m_lightMapOffset == -1) {
                if (b.m_shadowOffset != -1 && !env.Building(id) && !env.Demolishing(id)) {
                    SetAnim(sprite, b.m_shadowOffset);
                    env.InWorld(&sprite, world, env.Time());
                    SetAnim(sprite, 0);
                }
            } else if (env.RenderLights()) {
                DrawLightMap(env, b, sprite, lightBuffer);
            }
        }

        if (def.m_NumAnimSprites != 0 && b.m_OpenStatus == 0) b.m_buildingAnimTime = env.Time();
        for (uint32_t i = 0; i < static_cast<uint32_t>(def.m_NumAnimSprites); ++i) {
            SpriteInstance& part = *b.m_animSprites[i];
            const int32_t direction = sprite.m_direction;
            int32_t x = AnimSpriteInt(def, false, i, direction) + screen[0];
            int32_t y = AnimSpriteInt(def, true, i, direction) + screen[1];
            if (part.m_sprite->m_type == 12) {
                env.InWorld(&part, world, def.m_AnimSprites[i].m_animated ? env.Time() : b.m_buildingAnimTime);
            } else {
                int32_t shift = 0;
                const SpriteAnim& partFirst = *part.m_sprite->m_anims[0];
                if (static_cast<uint32_t>(direction) <= 3) {
                    if (direction == 0 || direction == 2) {
                        shift = partFirst.m_hotspotX;
                    } else {
                        const SpriteAnim& first = *sprite.m_sprite->m_anims[0];
                        shift = partFirst.m_hotspotX - first.m_hotspotX * 2 - first.m_width;
                    }
                }
                x += shift;
                y += Anim(part).m_hotspotY;
                const uint32_t depth = env.Depth(world);
                env.Clipped(&part, x, y, depth, def.m_AnimSprites[i].m_animated ? env.Time() : b.m_buildingAnimTime);
            }
        }
    }

    // The toilets smoke and breed flies when they have not been cleaned for long; a broken building smokes above its top point.
    if (b.m_definition->m_Behavior == BuildingBehavior::Toilet && !env.Demolishing(id)) {
        if (static_cast<uint32_t>(b.m_VisitsSinceCleaned) > 0x1e) {
            SpriteInstance* smoke = env.CreateSprite("test_smoke");
            env.InWorld(smoke, world, env.Time());
            env.ReleaseSprite(smoke);
        }
        if (static_cast<uint32_t>(b.m_VisitsSinceCleaned) > 0x14) {
            SpriteInstance* flies = env.CreateSprite("fly");
            env.InWorld(flies, world, env.Time());
            env.ReleaseSprite(flies);
        }
    }
    if (b.m_RepairState == 3 && !env.Demolishing(id)) {
        int32_t top[2];
        env.WorldToScreen(top, b.m_TopWorldPos);
        SpriteInstance* smoke = env.CreateSprite("smoke");
        const SpriteAnim& anim = Anim(*smoke);
        const uint32_t height = anim.m_height;
        env.Clipped(smoke, top[0] - (anim.m_width >> 1), top[1] - static_cast<int32_t>(height), 0xffffu - (height << 4), env.Time());
        env.ReleaseSprite(smoke);
    }
}

} // namespace obl::building_draw
