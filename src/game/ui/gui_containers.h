#pragma once

#include "game/ui/gui_canvas.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_containers_types.h"

#include <cstdint>

// `GuiDefine` and `GuiMainItem`: containers that keep their children in a circular `std::list` and forward
// almost every virtual method to them, and the seven procedural backgrounds `GuiMainItem::Draw` paints. All game-heap and
// virtual-call work goes through an `Env`, so the unit tests run the same code on plain memory.
namespace obl::gui_containers {

using Node = ui::GuiContainerChildNode;
using Widget = ui::GuiWidget;
using gui_widgets::Add;
using gui_widgets::Sub;

inline constexpr int32_t kDefineType = 3;
inline constexpr int32_t kMainItemType = 0xb;
inline constexpr size_t kNodeBytes = sizeof(Node);

template<class Callback> void ForEachChild(Node* const& children, Callback&& callback)
{
    Node* node = children->m_next;
    if (node == children) return;
    do {
        Widget* child = node->m_widget;
        callback(*child);
        node = node->m_next;
    } while (node != children);
}

// Same walk for the methods that stop at the first child that answers true (the loop tests the previous answer before the next call).
// Returns the last answer, so an empty list gives 0.
template<class Callback> uint8_t FirstChildAnswer(Node* const& children, Callback&& callback)
{
    uint8_t answer = 0;
    Node* node = children->m_next;
    while (node != children) {
        if (answer) break;
        answer = callback(*node->m_widget);
        node = node->m_next;
    }
    return answer;
}

// std::list::push_back of a widget pointer (after = end): a pool node linked in front of the sentinel.
template<class Env> void AddChild(Node* const& children, Widget* child, Env& env)
{
    auto* node = static_cast<Node*>(env.AllocateNode());
    node->m_widget = child;
    Node* end = children;
    Node* last = end->m_prev;
    node->m_next = end;
    node->m_prev = last;
    last->m_next = node;
    end->m_prev = node;
}

template<class Env> void InitList(Node*& slot, Env& env)
{
    slot = nullptr;
    auto* sentinel = static_cast<Node*>(env.AllocateNode());
    sentinel->m_next = sentinel;
    sentinel->m_prev = sentinel;
    slot = sentinel;
}

template<class Env> void DestroyList(Node* const& slot, Env& env)
{
    Node* sentinel = slot;
    Node* node = sentinel->m_next;
    while (node != sentinel) {
        Node* next = node->m_next;
        env.FreeNode(node);
        node = next;
    }
    sentinel->m_next = sentinel;
    sentinel->m_prev = sentinel;
    env.FreeNode(sentinel);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// GuiDefine (type 3). Every method below walks the children; nothing is drawn by the template itself.

// The rectangle comes from GuiWidget's constructor, the tooltip goes with GuiLeaf's destructor.
template<class Env> void DefineConstruct(ui::GuiDefineData& define, const char* name, Env& env)
{
    InitList(define.m_children, env);
    define.m_name = static_cast<char*>(env.CopyString(name));
    define.m_type = kDefineType;
}

template<class Env> void DefineDestruct(ui::GuiDefineData& define, Env& env)
{
    if (define.m_name) env.Free(define.m_name);
    DestroyList(define.m_children, env);
}

template<class Env> void DefineSetOrigin(ui::GuiDefineData& define, int dx, int dy, Env& env)
{
    ForEachChild(define.m_children, [&](Widget& w) { env.SetOrigin(w, dx, dy); });
}
template<class Env> void DefineTranslate(ui::GuiDefineData& define, int dx, int dy, Env& env)
{
    ForEachChild(define.m_children, [&](Widget& w) { env.Translate(w, dx, dy); });
}
template<class Env> void DefineRefresh(ui::GuiDefineData& define, Env& env)
{
    ForEachChild(define.m_children, [&](Widget& w) { env.Refresh(w); });
}
template<class Env> void DefineSelectTab(ui::GuiDefineData& define, int index, Env& env)
{
    ForEachChild(define.m_children, [&](Widget& w) { env.SelectTab(w, index); });
}
template<class Env> void DefineSetVisible(ui::GuiDefineData& define, uint32_t visible, Env& env)
{
    ForEachChild(define.m_children, [&](Widget& w) { env.SetVisible(w, visible); });
}
template<class Env> void DefineDraw(ui::GuiDefineData& define, Env& env)
{
    ForEachChild(define.m_children, [&](Widget& w) { env.Draw(w); });
}
template<class Env> void DefineInvalidate(ui::GuiDefineData& define, Env& env)
{
    ForEachChild(define.m_children, [&](Widget& w) { env.Invalidate(w); });
}
template<class Env> void DefineUpdate(ui::GuiDefineData& define, uint32_t dt, Env& env)
{
    ForEachChild(define.m_children, [&](Widget& w) { env.Update(w, dt); });
}

// ---------------------------------------------------------------------------------------------------------------------------------
// GuiMainItem (type 0xb).

// The rectangle comes from GuiWidget's constructor, the tooltip goes with GuiLeaf's destructor.
template<class Env> void MainItemConstruct(ui::GuiMainItemData& mainItem, int variant, Env& env)
{
    mainItem.m_variant = variant;
    mainItem.m_index = 0;
    InitList(mainItem.m_children, env);
    mainItem.m_type = kMainItemType;
}

template<class Env> void MainItemDestruct(ui::GuiMainItemData& mainItem, Env& env)
{
    DestroyList(mainItem.m_children, env);
}

// The base HitTest (non-virtual, it also sets the hover flag) comes first; only a miss asks the children.
template<class Env> uint8_t MainItemHitTest(ui::GuiMainItemData& mainItem, int x, int y, Env& env)
{
    if (env.BaseHitTest(mainItem, x, y)) return 1;
    return FirstChildAnswer(mainItem.m_children, [&](Widget& w) { return env.HitTest(w, x, y); });
}

// Clears both press flags, stores the (virtual) hit test as the hover flag, offers the click to the children first and only then
// takes it itself while it is hovered: state 1 = click, state 2 = held.
template<class Env> uint8_t MainItemOnMouseLeft(ui::GuiMainItemData& mainItem, int x, int y, int state, Env& env)
{
    mainItem.m_clicked = 0;
    mainItem.m_held = 0;
    mainItem.m_hovered = env.HitTest(mainItem, x, y);
    const uint8_t handled = FirstChildAnswer(mainItem.m_children, [&](Widget& w) { return env.OnMouseLeft(w, x, y, state); });
    if (handled) return handled;
    if (!env.IsHovered(mainItem)) return 0;
    if (state == 1) {
        mainItem.m_clicked = 1;
        return 1;
    }
    if (state == 2) {
        mainItem.m_held = 1;
        return 1;
    }
    return 0;
}

template<class Env> uint8_t MainItemOnMouseRight(ui::GuiMainItemData& mainItem, int x, int y, int state, Env& env)
{
    return FirstChildAnswer(mainItem.m_children, [&](Widget& w) { return env.OnMouseRight(w, x, y, state); });
}

template<class Env> void MainItemUpdate(ui::GuiMainItemData& mainItem, uint32_t dt, Env& env)
{
    ForEachChild(mainItem.m_children, [&](Widget& w) { env.Update(w, dt); });
}
// Unlike the base, this does not store the flag: only the children learn about it.
template<class Env> void MainItemSetVisible(ui::GuiMainItemData& mainItem, uint32_t visible, Env& env)
{
    ForEachChild(mainItem.m_children, [&](Widget& w) { env.SetVisible(w, visible); });
}
// The bottom edge follows the top edge (the top is `m_y0`, the stored rectangle is not the base one).
inline void MainItemSetHeight(ui::GuiMainItemData& mainItem, int height)
{
    mainItem.m_y1 = Add(mainItem.m_y0, height);
}

template<class Env> void MainItemInvalidate(ui::GuiMainItemData& mainItem, Env& env)
{
    mainItem.m_dirty = 1;
    ForEachChild(mainItem.m_children, [&](Widget& w) { env.Invalidate(w); });
}
template<class Env> void MainItemDrawOverlay(ui::GuiMainItemData& mainItem, Env& env)
{
    ForEachChild(mainItem.m_children, [&](Widget& w) { env.DrawOverlay(w); });
}

class Primitives {
public:
    virtual ~Primitives() = default;
    virtual uint32_t Pack(int r, int g, int b) = 0;
    virtual void Line(int x0, int y0, int x1, int y1, uint32_t color) = 0;
    virtual const char* ScrollBarSliderSprite() = 0; // g_GuiScrollBarSliderSprite
    virtual const char* ScrollBarTrackSprite() = 0; // g_GuiScrollBarTrackSprite
    virtual const char* IndexedSprite(int index) = 0;
};

// Rectangle `r` is {left, top, right, bottom}. Variants 2, 3 and 7 are the same bevelled panel with a different edge set:
// 2 = left, right, bottom; 3 = left, right and a strip above the top; 7 = all four edges. The inner fill follows.
namespace backgrounds {

inline void Panel(const int32_t* r, int variant, gui_canvas::Canvas& canvas, Primitives& prim)
{
    const int32_t x0 = r[0], y0 = r[1], x1 = r[2], y1 = r[3];
    const auto edge = static_cast<uint16_t>(prim.Pack(0x7a, 0x98, 0xfa));
    canvas.FillRect(x0, y0, Add(x0, 2), y1, edge);
    canvas.FillRect(Sub(x1, 2), y0, x1, y1, edge);
    if (variant == 3)
        canvas.FillRect(x0, Sub(y0, 2), x1, y0, edge);
    else
        canvas.FillRect(x0, Sub(y1, 2), x1, y1, edge);
    if (variant == 7) canvas.FillRect(x0, y0, x1, Add(y0, 2), edge);
    const auto inner = static_cast<uint16_t>(prim.Pack(0xeb, 0xe9, 0xf9));
    const int32_t top = variant == 7 ? Add(y0, 2) : y0;
    canvas.FillRect(Add(x0, 2), top, Sub(x1, 2), Sub(y1, 2), inner);
}

// Variant 6: three horizontal highlight lines along the top edge, inset by one pixel on the left and two on the right.
inline void Highlight(const int32_t* r, Primitives& prim)
{
    const int32_t x0 = r[0], y0 = r[1], x1 = r[2];
    const int32_t from = Add(x0, 1), to = Sub(x1, 2);
    prim.Line(from, y0, to, y0, prim.Pack(0x25, 0x42, 0xbd));
    prim.Line(from, Add(y0, 1), to, Add(y0, 1), prim.Pack(0x93, 0xb2, 0xfd));
    prim.Line(from, Add(y0, 2), to, Add(y0, 2), prim.Pack(0xbb, 0xd0, 0xff));
}

// Variant 4: scroll bar track, seven shaded columns on the left, then the sprite frames 0 (top) and 1 (bottom) of the track bitmap.
inline void ScrollTrack(const int32_t* r, gui_canvas::Canvas& canvas, Primitives& prim)
{
    const int32_t x0 = r[0], y0 = r[1], y1 = r[3];
    const int32_t bottom = Sub(y1, 1);
    int32_t x = x0;
    prim.Line(x, y0, x, bottom, prim.Pack(0xb7, 0xce, 0xfe));
    x = Add(x, 1);
    prim.Line(x, y0, x, bottom, prim.Pack(0xb2, 0xc9, 0xfe));
    x = Add(x, 1);
    prim.Line(x, y0, x, bottom, prim.Pack(0xa0, 0xbc, 0xfd));
    x = Add(x, 1);
    const auto fill = static_cast<uint16_t>(prim.Pack(0x93, 0xb2, 0xfd));
    const int32_t fillRight = Add(x, 8);
    canvas.FillRect(x, y0, fillRight, bottom, fill);
    x = fillRight;
    prim.Line(x, y0, x, bottom, prim.Pack(0x83, 0xa2, 0xfa));
    x = Add(x, 1);
    prim.Line(x, y0, x, bottom, prim.Pack(0x5a, 0x7b, 0xf2));
    x = Add(x, 1);
    prim.Line(x, y0, x, bottom, prim.Pack(0x2e, 0x51, 0xe9));

    const auto sprite = canvas.CreateSprite(prim.ScrollBarTrackSprite(), gui_canvas::CreateCall::Sprite);
    canvas.SetAnimType(sprite, 3);
    canvas.SetFrame(sprite, 0);
    canvas.DrawClipped(sprite, x0, y0, 0);
    canvas.SetFrame(sprite, 1);
    canvas.DrawClipped(sprite, x0, Sub(y1, canvas.Metrics(sprite).height), 0);
    canvas.ReleaseSprite(sprite);
}

// Variant 5: scroll bar slider. Cap sprite (frame 0) on top, a body of shaded columns whose length is what the two caps leave,
// cap sprite (frame 1) below. A rectangle shorter than two caps has no body at all.
inline void ScrollSlider(const int32_t* r, gui_canvas::Canvas& canvas, Primitives& prim)
{
    const int32_t x0 = r[0], y0 = r[1], y1 = r[3];
    const auto sprite = canvas.CreateSprite(prim.ScrollBarSliderSprite(), gui_canvas::CreateCall::Sprite);
    canvas.SetAnimType(sprite, 3);
    const int capHeight = canvas.Metrics(sprite).height;
    int32_t body = Sub(Sub(y1, capHeight * 2), y0);
    if (body < 0) body = 0;
    canvas.SetFrame(sprite, 0);
    canvas.DrawClipped(sprite, x0, y0, 0);
    int32_t y = Add(y0, capHeight);
    if (body != 0) {
        const int32_t bottom = Add(y, body);
        int32_t x = x0;
        prim.Line(x, y, x, bottom, prim.Pack(0x68, 0xa5, 0x7a));
        x = Add(x, 1);
        prim.Line(x, y, x, bottom, prim.Pack(0x42, 0x9f, 0x5a));
        x = Add(x, 1);
        prim.Line(x, y, x, bottom, prim.Pack(0x39, 0xb6, 0x64));
        x = Add(x, 1);
        const auto fill = static_cast<uint16_t>(prim.Pack(0x43, 0xc5, 0x73));
        const int32_t fillRight = Add(x, 6);
        canvas.FillRect(x, y, fillRight, bottom, fill);
        x = fillRight;
        prim.Line(x, y, x, bottom, prim.Pack(0x7a, 0xd8, 0x9e));
        x = Add(x, 1);
        prim.Line(x, y, x, bottom, prim.Pack(0xb3, 0xeb, 0xcb));
        x = Add(x, 1);
        prim.Line(x, y, x, bottom, prim.Pack(0x68, 0xa5, 0x7a));
    }
    canvas.SetFrame(sprite, 1);
    canvas.DrawClipped(sprite, x0, Add(y, body), 0);
    canvas.ReleaseSprite(sprite);
}

// Variant 1: a caption bar whose left end is the sprite `names[index]` (frame 0) and right end its frame 1, joined by three
// shaded lines on top, a light fill and three lines below, each starting one sprite width in from the left.
inline void Caption(const int32_t* r, int index, gui_canvas::Canvas& canvas, Primitives& prim)
{
    const int32_t x0 = r[0], y0 = r[1], x1 = r[2];
    const auto sprite = canvas.CreateSprite(prim.IndexedSprite(index), gui_canvas::CreateCall::Sprite);
    canvas.SetAnimType(sprite, 3);
    canvas.SetFrame(sprite, 0);
    const int width = canvas.Metrics(sprite).width;
    canvas.DrawClipped(sprite, x0, y0, 0);
    const int32_t from = Add(x0, width);
    prim.Line(from, y0, x1, y0, prim.Pack(0x6d, 0x6b, 0x82));
    prim.Line(from, Add(y0, 1), x1, Add(y0, 1), prim.Pack(0x9f, 0x9d, 0xb1));
    prim.Line(from, Add(y0, 2), x1, Add(y0, 2), prim.Pack(0xd7, 0xd5, 0xe7));
    const auto fill = static_cast<uint16_t>(prim.Pack(0xeb, 0xe9, 0xf9));
    canvas.FillRect(from, Add(y0, 3), Sub(x1, 1), Add(y0, 13), fill);
    prim.Line(from, Add(y0, 13), x1, Add(y0, 13), prim.Pack(0xee, 0xec, 0xfa));
    prim.Line(from, Add(y0, 14), x1, Add(y0, 14), prim.Pack(0xf2, 0xf0, 0xfb));
    prim.Line(from, Add(y0, 15), x1, Add(y0, 15), prim.Pack(0xf3, 0xf2, 0xfb));
    canvas.SetFrame(sprite, 1);
    canvas.DrawClipped(sprite, Add(Sub(x1, canvas.Metrics(sprite).width), 1), y0, 0);
    canvas.ReleaseSprite(sprite);
}

} // namespace backgrounds

// GuiMainItem::Draw picks a background by variant: 1..7 -> helper; 0 and anything else paint nothing.
inline bool HasBackground(int32_t variant)
{
    return static_cast<uint32_t>(variant - 1) <= 6;
}

inline void DrawBackground(int32_t variant, const int32_t* rect, int32_t index, gui_canvas::Canvas& canvas, Primitives& prim)
{
    switch (variant) {
    case 1: backgrounds::Caption(rect, index, canvas, prim); break;
    case 2:
    case 3:
    case 7: backgrounds::Panel(rect, variant, canvas, prim); break;
    case 4: backgrounds::ScrollTrack(rect, canvas, prim); break;
    case 5: backgrounds::ScrollSlider(rect, canvas, prim); break;
    case 6: backgrounds::Highlight(rect, prim); break;
    default: break;
    }
}

// Paints the background once per invalidation, then lets every child draw (they keep their own dirty flags).
template<class Env> void MainItemDraw(ui::GuiMainItemData& mainItem, gui_canvas::Canvas& canvas, Primitives& prim, Env& env)
{
    if (mainItem.m_dirty) {
        const int32_t rect[4] = {mainItem.m_x0, mainItem.m_y0, mainItem.m_x1, mainItem.m_y1};
        DrawBackground(mainItem.m_variant, rect, mainItem.m_index, canvas, prim);
        mainItem.m_dirty = 0;
    }
    ForEachChild(mainItem.m_children, [&](Widget& w) { env.Draw(w); });
}

} // namespace obl::gui_containers

namespace obl::ui {
struct GuiDefine;
struct GuiDialogData;
struct GuiMainItem;
struct GuiTab;
struct GuiTabWindow;
struct GuiWidget;
} // namespace obl::ui

namespace obl::gui {
// Registers the GuiDefine and GuiMainItem implementations.

namespace container {

ui::GuiDefine* ConstructDefine(ui::GuiDefine* define, const char* name);
ui::GuiMainItem* ConstructMainItem(ui::GuiMainItem* mainItem, int x0, int y0, int x1, int y1, int variant);
ui::GuiTab* ConstructTab(ui::GuiTab* tab, int x0, int y0, int x1, int y1, const char* sprite);
ui::GuiTabWindow* ConstructTabWindow(ui::GuiTabWindow* tabWindow, int x0, int y0, int x1, int y1);
ui::GuiDialog* ConstructDialog(
        ui::GuiDialog* dialog, const char* name, int x0, int y0, int x1, int y1, const char* sprite, int32_t key, bool movable);
void MainItemSetHeight(ui::GuiMainItemData* mainItem, int height);
uint8_t TabMouse(ui::GuiTabData* tab, int x, int y, int state, bool active);
void DialogSelectTab(ui::GuiDialogData* dialog, int index);
// The speed is a float passed as its bits.
void DialogStartAnimation(ui::GuiDialogData* dialog, int type, uint32_t speedBits, int delay);

} // namespace container
} // namespace obl::gui
