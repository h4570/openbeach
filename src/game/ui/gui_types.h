#pragma once

#include "core/char16.h"
#include "sprite.h"

#include <cstdint>
#include <new>

namespace obl::ui {

struct GuiDialogData; // game/ui/gui_containers_types.h (the manager's dialog entries point at it)

// Widget types. Type 5 has no script command: it is the scroll bar that
// lists and tables create themselves.
enum class GuiType : int32_t {
    Button = 0,
    CheckBox,
    List,
    Define,
    Dialog,
    ScrollBar,
    Slider,
    Table,
    TabWindow,
    Tab,
    Image,
    MainItem,
    Text,
    Variable,
    VariableImage,
    TableColumn,
    Tga,
    ImageList,
    Bar,
    NumberAdjust,
    RadioGroup,
    RadioButton,
    ListBox,
    Count
};

// Common base of every `Gui<Class>` widget; the classes override the `Do*` virtuals. The defaults stay free of game calls so
// that the `Gui*Data` records link into the unit tests; GuiLeaf supplies the game behavior.
struct GuiWidget {
    GuiWidget() = default;
    GuiWidget(int x0, int y0, int x1, int y1)
        : m_x0(x0), m_y0(y0), m_x1(x1), m_y1(y1), m_baseX0(x0), m_baseY0(y0), m_baseX1(x1), m_baseY1(y1), m_dirty(1)
    {}
    virtual ~GuiWidget() = default; // GuiLeaf frees the tooltip

    virtual void Draw() {}
    virtual void DrawOverlay() {}
    virtual void SetVisible(uint32_t visible) { m_visible = static_cast<uint8_t>(visible); }
    virtual bool HitTest(int, int) { return false; }
    virtual bool MouseLeft(int, int, int) { return false; }
    virtual bool MouseRight(int, int, int) { return false; }
    virtual bool MouseWheel(int, int, int) { return false; }
    virtual void Update(float) {}
    virtual void SetOrigin(int, int) {}
    virtual void Translate(int, int) {}
    virtual void Invalidate() {}
    virtual void SetTooltip(const char*) {}
    virtual void RefreshSelection() {}
    virtual void AddChild(GuiWidget*) {}
    virtual void Refresh() {}
    virtual void SelectTab(int) {}
    virtual const char* ClassName() const { return "GuiWidget"; }

    int32_t m_x0 = 0, m_y0 = 0, m_x1 = 0, m_y1 = 0; // current rectangle
    int32_t m_baseX0 = 0, m_baseY0 = 0, m_baseX1 = 0, m_baseY1 = 0; // rectangle before SetOrigin
    float m_hoverTime = 0.0f; // tooltip timer
    int32_t m_type = 0; // widget type
    int32_t m_id = 0;
    uint8_t m_visible = 0; // SetVisible (the base also drops a tooltip it owns when hiding)
    int32_t m_tooltipX = 0, m_tooltipY = 0; // cursor position the tooltip timer last saw (TrackTooltip)
    char* m_tooltip = nullptr; // tooltip text on the game heap (SetTooltip allocates, the destructors free), null = none
    uint8_t m_hovered = 0; // set by a successful HitTest, never cleared by the base
    uint8_t m_held = 0; // button held over the widget (state 2)
    uint8_t m_clicked = 0; // click pending, the consumer clears it
    uint8_t m_dirty = 0; // redraw wanted: Draw does nothing while it is 0 (GuiVariableImage::Draw clears it)

