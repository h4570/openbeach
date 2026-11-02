#pragma once

#include "game/ui/gui_types.h"

#include <cstddef>
#include <cstdint>

namespace obl::ui {

// A button action from the GUI script's action table.
using GuiAction = void (*)(int);
struct GuiActionEntry {
    const char* m_name;
    GuiAction m_function;
};

struct GuiButtonData : GuiWidget {
    using GuiWidget::GuiWidget;
    int32_t m_state = 0, m_drawnState = 0, m_stateVariable = 0;
    uint8_t m_repeat = 0;
    char* m_text = nullptr;
    float m_clickTime = 0.0f;
    GuiAction m_leftAction = nullptr, m_rightAction = nullptr;
    uint8_t m_blink = 0, m_blinkFrame = 0;
    float m_blinkTime = 0.0f, m_repeatDelay = 0.0f;
    char* m_graphic = nullptr;
    SpriteInstance* m_instance = nullptr;
    int32_t m_textSource = 0;
    uint8_t m_flashing = 0, m_rightClick = 0;
};

// gui_controls.cpp
struct GuiButton final : GuiLeaf<GuiButtonData> {
    using GuiLeaf::GuiLeaf;
    GuiButton() = default;
    GuiButton(int x0, int y0, int x1, int y1, const char* graphic, const char* text, const char* left, const char* right);
    ~GuiButton() override;
    void Update(float dt) override;
    void Draw() override;
    bool HitTest(int x, int y) override;
    bool MouseLeft(int x, int y, int state) override;
    bool MouseRight(int x, int y, int state) override;
    void SetVisible(uint32_t visible) override;
    const char* ClassName() const override { return "GuiButton"; }
};

struct GuiCheckBoxData : GuiWidget {
    using GuiWidget::GuiWidget;
    char* m_variable = nullptr;
    SpriteInstance* m_instance = nullptr;
    int32_t m_value = 0;
};

struct GuiCheckBox final : GuiLeaf<GuiCheckBoxData> {
    using GuiLeaf::GuiLeaf;
    GuiCheckBox() = default;
    GuiCheckBox(int x0, int y0, int x1, int y1, const char* graphic, const char* key);
    ~GuiCheckBox() override;
    void Update(float dt) override;
    void Draw() override;
    bool HitTest(int x, int y) override;
    bool MouseLeft(int x, int y, int state) override;
    void SetVisible(uint32_t visible) override { VisibleInvalidate(visible); }
    const char* ClassName() const override { return "GuiCheckBox"; }
};

struct GuiRadioButtonData : GuiWidget {
    using GuiWidget::GuiWidget;
    uint32_t m_flags = 0;
    int32_t m_value = 0;
    SpriteInstance* m_instance = nullptr;
    char* m_text = nullptr;
    int32_t m_font = 0;
};

struct GuiRadioButton final : GuiLeaf<GuiRadioButtonData> {
    using GuiLeaf::GuiLeaf;
    GuiRadioButton() = default;
    GuiRadioButton(int x0, int y0, int x1, int y1, const char* graphic, const char* text, int font, int value);
    ~GuiRadioButton() override;
    void Update(float dt) override;
    void Draw() override;
    void DrawOverlay() override;
    bool HitTest(int x, int y) override;
    bool MouseLeft(int x, int y, int state) override;
    const char* ClassName() const override { return "GuiRadioButton"; }
};

struct GuiRadioGroupData : GuiWidget {
    using GuiWidget::GuiWidget;
    char* m_variable = nullptr;
    GuiContainerChildNode* m_children = nullptr; // sentinel of the child list
    SpriteInstance* m_instance = nullptr;
};

struct GuiRadioGroup final : GuiLeaf<GuiRadioGroupData> {
    using GuiLeaf::GuiLeaf;
    GuiRadioGroup() = default;
    GuiRadioGroup(int x0, int y0, int x1, int y1, const char* key, const char* graphic);
    ~GuiRadioGroup() override;
    void Update(float dt) override;
    void Draw() override;
    void DrawOverlay() override;
    void SetVisible(uint32_t visible) override { VisibleInvalidate(visible); }
    bool MouseLeft(int x, int y, int state) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    void Invalidate() override;
    void AddChild(GuiWidget* child) override;
    const char* ClassName() const override { return "GuiRadioGroup"; }
};

struct GuiScrollBarData : GuiWidget {
    using GuiWidget::GuiWidget;
    uint32_t m_id = 0;
    uint32_t m_top = 0, m_count = 0, m_visibleRows = 0;
    uint8_t m_dragging = 0;
    GuiWidget* m_up = nullptr;
    GuiWidget* m_down = nullptr;
    GuiWidget* m_thumb = nullptr;
    GuiWidget* m_track = nullptr;
    float m_trackHeight = 0.0f; // widget height minus two arrow heights
    int32_t m_buttonHeight = 0; // arrow sprite height
    int32_t m_extra = 0; // shortfall when the thumb is raised to its 6 px minimum
    int32_t m_thumbHeight = 0;
};

struct GuiScrollBar final : GuiLeaf<GuiScrollBarData> {
    using GuiLeaf::GuiLeaf;
    GuiScrollBar() = default;
    GuiScrollBar(int x0, int y0, int x1, int y1, uint32_t top, uint32_t count, uint32_t rows, uint32_t id);
    ~GuiScrollBar() override;
    void Update(float dt) override;
    void DrawOverlay() override;
    void SetVisible(uint32_t visible) override;
    bool MouseWheel(int x, int y, int delta) override;
    bool MouseLeft(int x, int y, int state) override;
    void Translate(int dx, int dy) override;
    const char* ClassName() const override { return "GuiScrollBar"; }
};

struct GuiSliderData : GuiWidget {
    using GuiWidget::GuiWidget;
    char* m_variable = nullptr;
    uint8_t m_dragging = 0;
    float m_value = 0.0f; // 0..1, re-read from the variable every Update
    GuiWidget* m_track = nullptr; // GuiMainItem variant 6
    GuiWidget* m_knob = nullptr; // GuiImage
    uint32_t m_param0 = 0, m_param1 = 0; // constructor arguments the methods never read
    int32_t m_range = 0; // knob travel in pixels: width minus knob width
};

struct GuiSlider final : GuiLeaf<GuiSliderData> {
    using GuiLeaf::GuiLeaf;
    GuiSlider() = default;
    GuiSlider(int x0, int y0, int x1, int y1, const char* variable, uint32_t param0, uint32_t param1, int frame);
    ~GuiSlider() override;
    void Update(float dt) override;
    void DrawOverlay() override;
    bool MouseLeft(int x, int y, int state) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    const char* ClassName() const override { return "GuiSlider"; }
};

struct GuiNumberAdjustData : GuiWidget {
    using GuiWidget::GuiWidget;
    void* m_provider = nullptr; // null = the GuiManager's variable provider
    GuiWidget* m_inc = nullptr;
    GuiWidget* m_dec = nullptr;
    int32_t m_kind = 0; // 0 whole number, 1 whole.fraction
    int32_t m_font = 0; // GuiManager::m_textRenderers index
    GuiWidget* m_left = nullptr; // GuiImage of the whole part
    GuiWidget* m_right = nullptr; // GuiImage of the fraction part (kind 1)
    int32_t m_part = 0; // which part the arrows change
    float m_value = 0.0f;
    char* m_variable = nullptr;
    uint8_t m_wrap = 0;
};

struct GuiNumberAdjust final : GuiLeaf<GuiNumberAdjustData> {
    using GuiLeaf::GuiLeaf;
    GuiNumberAdjust() = default;
    GuiNumberAdjust(int x0, int y0, int x1, int y1, int kind, int font, const char* variable, uint8_t wrap, uint8_t tips);
    ~GuiNumberAdjust() override;
    void Update(float dt) override;
    void Draw() override;
    void DrawOverlay() override;
    void SetVisible(uint32_t visible) override;
    bool MouseLeft(int x, int y, int state) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    void Invalidate() override;
    void SetTooltip(const char* text) override;
    const char* ClassName() const override { return "GuiNumberAdjust"; }
};

static_assert(sizeof(GuiButton) == sizeof(GuiButtonData) && sizeof(GuiCheckBox) == sizeof(GuiCheckBoxData) &&
        sizeof(GuiRadioButton) == sizeof(GuiRadioButtonData) && sizeof(GuiScrollBar) == sizeof(GuiScrollBarData) &&
        sizeof(GuiSlider) == sizeof(GuiSliderData) && sizeof(GuiNumberAdjust) == sizeof(GuiNumberAdjustData) &&
        sizeof(GuiRadioGroup) == sizeof(GuiRadioGroupData));

} // namespace obl::ui
