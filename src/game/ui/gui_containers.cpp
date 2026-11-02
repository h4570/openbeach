#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/sound_api.h"
#include "game/ui/renderer_api.h"
#include "game/ui/line_draw.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_data.h"
#include "game/ui/gui_containers.h"

#include "game/game/game_accessors.h"
#include "game/game/startup_tables.h"
#include "game/ui/gui_canvas_live.h"
#include "game/ui/gui_dialog.h"
#include "game/ui/gui_tabs.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <bit>
#include <new>
#include <vector>

// The container widgets: GuiDefine, GuiMainItem, GuiTab, GuiTabWindow and GuiDialog. The logic is src/game/ui/gui_containers.h; this file adds the game side (virtual calls on the children, the list
// nodes, the game heap, Renderer colour and line functions).
namespace obl::gui {
namespace {

using Widget = ui::GuiWidget;
using Define = ui::GuiDefine;
using Item = ui::GuiMainItem;
using Node = ui::GuiContainerChildNode;
using Tab = ui::GuiTab;
using Window = ui::GuiTabWindow;
using TabNode = ui::GuiTabNode;
using DialogData = ui::GuiDialogData;
using Dialog = ui::GuiDialog;

// Call counters (`call-report ui.gui-containers`).
test::CallCounter g_counter("ui.gui-containers");
// Native tab buttons painted (a frame test sees that its scene reaches the tab code).
test::CallCounter g_tabDrawn("GuiTabButtonDrawn");
// Native dialog frames: backgrounds painted and animation steps taken (a frame test sees that its scene reaches the dialog code).
test::CallCounter g_dialogDrawn("GuiDialogBackgroundDrawn"), g_dialogSlid("GuiDialogSlideStep");
// Native backgrounds painted per variant (a frame test sees which ones its scene reaches).
test::CallCounter g_variantDrawn[8] = {test::CallCounter("GuiMainItemVariant0"), test::CallCounter("GuiMainItemVariant1"),
        test::CallCounter("GuiMainItemVariant2"), test::CallCounter("GuiMainItemVariant3"), test::CallCounter("GuiMainItemVariant4"),
        test::CallCounter("GuiMainItemVariant5"), test::CallCounter("GuiMainItemVariant6"), test::CallCounter("GuiMainItemVariant7")};

// Thin wrappers over the game functions the containers call; GameEnv below goes through them.
void __cdecl CallFree(void* block)
{
    ::operator delete(block);
}

void CallInvalidateDialogs(void* manager, char sound)
{
    static_cast<ui::GuiManager*>(manager)->InvalidateAllDialogs(sound);
}

int CallPlaySound(audio::Sound* sound, const char* name, uint8_t flags, float fade)
{
    return sound->Play(name, flags, fade);
}

void CallCloseDialog(void* manager, char* name)
{
    static_cast<ui::GuiManager*>(manager)->CloseDialog(name);
}

void CallInvalidateDialog(void* manager, char* name, char sound)
{
    static_cast<ui::GuiManager*>(manager)->InvalidateDialog(name, sound);
}

void CallRememberPosition(void* manager, const int32_t* rect, int32_t key)
{
    static_cast<ui::GuiManager*>(manager)->RememberDialogPosition(rect, key);
}

struct GameEnv {
    uint8_t HitTest(Widget& w, int x, int y) { return w.HitTest(x, y); }
    uint8_t IsHovered(Widget& w) { return w.IsHovered(); }
    uint8_t OnMouseLeft(Widget& w, int x, int y, int s) { return w.MouseLeft(x, y, s); }
    uint8_t OnMouseRight(Widget& w, int x, int y, int s) { return w.MouseRight(x, y, s); }
    void SetOrigin(Widget& w, int dx, int dy) { w.SetOrigin(dx, dy); }
    void Translate(Widget& w, int dx, int dy) { w.Translate(dx, dy); }
    void Refresh(Widget& w) { w.Refresh(); }
    void SelectTab(Widget& w, int index) { w.SelectTab(index); }
    void SetVisible(Widget& w, uint32_t visible) { w.SetVisible(visible); }
    void Draw(Widget& w) { w.Draw(); }
    void DrawOverlay(Widget& w) { w.DrawOverlay(); }
    void Invalidate(Widget& w) { w.Invalidate(); }
    void Update(Widget& w, uint32_t dt) { w.Update(std::bit_cast<float>(dt)); }

