#pragma once

#include "game/ui/gui_types.h"

#include <cstddef>
#include <cstdint>

namespace obl::ui {

// `Define(name) {... }` of a GUI script (type 3, built by DefineConstruct): a template of widgets that dialogs copy
// from. Every method forwards to the children in list order, nothing is drawn by the template itself.
struct GuiDefineData : GuiContainer {
    using GuiContainer::GuiContainer;
};

// gui_containers.cpp
struct GuiDefine final : GuiLeaf<GuiDefineData> {
    using GuiLeaf::GuiLeaf;
    GuiDefine() = default;
    explicit GuiDefine(const char* name);
    ~GuiDefine() override;
    void Update(float dt) override;
    void Refresh() override;
    void Draw() override;
    void DrawOverlay() override;
    void AddChild(GuiWidget* child) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    void Invalidate() override;
    void SetVisible(uint32_t visible) override;
    void SelectTab(int index) override;
    bool MouseLeft(int x, int y, int state) override;
    const char* ClassName() const override { return "GuiDefine"; }
};
// The templates allocate a `Gui*Data` record for the class that is constructed over it.
static_assert(sizeof(GuiDefine) == sizeof(GuiDefineData));

// `MainItem` of a GUI script and the background panel of scroll bars, sliders and lists (type 0xb, built by MainItemConstruct). `m_variant` picks one of seven procedural backgrounds that `Draw` paints once per invalidation (0 = none).
struct GuiMainItemData : GuiWidget {
    using GuiWidget::GuiWidget;
    int32_t m_variant = 0; // constructor argument 5: 0 none, 1..7 one of the gui_containers::backgrounds helpers
    int32_t m_index = 0; // index into g_GuiMainItemSpriteNames for variant 1 (the constructor writes 0)
    GuiContainerChildNode* m_children = nullptr; // sentinel of the child list
};

struct GuiMainItem final : GuiLeaf<GuiMainItemData> {
    using GuiLeaf::GuiLeaf;
    GuiMainItem() = default;
    GuiMainItem(int x0, int y0, int x1, int y1, int variant);
    ~GuiMainItem() override;
    void Update(float dt) override;
    void Draw() override;
    void DrawOverlay() override;
    void SetVisible(uint32_t visible) override;
    bool HitTest(int x, int y) override;
    bool MouseLeft(int x, int y, int state) override;
    bool MouseRight(int x, int y, int state) override;
    void Invalidate() override;
    void AddChild(GuiWidget* child) override;
    const char* ClassName() const override { return "GuiMainItem"; }
};
static_assert(sizeof(GuiMainItem) == sizeof(GuiMainItemData));

// `<TAB(name)(rect, sprite, tooltip)> {... }` of a TabWindow (type 9): the button of one tab plus the widgets
// of its page. The page is shown while `m_state` is 1; the button sprite shows direction `m_state`.
struct GuiTabData : GuiWidget {
    using GuiWidget::GuiWidget;
    int32_t m_state = 0; // 0 = page hidden (button up), 1 = shown; SetVisible writes it
    int32_t m_drawnState = 0; // state the sprite was last drawn for; Invalidate writes 3
    SpriteInstance* m_sprite = nullptr; // button sprite instance taken from the sprite bank, released by the destructor
    GuiContainerChildNode* m_children = nullptr; // sentinel of the page's child list
};

struct GuiTab final : GuiLeaf<GuiTabData> {
    using GuiLeaf::GuiLeaf;
    GuiTab() = default;
    GuiTab(int x0, int y0, int x1, int y1, const char* sprite);
    ~GuiTab() override;
    void Update(float dt) override;
    void Refresh() override;
    void Draw() override;
    void DrawOverlay() override;
    void SetVisible(uint32_t visible) override;
    bool MouseWheel(int x, int y, int delta) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    void Invalidate() override;
    void AddChild(GuiWidget* child) override;
    const char* ClassName() const override { return "GuiTab"; }
};
static_assert(sizeof(GuiTab) == sizeof(GuiTabData));

// Node of the singly linked tab list of a `GuiTabWindow` (an 8-byte heap block filled when a tab is added).
struct GuiTabNode {
    GuiTabNode* m_next;
    GuiWidget* m_widget;
};

// `TabWindow(rect, flags)` of a GUI script (type 8): a list of tabs of which exactly one is current.
struct GuiTabWindowData : GuiWidget {
    using GuiWidget::GuiWidget;
    GuiTabNode* m_tabs = nullptr; // first node, null = no tab yet
    GuiWidget* m_current = nullptr; // the shown tab (the first one added)
};

struct GuiTabWindow final : GuiLeaf<GuiTabWindowData> {
    using GuiLeaf::GuiLeaf;
    GuiTabWindow() = default;
    GuiTabWindow(int x0, int y0, int x1, int y1);
    ~GuiTabWindow() override;
    void Update(float dt) override;
    void Refresh() override;
    void Draw() override;
    void DrawOverlay() override;
    void SetVisible(uint32_t visible) override;
    bool MouseLeft(int x, int y, int state) override;
    bool MouseWheel(int x, int y, int delta) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    void Invalidate() override;
    void SelectTab(int index) override;
    void AddChild(GuiWidget* child) override;
    const char* ClassName() const override { return "GuiTabWindow"; }
};
static_assert(sizeof(GuiTabWindow) == sizeof(GuiTabWindowData));

// `Dialog(name)(rect, sprite,...) {... }` of a GUI script (type 4, created when the script parser reads a dialog item):
// a window with a background sprite, a draggable title bar and an optional slide animation.
struct GuiDialogData : GuiWidget {
    using GuiWidget::GuiWidget;
    GuiWidget* m_caption = nullptr; // title bar, a GuiMainItem over the top 30 rows; its hit test starts a drag
    char* m_name = nullptr; // copy of the script name on the game heap (GuiManager looks dialogs up by it)
    SpriteInstance* m_sprite = nullptr; // background sprite instance, null for "CLEAR" and "NONE"
    int32_t m_dragX = 0, m_dragY = 0; // cursor position of the previous drag step, 0 = no step yet
    uint8_t m_dragging = 0; // the caption was pressed and the button is still held
    uint8_t m_modal = 0; // written by GuiManager (modal dialogs can not be dragged)
    int32_t m_clear = 0; // 1 when the script's sprite is "CLEAR": nothing is painted behind the children
    uint8_t m_movable = 0; // constructor argument 9: the title bar may drag the window
    uint8_t m_animating = 0; // StartAnimation .. the animation's end
    int32_t m_animType = 0; // 0 none, 1 slide in, 2 slide out (then closes), 3 to a target x, 4 back to the base position
    float m_animTime = 0.0f; // time left
    float m_animDuration = 0.0f; // total time (StartAnimation's speed)
    int32_t m_animTarget = 0; // StartAnimation's delay argument: the x that type 3 slides towards
    int32_t m_minimized = 0; // 1 while the window is rolled out; zeroed by the constructor and by showing
    GuiContainerChildNode* m_children = nullptr; // sentinel of the child list
    int32_t m_positionKey = 0; // constructor argument 8, the key under which GuiManager remembers the window position
};

// gui_containers.cpp
struct GuiDialog final : GuiLeaf<GuiDialogData> {
    using GuiLeaf::GuiLeaf;
    GuiDialog() = default;
    GuiDialog(const char* name, int x0, int y0, int x1, int y1, const char* sprite, int32_t positionKey, uint8_t movable);
    ~GuiDialog() override;
    void Update(float dt) override;
    void Refresh() override;
    void Draw() override;
    void DrawOverlay() override;
    void SetVisible(uint32_t visible) override;
    bool HitTest(int x, int y) override;
    bool MouseLeft(int x, int y, int state) override;
    bool MouseWheel(int x, int y, int delta) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    void Invalidate() override;
    void SelectTab(int index) override;
    void AddChild(GuiWidget* child) override;
    const char* ClassName() const override { return "GuiDialog"; }
};
static_assert(sizeof(GuiDialog) == sizeof(GuiDialogData));

} // namespace obl::ui
