#pragma once

#include "core/ascii_case.h"
#include "core/float_to_int.h"
#include "game/ui/gui_canvas.h"
#include "game/ui/gui_containers.h"
#include "game/ui/gui_tabs.h"
#include "game/ui/gui_containers_types.h"

#include <bit>
#include <cmath>
#include <cstdint>

// `GuiDialogData`: the window of the GUI script. It keeps its widgets in a circular `std::list`, paints a background sprite,
// can be dragged by a title bar (a `GuiMainItem` over its top rows) and slides in and out with a small timer-driven animation. All game-heap
// and virtual-call work goes through an `Env`, so the unit tests run the same code on plain memory.
namespace obl::gui_dialog {

using gui_containers::FirstChildAnswer;
using gui_containers::ForEachChild;
using gui_tabs::ForEachChildListsLast;
using gui_widgets::Add;
using gui_widgets::Sub;
using Dialog = ui::GuiDialogData;
using Widget = ui::GuiWidget;

inline constexpr int32_t kDialogType = 4;
inline constexpr int32_t kCaptionHeight = 0x1e; // the title bar covers the top 30 rows
inline constexpr int32_t kScreenMargin = 0x14; // a dragged window keeps this many pixels of its far edge on screen
inline constexpr int32_t kCursorOutside = -300; // the "no cursor" coordinate GuiManager sends while the mouse is off the window
inline constexpr uint32_t kBackgroundDepth = 0xfff; // clipped-draw depth of the background sprite
inline constexpr uint16_t kClearColor = 0xffff; // fill of a dialog without sprite ("white" in either RGB555 or RGB565)
inline constexpr int32_t kSlideInType = 1, kSlideOutType = 2, kSlideToType = 3, kSlideBackType = 4;
inline constexpr int32_t kSlideOutMargin = 100; // type 2 travels until the window is this far past the right edge
inline constexpr double kAnimationEpsilon = 1.1920928955078125e-07; // FLT_EPSILON, "the animation has run out"

struct Size {
    int width = 0;
    int height = 0;
};

template<class Env>
void DialogConstruct(Dialog& dialog, const char* name, const char* sprite, int32_t positionKey, uint8_t movable, Env& env)
{
    dialog.m_movable = movable;
    gui_containers::InitList(dialog.m_children, env);
    dialog.m_positionKey = positionKey;
    dialog.m_name = static_cast<char*>(env.CopyString(name));
    // The script's sprite names compare as `_stricmp` did (ASCII case folding).
    if (ascii::EqualsNoCase(sprite, "CLEAR"))
        dialog.m_clear = 1;
    else if (!ascii::EqualsNoCase(sprite, "NONE"))
        dialog.m_sprite = env.CreateSprite(sprite);
    dialog.m_caption = env.NewCaption(dialog.m_x0, dialog.m_y0, dialog.m_x1, Add(dialog.m_y0, kCaptionHeight));
    dialog.m_type = kDialogType;
}

// The title bar is destroyed through its own virtual destructor (and freed); the children belong to the parser.
template<class Env> void DialogDestruct(Dialog& dialog, Env& env)
{
    if (dialog.m_sprite) env.ReleaseSprite(dialog.m_sprite);
    if (dialog.m_name) env.Free(dialog.m_name);
    if (dialog.m_caption) env.DeleteWidget(*dialog.m_caption);
    gui_containers::DestroyList(dialog.m_children, env);
}

template<class Env> void DialogSetVisible(Dialog& dialog, uint32_t visible, Env& env)
{
    env.BaseSetVisible(dialog, visible);
    if (static_cast<uint8_t>(visible)) dialog.m_minimized = 0;
    ForEachChild(dialog.m_children, [&](Widget& w) { env.SetVisible(w, visible); });
}

template<class Env> uint8_t DialogOnMouseWheel(Dialog& dialog, int x, int y, int delta, Env& env)
{
    return FirstChildAnswer(dialog.m_children, [&](Widget& w) { return env.OnMouseWheel(w, x, y, delta); });
}

template<class Env> void DialogRefresh(Dialog& dialog, Env& env)
{
    ForEachChild(dialog.m_children, [&](Widget& w) { env.Refresh(w); });
}

template<class Env> void DialogSelectTab(Dialog& dialog, int index, Env& env)
{
    ForEachChild(dialog.m_children, [&](Widget& w) { env.SelectTab(w, index); });
}

// Only the templates (`Define`) and tab windows follow the window: they are told its position, the arguments are ignored.
template<class Env> void DialogSetOrigin(Dialog& dialog, Env& env)
{
    ForEachChild(dialog.m_children, [&](Widget& w) {
        if (w.m_type == gui_containers::kDefineType || w.m_type == gui_tabs::kTabWindowType) env.SetOrigin(w, dialog.m_x0, dialog.m_y0);
    });
}

// Moves the title bar and every child, then tells GuiManager where the window is now (it remembers the position under `m_positionKey`).
template<class Env> void DialogTranslate(Dialog& dialog, int dx, int dy, Env& env)
{
    env.BaseTranslate(dialog, dx, dy);
    env.Translate(*dialog.m_caption, dx, dy);
    ForEachChild(dialog.m_children, [&](Widget& w) { env.Translate(w, dx, dy); });
    int32_t bounds[4];
    env.BaseBounds(dialog, bounds);
    env.RememberPosition(bounds, dialog.m_positionKey);
}

template<class Env> void DialogInvalidate(Dialog& dialog, Env& env)
{
    env.BaseInvalidate(dialog);
    ForEachChild(dialog.m_children, [&](Widget& w) { env.Invalidate(w); });
}

template<class Env> void DialogDrawOverlay(Dialog& dialog, Env& env)
{
    ForEachChildListsLast(dialog.m_children, [&](Widget& w) { env.DrawOverlay(w); });
}

// The background is painted once per invalidation (a sprite at depth 0xfff, or white unless the script said "CLEAR"); the children draw every
// frame, lists last so an open drop-down covers its neighbours. `sprite` is the canvas handle of `m_sprite` (kNoSprite without one).
template<class Env> void DialogDraw(Dialog& dialog, gui_canvas::Canvas& canvas, gui_canvas::SpriteId sprite, Env& env)
{
    if (dialog.m_dirty) {
        if (dialog.m_sprite)
            canvas.DrawClipped(sprite, dialog.m_x0, dialog.m_y0, kBackgroundDepth);
        else if (!dialog.m_clear)
            canvas.FillRect(dialog.m_x0, dialog.m_y0, Sub(dialog.m_x1, 1), Sub(dialog.m_y1, 1), kClearColor);
        dialog.m_dirty = 0;
    }
    ForEachChildListsLast(dialog.m_children, [&](Widget& w) { env.Draw(w); });
}

template<class Env> void DialogAddChild(Dialog& dialog, Widget* child, Env& env)
{
    gui_containers::AddChild(dialog.m_children, child, env);
}

// Strictly inside the sprite's current animation frame the opaque-pixel test decides; a dialog without sprite is a plain rectangle. The hover
// flag (`m_hovered`) is written either way, and the answer is the raw byte of the test.
template<class Env> uint8_t DialogHitTest(Dialog& dialog, int x, int y, Env& env)
{
    uint8_t hit = 0;
    if (!dialog.m_sprite) {
        hit = env.BaseHitTest(dialog, x, y);
    } else {
        const int localX = Sub(x, dialog.m_x0);
        if (localX > 0 && localX < env.SpriteSize(dialog.m_sprite).width) {
            const int localY = Sub(y, dialog.m_y0);
            if (localY > 0 && localY < env.SpriteSize(dialog.m_sprite).height) hit = env.SpriteHit(dialog.m_sprite, localX, localY);
        }
    }
    dialog.m_hovered = hit;
    return hit;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Animation

// `type` 1 starts one window width to the right of its place (the first Update slides it in), 2..4 only record their parameters.
template<class Env> void DialogStartAnimation(Dialog& dialog, int type, uint32_t speedBits, int delay, Env& env)
{
    const int screenWidth = env.ScreenWidth();
    dialog.m_animTime = std::bit_cast<float>(speedBits);
    dialog.m_animDuration = std::bit_cast<float>(speedBits);
    dialog.m_animType = type;
    dialog.m_animating = 1;
    dialog.m_animTarget = delay;
    if (type == kSlideInType) env.Translate(dialog, Sub(screenWidth, dialog.m_baseX0), 0);
}

template<class Env> void SlideBy(Dialog& dialog, int distance, Env& env)
{
    const double left = static_cast<double>(dialog.m_animDuration) - static_cast<double>(dialog.m_animTime);
    const double scaled = static_cast<double>(distance) * left / static_cast<double>(dialog.m_animDuration);
    env.Translate(dialog, FloatToIntWrap(scaled), 0);
}

// Ticks the timer, moves the window towards its goal (types 1 and 4: to the base position; 2: out past the right edge; 3: to `m_animTarget`),
// invalidates and, once the time ran out, ends the animation (type 2 closes the dialog by name). The children update every frame.
template<class Env> void DialogUpdate(Dialog& dialog, uint32_t dtBits, Env& env)
{
    if (dialog.m_animating) {
        const double remaining = static_cast<double>(dialog.m_animTime) - static_cast<double>(std::bit_cast<float>(dtBits));
        dialog.m_animTime = static_cast<float>(remaining);
        if (!(remaining >= 0.0)) dialog.m_animTime = 0.0f;
        switch (dialog.m_animType) {
        case kSlideInType:
        case kSlideBackType: SlideBy(dialog, Sub(dialog.m_baseX0, dialog.m_x0), env); break;
        case kSlideOutType: {
            const int screenWidth = env.ScreenWidth();
            SlideBy(dialog, Add(Sub(screenWidth, dialog.m_x0), kSlideOutMargin), env);
            if (screenWidth < dialog.m_x0) dialog.m_animTime = 0.0f;
            break;
        }
        case kSlideToType: SlideBy(dialog, Add(Sub(dialog.m_animTarget, dialog.m_x0), dialog.m_baseX0), env); break;
        default: break;
        }
        env.Invalidate(dialog);
        if (!(std::fabs(static_cast<double>(dialog.m_animTime)) > kAnimationEpsilon)) {
            dialog.m_animTime = 0.0f;
            dialog.m_animating = 0;
            if (dialog.m_animType == kSlideOutType) env.CloseDialog(dialog.m_name);
            dialog.m_animType = 0;
            env.Invalidate(dialog);
        }
    }
    ForEachChild(dialog.m_children, [&](Widget& w) { env.Update(w, dtBits); });
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Mouse

// One step of a title-bar drag: the window follows the cursor but never leaves less than 20 pixels of itself on screen (each axis is dropped
// on its own). The first step after the press (no previous position) only records the cursor. `x`/`y` equal to -300 mean "cursor off the
// window": the previous position is reused.
template<class Env> void DragStep(Dialog& dialog, int x, int y, Env& env)
{
    if (x == kCursorOutside || y == kCursorOutside) {
        x = dialog.m_dragX;
        y = dialog.m_dragY;
    }
    if (dialog.m_dragX != 0 && dialog.m_dragY != 0) {
        int dx = Sub(x, dialog.m_dragX);
        int dy = Sub(y, dialog.m_dragY);
        const int width = env.ScreenWidth();
        const int height = env.ScreenHeight();
        if (Add(dialog.m_x0, dx) > Sub(width, kScreenMargin)) dx = 0;
        if (Add(dialog.m_x1, dx) < kScreenMargin) dx = 0;
        if (Add(dialog.m_y0, dy) > Sub(height, kScreenMargin)) dy = 0;
        if (Add(dialog.m_y1, dy) < kScreenMargin) dy = 0;
        if (dx != 0 || dy != 0) {
            env.Translate(dialog, dx, dy);
            env.InvalidateDialog(dialog.m_name);
        }
    }
    dialog.m_dragX = x;
    dialog.m_dragY = y;
}

// `state` 1 = press, 2 = held, anything else = release/other. The window's own hit test runs first only for its hover flag. A press goes to
// the children, then to the title bar (which starts a drag unless the dialog is modal or fixed); while held the children are offered the event
// unless a drag is running; any other state ends the drag. Nothing is handled while the window is sliding. The answer is the byte a child
// returned, else "hovered and pressed".
template<class Env> uint8_t DialogOnMouseLeft(Dialog& dialog, int x, int y, int state, Env& env)
{
    uint8_t taken = 0;
    env.HitTest(dialog, x, y);
    if (state == 1) {
        if (!dialog.m_animating) {
            taken = FirstChildAnswer(dialog.m_children, [&](Widget& w) { return env.OnMouseLeft(w, x, y, 1); });
            if (!taken && env.HitTest(*dialog.m_caption, x, y) && !dialog.m_modal && dialog.m_movable) {
                dialog.m_dragging = 1;
                taken = 1;
            }
        }
    } else if (state == 2) {
        if (dialog.m_dragging) {
            DragStep(dialog, x, y, env);
            taken = 1;
        } else if (!dialog.m_animating) {
            taken = FirstChildAnswer(dialog.m_children, [&](Widget& w) { return env.OnMouseLeft(w, x, y, 2); });
        }
    } else {
        if (!dialog.m_animating) taken = FirstChildAnswer(dialog.m_children, [&](Widget& w) { return env.OnMouseLeft(w, x, y, state); });
        dialog.m_dragging = 0;
    }
    if (!dialog.m_dragging) {
        dialog.m_dragX = 0;
        dialog.m_dragY = 0;
    }
    if (taken) return taken;
    return env.IsHovered(dialog) && state == 1 ? 1 : 0;
}

} // namespace obl::gui_dialog