    uint8_t BaseHitTest(Widget& w, int x, int y) { return w.HitTestBase(x, y); }

    void* AllocateNode() { return ::operator new(gui_containers::kNodeBytes); }
    void FreeNode(void* node) { ::operator delete(node); }
    void Free(void* block) { CallFree(block); }
    void* CopyString(const char* text)
    {
        const size_t size = std::strlen(text) + 1;
        void* copy = ::operator new(size);
        std::memcpy(copy, text, size);
        return copy;
    }
    // GuiTab / GuiTabWindow.
    uint8_t OnMouseWheel(Widget& w, int x, int y, int delta) { return w.MouseWheel(x, y, delta); }
    void RefreshSelection(Widget& w) { w.RefreshSelection(); }
    void BaseSetOrigin(Widget& w, int dx, int dy) { w.SetOriginBase(dx, dy); }
    void BaseTranslate(Widget& w, int dx, int dy) { w.TranslateBase(dx, dy); }
    void BaseSetVisible(Widget& w, uint32_t visible) { w.SetVisibleBase(visible); }
    void BaseInvalidate(Widget& w) { w.InvalidateBase(); }
    void TrackTooltip(Widget& w, int x, int y, int state) { w.TrackTooltip(x, y, state); }
    uint8_t TabMouse(ui::GuiTabData& tab, int x, int y, int state, bool active) { return container::TabMouse(&tab, x, y, state, active); }
    ui::SpriteInstance* CreateSprite(const char* name)
    {
        return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(name);
    }
    void ReleaseSprite(ui::SpriteInstance* sprite) { (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->ReleaseInstance(sprite); }
    void* NewTabNode() { return NewGuiRecord<ui::GuiTabNode>(); }
    void InvalidateAllDialogs() { CallInvalidateDialogs(game::GetGuiManager(), 0); }
    void PlayClick() { CallPlaySound(audio::CurrentSound(), "ButtonClick", 1, 0.0f); }

    // GuiDialog. The GuiManager calls go through the observing forwarders.
    int ScreenWidth() { return game::GetRenderer()->m_width; }
    int ScreenHeight() { return game::GetRenderer()->m_height; }
    Widget* NewCaption(int x0, int y0, int x1, int y1)
    {
        return static_cast<Widget*>(container::ConstructMainItem(NewGuiRecord<Item>(), x0, y0, x1, y1, 0));
    }
    void DeleteWidget(Widget& w) { gui::DestroyWidget(w); }
    void BaseBounds(Widget& w, int32_t* out) { w.GetBounds(out); }
    gui_dialog::Size SpriteSize(ui::SpriteInstance* sprite)
    {
        const ui::SpriteAnim* anim = sprite->m_sprite->m_anims[sprite->m_animIndex];
        return {anim->m_width, anim->m_height};
    }
    uint8_t SpriteHit(ui::SpriteInstance* sprite, int x, int y) { return (sprite)->HitTest(x, y); }
    void InvalidateDialog(char* name) { CallInvalidateDialog(game::GetGuiManager(), name, 0); }
    void CloseDialog(char* name) { CallCloseDialog(game::GetGuiManager(), name); }
    void RememberPosition(const int32_t* rect, int32_t key) { CallRememberPosition(game::GetGuiManager(), rect, key); }
};

// The renderer's colour packing and line drawing and the sprite names kept in game globals, for the backgrounds.
class GamePrimitives final : public gui_containers::Primitives {
public:
    uint32_t Pack(int r, int g, int b) override { return obl::ui::renderer::PackColor(game::GetRenderer(), r, g, b); }
    void Line(int x0, int y0, int x1, int y1, uint32_t color) override
    {
        ui::pixel_primitives::DrawLine(game::GetRenderer(), x0, y0, x1, y1, color);
    }
    const char* ScrollBarSliderSprite() override { return gui_data::g_GuiScrollBarSliderSprite; }
    const char* ScrollBarTrackSprite() override { return gui_data::g_GuiScrollBarTrackSprite; }
    const char* IndexedSprite(int index) override { return game::GuiMainItemSpriteNames()[index]; }
};

} // namespace

namespace container {
Define* ConstructDefine(Define* define, const char* name)
{
    g_counter.Count();
    return new (static_cast<void*>(define)) Define(name);
}
} // namespace container

namespace {

// ---- GuiMainItem ----------------------------------------------------------------------------------------------------------------

void ItemDraw(Item* item)
{
    if (item->m_dirty && gui_containers::HasBackground(item->m_variant)) g_variantDrawn[item->m_variant].Count();
    LiveCanvas canvas;
    GamePrimitives primitives;
    GameEnv env;
    gui_containers::MainItemDraw(*item, canvas, primitives, env);
}

} // namespace

namespace container {
Item* ConstructMainItem(Item* item, int x0, int y0, int x1, int y1, int variant)
{
    g_counter.Count();
    return new (static_cast<void*>(item)) Item(x0, y0, x1, y1, variant);
}

void MainItemSetHeight(ui::GuiMainItemData* item, int height)
{
    g_counter.Count();
    gui_containers::MainItemSetHeight(*item, height);
}
} // namespace container

namespace {

// ---- GuiTab / GuiTabWindow ----------------------------------------------------------------------------------------------------

void RunTabDraw(Tab* tab)
{
    if (tab->m_state != tab->m_drawnState) g_tabDrawn.Count();
    LiveCanvas canvas;
    const gui_canvas::SpriteId sprite = canvas.BorrowSprite(tab->m_sprite);
    GameEnv env;
    gui_tabs::TabDraw(*tab, canvas, sprite, env);
}
} // namespace

namespace container {
Tab* ConstructTab(Tab* tab, int x0, int y0, int x1, int y1, const char* sprite)
{
    g_counter.Count();
    return new (static_cast<void*>(tab)) Tab(x0, y0, x1, y1, sprite);
}

uint8_t TabMouse(ui::GuiTabData* tab, int x, int y, int state, bool active)
{
    g_counter.Count();
    GameEnv env;
    return gui_tabs::TabMouse(*tab, x, y, state, active, env);
}

Window* ConstructTabWindow(Window* window, int x0, int y0, int x1, int y1)
{
    g_counter.Count();
    return new (static_cast<void*>(window)) Window(x0, y0, x1, y1);
}
} // namespace container

namespace {

// ---- GuiDialog -------------------------------------------------------------------------------------------------------------------

void RunDialogDraw(Dialog* dialog)
{
    if (dialog->m_dirty) g_dialogDrawn.Count();
    LiveCanvas canvas;
    const gui_canvas::SpriteId sprite = dialog->m_sprite ? canvas.BorrowSprite(dialog->m_sprite) : gui_canvas::kNoSprite;
    GameEnv env;
    gui_dialog::DialogDraw(*dialog, canvas, sprite, env);
}
void RunDialogUpdate(Dialog* dialog, uint32_t dt)
{
    if (dialog->m_animating) g_dialogSlid.Count();
    GameEnv env;
    gui_dialog::DialogUpdate(*dialog, dt, env);
}

} // namespace

namespace container {
Dialog* ConstructDialog(Dialog* dialog, const char* name, int x0, int y0, int x1, int y1, const char* sprite, int32_t key, bool movable)
{
    g_counter.Count();
    return new (static_cast<void*>(dialog)) Dialog(name, x0, y0, x1, y1, sprite, key, movable ? 1 : 0);
}
} // namespace container

namespace container {
void DialogSelectTab(DialogData* dialog, int index)
{
    g_counter.Count();
    GameEnv env;
    gui_dialog::DialogSelectTab(*dialog, index, env);
}

// The speed is only copied into two fields, so it travels as bits.
void DialogStartAnimation(DialogData* dialog, int type, uint32_t speedBits, int delay)
{
    g_counter.Count();
    GameEnv env;
    gui_dialog::DialogStartAnimation(*dialog, type, speedBits, delay, env);
}
} // namespace container

} // namespace obl::gui

namespace obl::ui {
using namespace gui;

GuiDefine::GuiDefine(const char* name) : GuiLeaf(0, 0, 0, 0)
{
    GameEnv env;
    gui_containers::DefineConstruct(*this, name, env);
}

GuiDefine::~GuiDefine()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::DefineDestruct(*this, env);
}

void GuiDefine::SetOrigin(int dx, int dy)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::DefineSetOrigin(*this, dx, dy, env);
}