    // Base behavior shared by every widget (gui_widgets.cpp over the field logic in gui_widgets.h); the derived widgets call these.
    void SetVisibleBase(uint32_t visible); // only the low byte is stored
    void SetOriginBase(int dx, int dy);
    void TranslateBase(int dx, int dy);
    void SetPosition(int x, int y);
    int32_t* GetBounds(int32_t* out); // writes x0, y0, x1, y1 and returns out
    bool HitTestBase(int x, int y);
    void InvalidateBase();
    bool TrackTooltip(int x, int y, int state);
    bool HideTooltipOnClick(int state);
    void SetTooltipBase(const char* text);
    void HideTooltip();
    bool ClickTooltip(int x, int y, int state); // HideTooltipOnClick with the mouse-handler signature (position unused)
    void VisibleInvalidate(uint32_t visible); // SetVisible, then Invalidate when shown
    bool IsVisible();
    bool IsHovered();
    bool IsHeld();
    bool WasClicked();
};

// The game behavior a leaf class inherits over its `Data` record. A template, so a unit test that only uses the record needs
// none of the game's widget code.
template<class Data> struct GuiLeaf : Data {
    using Data::Data;
    ~GuiLeaf() override
    {
        if (this->m_tooltip) ::operator delete(this->m_tooltip);
    }
    void SetVisible(uint32_t visible) override { GuiWidget::SetVisibleBase(visible); }
    bool HitTest(int x, int y) override { return GuiWidget::HitTestBase(x, y); }
    bool MouseLeft(int x, int y, int state) override { return GuiWidget::TrackTooltip(x, y, state); }
    bool MouseRight(int x, int y, int state) override { return GuiWidget::ClickTooltip(x, y, state); }
    void SetOrigin(int dx, int dy) override { GuiWidget::SetOriginBase(dx, dy); }
    void Translate(int dx, int dy) override { GuiWidget::TranslateBase(dx, dy); }
    void Invalidate() override { GuiWidget::InvalidateBase(); }
    void SetTooltip(const char* text) override { GuiWidget::SetTooltipBase(text); }
};

struct GuiTextData : GuiWidget {
    using GuiWidget::GuiWidget;
    const Char16* m_text = nullptr; // borrowed from the text table
    Char16* m_ownedText = nullptr; // optional fallback, freed by the destructor
    int32_t m_font = 0;
    uint32_t m_color = 0;
    uint32_t m_alignFlags = 0;
    uint8_t m_drawOnSurface = 0; // otherwise DrawOverlay draws every frame
};

// gui_widgets_leaves.cpp
struct GuiText final : GuiLeaf<GuiTextData> {
    using GuiLeaf::GuiLeaf;
    GuiText(int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, uint32_t flags);
    ~GuiText() override;
    void Draw() override;
    void DrawOverlay() override;
    void SetVisible(uint32_t visible) override { VisibleInvalidate(visible); }
    const char* ClassName() const override { return "GuiText"; }
};

// The presentation object behind GuiManager::m_tooltip; only the fields SetText / Show touch.
struct GuiTooltip {
    int32_t m_font; // GuiManager text renderer index
    uint8_t m_visible;
    const Char16* m_text; // borrowed from the text table
    int32_t m_textWidth;
    int32_t m_textHeight;
    int32_t m_boxWidth;
    int32_t m_boxHeight;
    int32_t m_x;
    int32_t m_y;
    GuiWidget* m_backing; // a zero-sized GuiMainItem; Show writes the box into its m_x0..m_y1