void GuiDefine::Translate(int dx, int dy)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::DefineTranslate(*this, dx, dy, env);
}

void GuiDefine::Refresh()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::DefineRefresh(*this, env);
}

void GuiDefine::SelectTab(int index)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::DefineSelectTab(*this, index, env);
}

void GuiDefine::SetVisible(uint32_t visible)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::DefineSetVisible(*this, visible, env);
}

void GuiDefine::AddChild(GuiWidget* child)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::AddChild(m_children, child, env);
}

void GuiDefine::Draw()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::DefineDraw(*this, env);
}

void GuiDefine::DrawOverlay()
{
    GuiContainer::ChildrenDrawOverlay();
}

bool GuiDefine::MouseLeft(int x, int y, int state)
{
    return GuiContainer::ChildrenMouseLeft(x, y, state);
}

void GuiDefine::Invalidate()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::DefineInvalidate(*this, env);
}

// The frame time keeps its four bytes on the way to the children.
void GuiDefine::Update(float dt)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::DefineUpdate(*this, std::bit_cast<uint32_t>(dt), env);
}

GuiMainItem::GuiMainItem(int x0, int y0, int x1, int y1, int variant) : GuiLeaf(x0, y0, x1, y1)
{
    GameEnv env;
    gui_containers::MainItemConstruct(*this, variant, env);
}

GuiMainItem::~GuiMainItem()
{
    gui::g_counter.Count();
    GameEnv env;
    if (m_children) gui_containers::MainItemDestruct(*this, env); // a default-constructed record owns nothing
}

bool GuiMainItem::HitTest(int x, int y)
{
    gui::g_counter.Count();
    GameEnv env;
    return gui_containers::MainItemHitTest(*this, x, y, env) != 0;
}

bool GuiMainItem::MouseLeft(int x, int y, int state)
{
    gui::g_counter.Count();
    GameEnv env;
    return gui_containers::MainItemOnMouseLeft(*this, x, y, state, env) != 0;
}

bool GuiMainItem::MouseRight(int x, int y, int state)
{
    gui::g_counter.Count();
    GameEnv env;
    return gui_containers::MainItemOnMouseRight(*this, x, y, state, env) != 0;
}

// The frame time keeps its four bytes on the way to the children.
void GuiMainItem::Update(float dt)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::MainItemUpdate(*this, std::bit_cast<uint32_t>(dt), env);
}

void GuiMainItem::SetVisible(uint32_t visible)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::MainItemSetVisible(*this, visible, env);
}

void GuiMainItem::Invalidate()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::MainItemInvalidate(*this, env);
}

void GuiMainItem::Draw()
{
    gui::g_counter.Count();
    ItemDraw(this);
}

void GuiMainItem::DrawOverlay()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::MainItemDrawOverlay(*this, env);
}

void GuiMainItem::AddChild(GuiWidget* child)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::AddChild(m_children, child, env);
}

GuiTab::GuiTab(int x0, int y0, int x1, int y1, const char* sprite) : GuiLeaf(x0, y0, x1, y1)
{
    GameEnv env;
    gui_tabs::TabConstruct(*this, sprite, env);
}

GuiTab::~GuiTab()
{
    gui::g_counter.Count();
    GameEnv env;
    if (m_children) gui_tabs::TabDestruct(*this, env); // a default-constructed record owns nothing
}

void GuiTab::SetOrigin(int dx, int dy)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabSetOrigin(*this, dx, dy, env);
}

bool GuiTab::MouseWheel(int x, int y, int delta)
{
    gui::g_counter.Count();
    GameEnv env;
    return gui_tabs::TabOnMouseWheel(*this, x, y, delta, env) != 0;
}

void GuiTab::SetVisible(uint32_t visible)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabSetVisible(*this, visible, env);
}

// The frame time keeps its four bytes on the way to the children.
void GuiTab::Update(float dt)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabUpdate(*this, std::bit_cast<uint32_t>(dt), env);
}

void GuiTab::Refresh()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabRefresh(*this, env);
}

void GuiTab::Translate(int dx, int dy)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabTranslate(*this, dx, dy, env);
}

void GuiTab::Invalidate()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabInvalidate(*this, env);
}

void GuiTab::Draw()
{
    gui::g_counter.Count();
    RunTabDraw(this);
}

void GuiTab::DrawOverlay()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabDrawOverlay(*this, env);
}

void GuiTab::AddChild(GuiWidget* child)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_containers::AddChild(m_children, child, env);
}