    // The live entries (gui_tooltip.cpp over gui_records.h): the text comes from the text table, Show places the box next
    // to the pointer, Draw paints it over the GUI.
    GuiTooltip* Construct(int font);
    void Destruct();
    void SetText(const char* key);
    void Show(int x, int y);
    void Hide();
    void Draw();
};

struct GuiTgaData : GuiWidget {
    using GuiWidget::GuiWidget;
    void* m_image = nullptr;
    char m_name[0x20] = {};
};

// gui_widgets_leaves.cpp; SetVisible also loads or releases the image.
struct GuiTga final : GuiLeaf<GuiTgaData> {
    using GuiLeaf::GuiLeaf;
    GuiTga(int x0, int y0, int x1, int y1, const char* name);
    ~GuiTga() override;
    void Draw() override;
    void SetVisible(uint32_t visible) override;
    const char* ClassName() const override { return "GuiTga"; }
};

struct GuiImageData : GuiWidget {
    using GuiWidget::GuiWidget;
    SpriteInstance* m_instance = nullptr;
    char m_name[0x20] = {};
};

// gui_widgets_image_bar.cpp
struct GuiImage final : GuiLeaf<GuiImageData> {
    using GuiLeaf::GuiLeaf;
    GuiImage(int x0, int y0, int x1, int y1, const char* name);
    ~GuiImage() override;
    void Draw() override;
    void SetVisible(uint32_t visible) override { VisibleInvalidate(visible); }
    bool HitTest(int x, int y) override;
    bool MouseLeft(int x, int y, int state) override;
    const char* ClassName() const override { return "GuiImage"; }
};

struct GuiBarData : GuiWidget {
    using GuiWidget::GuiWidget;
    float m_min = 0.0f, m_max = 0.0f;
    char m_variable[0x20] = {};
    int32_t m_width = 0, m_height = 0;
    int32_t m_colorMode = 0;
    float m_lastValue = 0.0f;
    void* m_provider = nullptr;
};

// gui_widgets_image_bar.cpp
struct GuiBar final : GuiLeaf<GuiBarData> {
    using GuiLeaf::GuiLeaf;
    GuiBar(int x0, int y0, int x1, int y1, float min, float max, const char* key, int mode);
    ~GuiBar() override;
    void Draw() override;
    void SetVisible(uint32_t visible) override { VisibleInvalidate(visible); }
    const char* ClassName() const override { return "GuiBar"; }
};

// Container children use an STL-style circular list; this is distinct from the owning wrapper below.
struct GuiContainerChildNode {
    GuiContainerChildNode* m_next;
    GuiContainerChildNode* m_prev;
    GuiWidget* m_widget;
};

// The named child list of `GuiDefine` and the two functions it answers its mouse and overlay calls with.
struct GuiContainer : GuiWidget {
    using GuiWidget::GuiWidget;
    char* m_name = nullptr; // the owner's name or variable; not read by the shared functions
    GuiContainerChildNode* m_children = nullptr; // sentinel (its next points to the first child)

    bool ChildrenMouseLeft(int x, int y, int state); // first child that handles it wins
    void ChildrenDrawOverlay();
};

// DestroyWidget reads `m_widget`, not the container list node's widget field. It does not clear it or free the wrapper.
struct GuiWidgetListNode {
    void DestroyWidget();
    void* m_next; // the owning list's link (GuiScriptNode / GuiManagerWidgetNode / GuiManagerDefinitionNode), not read here
    GuiWidget* m_widget;
};

// `Variable(rect, key, font, color, align)` of a GUI script: text of a game variable (type 0xd, built by GuiVariableConstruct). `DrawOverlay` maps `m_key` to the text and draws it with text renderer `m_font`.
struct GuiVariable final : GuiLeaf<GuiWidget> {
    GuiVariable() = default;
    GuiVariable(int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, uint32_t align);
    ~GuiVariable() override;
    void DrawOverlay() override;
    const char* ClassName() const override { return "GuiVariable"; }

    char m_key[0x20] = {}; // variable name copied from the script
    uint32_t m_color = 0;
    int32_t m_font = 0; // index into GuiManager::m_textRenderers
    uint32_t m_alignFlags = 0; // bit 0 centre, bit 1 left, bit 2 right, none = renderer state unchanged
};

// `Image(rect, key)` of a GUI script whose picture depends on a game variable (type 0xe, built by
// GuiVariableImageConstruct). `Draw` and `DrawOverlay` map `m_key` to a sprite and draw it.
struct GuiVariableImageData : GuiWidget {
    using GuiWidget::GuiWidget;
    char m_key[0x20] = {}; // variable name copied from the script
    void* m_instance = nullptr; // SpriteInstance of the latest draw (already released; written, never read back)
    void* m_object = nullptr; // object the picture reads (null: the variable provider of the open window)
};

struct GuiVariableImage final : GuiLeaf<GuiVariableImageData> {
    using GuiLeaf::GuiLeaf;
    GuiVariableImage(int x0, int y0, int x1, int y1, const char* key);
    ~GuiVariableImage() override;
    void Draw() override;
    void DrawOverlay() override;
    void SetVisible(uint32_t visible) override;
    const char* ClassName() const override { return "GuiVariableImage"; }
};

// The templates allocate a `Gui*Data` record for the class that is constructed over it.
static_assert(sizeof(GuiImage) == sizeof(GuiImageData) && sizeof(GuiText) == sizeof(GuiTextData) && sizeof(GuiBar) == sizeof(GuiBarData) &&
        sizeof(GuiTga) == sizeof(GuiTgaData) && sizeof(GuiVariableImage) == sizeof(GuiVariableImageData));

} // namespace obl::ui