GuiTabWindow::GuiTabWindow(int x0, int y0, int x1, int y1) : GuiLeaf(x0, y0, x1, y1)
{
    gui_tabs::TabWindowConstruct(*this);
}

GuiTabWindow::~GuiTabWindow()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowDestruct(*this, env);
}

void GuiTabWindow::Translate(int dx, int dy)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowTranslate(*this, dx, dy, env);
}

void GuiTabWindow::SetOrigin(int dx, int dy)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowSetOrigin(*this, dx, dy, env);
}

bool GuiTabWindow::MouseWheel(int x, int y, int delta)
{
    gui::g_counter.Count();
    GameEnv env;
    return gui_tabs::TabWindowOnMouseWheel(*this, x, y, delta, env) != 0;
}

bool GuiTabWindow::MouseLeft(int x, int y, int state)
{
    gui::g_counter.Count();
    GameEnv env;
    return gui_tabs::TabWindowOnMouseLeft(*this, x, y, state, env) != 0;
}

// The frame time keeps its four bytes on the way to the children.
void GuiTabWindow::Update(float dt)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowUpdate(*this, std::bit_cast<uint32_t>(dt), env);
}

void GuiTabWindow::Refresh()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowRefresh(*this, env);
}

void GuiTabWindow::SetVisible(uint32_t visible)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowSetVisible(*this, visible, env);
}

void GuiTabWindow::SelectTab(int index)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowSelectTab(*this, index, env);
}

void GuiTabWindow::Draw()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowDraw(*this, env);
}

void GuiTabWindow::Invalidate()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowInvalidate(*this, env);
}

void GuiTabWindow::DrawOverlay()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowDrawOverlay(*this, env);
}

void GuiTabWindow::AddChild(GuiWidget* child)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_tabs::TabWindowAddChild(*this, child, env);
}

GuiDialog::GuiDialog(const char* name, int x0, int y0, int x1, int y1, const char* sprite, int32_t positionKey, uint8_t movable)
    : GuiLeaf(x0, y0, x1, y1)
{
    GameEnv env;
    gui_dialog::DialogConstruct(*this, name, sprite, positionKey, movable, env);
}

GuiDialog::~GuiDialog()
{
    gui::g_counter.Count();
    GameEnv env;
    if (m_children) gui_dialog::DialogDestruct(*this, env); // a default-constructed record owns nothing
}

void GuiDialog::SetVisible(uint32_t visible)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_dialog::DialogSetVisible(*this, visible, env);
}

bool GuiDialog::MouseWheel(int x, int y, int delta)
{
    gui::g_counter.Count();
    GameEnv env;
    return gui_dialog::DialogOnMouseWheel(*this, x, y, delta, env) != 0;
}

// The frame time keeps its four bytes on the way to the children.
void GuiDialog::Update(float dt)
{
    gui::g_counter.Count();
    RunDialogUpdate(this, std::bit_cast<uint32_t>(dt));
}

void GuiDialog::SetOrigin(int, int)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_dialog::DialogSetOrigin(*this, env);
}

void GuiDialog::Refresh()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_dialog::DialogRefresh(*this, env);
}

void GuiDialog::Translate(int dx, int dy)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_dialog::DialogTranslate(*this, dx, dy, env);
}

void GuiDialog::SelectTab(int index)
{
    container::DialogSelectTab(this, index);
}

void GuiDialog::Draw()
{
    gui::g_counter.Count();
    RunDialogDraw(this);
}

void GuiDialog::DrawOverlay()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_dialog::DialogDrawOverlay(*this, env);
}

void GuiDialog::Invalidate()
{
    gui::g_counter.Count();
    GameEnv env;
    gui_dialog::DialogInvalidate(*this, env);
}

void GuiDialog::AddChild(GuiWidget* child)
{
    gui::g_counter.Count();
    GameEnv env;
    gui_dialog::DialogAddChild(*this, child, env);
}

bool GuiDialog::HitTest(int x, int y)
{
    gui::g_counter.Count();
    GameEnv env;
    return gui_dialog::DialogHitTest(*this, x, y, env) != 0;
}

bool GuiDialog::MouseLeft(int x, int y, int state)
{
    gui::g_counter.Count();
    GameEnv env;
    return gui_dialog::DialogOnMouseLeft(*this, x, y, state, env) != 0;
}
} // namespace obl::ui
