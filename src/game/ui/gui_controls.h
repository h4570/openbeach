#pragma once

#include "core/ascii_case.h"
#include "core/char16.h"
#include "core/float_to_int.h"
#include "game/ui/gui_canvas.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_controls_types.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace obl::gui_controls {

inline int ButtonDirection(const ui::GuiButtonData& w, int directions)
{
    const uint32_t state = w.m_blink ? (w.m_blinkFrame ? 2 : 0) : (w.m_state == 4 ? 1 : static_cast<uint32_t>(w.m_state));
    return static_cast<int>(state % static_cast<uint32_t>(directions));
}
inline int RadioFrame(uint32_t flags)
{
    if (flags & 0x10) return (flags & 4 ? 1 : 0) + 3;
    return flags & 4 ? 2 : ((flags & 8) >> 3);
}
inline void TextRect(const ui::GuiWidget& w, int font, uint32_t color, const Char16* text, gui_canvas::Canvas& canvas)
{
    canvas.SetTextAlign(font, 2);
    canvas.SetTextOutlined(font, false);
    canvas.SetTextColor(font, color);
    canvas.SetTextRect(font, w.m_x0, gui_widgets::Add(w.m_y0, 3), gui_widgets::Sub(w.m_x1, w.m_x0),
            gui_widgets::Sub(gui_widgets::Sub(w.m_y1, w.m_y0), 2));
    canvas.DrawWrapped(font, 0, 2, text);
}
inline void DrawCheckBox(ui::GuiCheckBoxData& w, gui_canvas::SpriteId sprite, int directions, gui_canvas::Canvas& canvas)
{
    if (!w.m_dirty) return;
    if (w.m_value == 0 || w.m_value == 1) canvas.SetDirection(sprite, w.m_value < directions ? w.m_value : directions - 1);
    canvas.DrawClippedAt(sprite, w.m_x0, w.m_y0, 0, 999999.0f);
    w.m_dirty = 0;
}
inline void DrawRadio(ui::GuiRadioButtonData& w, gui_canvas::SpriteId sprite, gui_canvas::Canvas& canvas)
{
    if (!w.m_dirty) return;
    canvas.SetFrame(sprite, RadioFrame(w.m_flags));
    canvas.DrawClippedAt(sprite, w.m_x0, w.m_y0, 0, 99999.0f);
    w.m_dirty = 0;
}

// ScrollBar thumb top: trunc(max(0, (track - extra) / count * top) + (arrow height + y0)). count is a signed divisor here.
inline int ScrollThumbY(const ui::GuiScrollBarData& w)
{
    double offset = (static_cast<double>(w.m_trackHeight) - static_cast<double>(w.m_extra)) /
            static_cast<double>(static_cast<int32_t>(w.m_count)) * static_cast<double>(static_cast<int32_t>(w.m_top));
    if (!(offset >= 0.0)) offset = 0.0;
    return FloatToIntWrap(static_cast<double>(gui_widgets::Add(w.m_buttonHeight, w.m_y0)) + offset);
}

inline bool ScrollSetCount(ui::GuiScrollBarData& w, uint32_t count)
{
    if (w.m_count == count) return false;
    w.m_count = count;
    w.m_extra = 0;
    // count is converted as an unsigned 64-bit integer here, the row count as a signed 32-bit one.
    w.m_thumbHeight = FloatToIntWrap(
            static_cast<double>(w.m_trackHeight) / static_cast<double>(count) * static_cast<double>(static_cast<int32_t>(w.m_visibleRows)));
    if (w.m_thumbHeight < 6) {
        w.m_extra = gui_widgets::Sub(6, w.m_thumbHeight);
        w.m_thumbHeight = 6;
    }
    const uint32_t maxTop = count - w.m_visibleRows;
    if (!(w.m_top < maxTop)) w.m_top = maxTop;
    return true;
}

// Top row that matches a dragged thumb: trunc((thumb.y0 - up.y1) / (track - thumbHeight) * (count - rows)).
inline uint32_t ScrollDragTop(const ui::GuiScrollBarData& w, int thumbY0, int upY1)
{
    const double travel = static_cast<double>(w.m_trackHeight) - static_cast<double>(w.m_thumbHeight);
    return static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(gui_widgets::Sub(thumbY0, upY1)) / travel *
            static_cast<double>(static_cast<int32_t>(w.m_count - w.m_visibleRows))));
}

// Drag delta limited to the track between the arrows.
inline int ScrollClampDelta(int delta, int thumbY0, int thumbY1, int upY1, int downY0)
{
    if (gui_widgets::Add(thumbY1, delta) > downY0) return gui_widgets::Sub(downY0, thumbY1);
    if (gui_widgets::Add(thumbY0, delta) < upY1) return gui_widgets::Sub(upY1, thumbY0);
    return delta;
}

// Slider: knob x from a value, value from the knob after a drag (x already clamped by the caller).
inline int SliderKnobX(const ui::GuiSliderData& w, float value)
{
    return gui_widgets::Add(FloatToIntWrap(static_cast<double>(w.m_range) * static_cast<double>(value)), w.m_x0);
}
// Returns the value to store (0 or 1 clamped). The unrounded quotient is compared with 0, the rounded float with 1.
inline float SliderValue(int knobX0, int sliderX0, int range)
{
    const double quotient = static_cast<double>(gui_widgets::Sub(knobX0, sliderX0)) / static_cast<double>(range);
    if (!(quotient >= 0.0)) return 0.0f;
    float value = static_cast<float>(quotient);
    if (value > 1.0f) value = 1.0f;
    return value;
}
inline int SliderClampX(int x, int left, int right)
{
    if (x < left) return left;
    const int limit = gui_widgets::Add(right, -7);
    return x > limit ? limit : x;
}
// A slider writes its game variable in whole hundredths.
inline float SliderQuantize(float value)
{
    return static_cast<float>(static_cast<double>(FloatToIntWrap(static_cast<double>(value) * 100.0)) * static_cast<double>(0.01f));
}

// NumberAdjust arrow press: dir +1 / -1. `part` 0 steps by 1, 1 by 0.1 (as float), anything else by nothing. Past the upper limit
// (999.99) an increase wraps to |integer + fraction| of the old value; below 0 a decrease wraps to 999 (wrap flag) or 0 plus the fraction.
inline float AdjustStep(float value, int part, bool wrap, int dir)
{
    double whole;
    const double fraction = std::modf(static_cast<double>(value), &whole);
    const float wholeF = static_cast<float>(whole), fractionF = static_cast<float>(fraction);
    const double step = part == 0 ? 1.0 : part == 1 ? static_cast<double>(0.1f) : 0.0;
    if (dir > 0) {
        const double next = static_cast<double>(value) + (part == 0 || part == 1 ? step : 0.0);
        if (next > static_cast<double>(999.99f))
            return static_cast<float>(std::fabs(static_cast<double>(wholeF) + static_cast<double>(fractionF)));
        return static_cast<float>(next);
    }
    const double next = static_cast<double>(value) - (part == 0 || part == 1 ? step : 0.0);
    if (!(next >= 0.0)) return static_cast<float>(std::fabs((wrap ? 999.0 : 0.0) + static_cast<double>(fractionF)));
    return static_cast<float>(next);
}

// Text prefix the button constructor looks for: `BUTVAR_` reads the text from a game variable, `BUTSTATENTVAR_`/`BUTSTATEVAR_` bind the state to a
// `<name>_STATE` variable (2 = without a text draw). `prefix` is skipped when the text is copied.
struct ButtonText {
    size_t prefix = 0;
    int32_t textSource = 0;
    int32_t stateVariable = 0;
};
inline ButtonText ParseButtonText(const char* text)
{
    ButtonText r;
    if (std::strncmp(text, "BUTVAR_", 7) == 0) {
        r.prefix = 7;
        r.textSource = 1;
    } else if (std::strncmp(text, "BUTSTATENTVAR_", 14) == 0) {
        r.prefix = 14;
        r.stateVariable = 2;
    } else if (std::strncmp(text, "BUTSTATEVAR_", 12) == 0) {
        r.prefix = 12;
        r.stateVariable = 1;
    }
    return r;
}

// GuiButton mouse handler after the tooltip tracking and hover update. States: 0 idle, 1 pressed (action done), 2 hovered, 3 disabled,
// 4 clicked (action pending in Update). Mouse state 0 move, 1 press, 2 held, 3 leave. Returns true when the click is consumed.
template<class Host> bool ButtonMouse(ui::GuiButtonData& w, int state, Host& host)
{
    switch (state) {
    case 0:
        if (w.m_state == 0) {
            if (host.Hovered()) w.m_state = 2;
        } else if (w.m_state == 2 && !host.Hovered())
            w.m_state = 0;
        break;
    case 1:
        if (host.Hovered() && w.m_state == 2) {
            w.m_state = 4;
            w.m_clickTime = host.Now();
            w.m_repeatDelay = 0.75f;
            w.m_rightClick = 0;
            return true;
        }
        break;
    case 2:
        if (w.m_state == 1 && host.Hovered()) {
            bool clicked = false;
            if (w.m_repeat) {
                if (static_cast<double>(host.Now()) - w.m_clickTime >= w.m_repeatDelay) {
                    host.Fire(false);
                    w.m_clickTime = host.Now();
                    w.m_clicked = 1;
                    w.m_repeatDelay = 0.05f;
                    clicked = true;
                }
            } else
                w.m_clickTime = host.Now();
            w.m_held = 1;
            return clicked;
        }
        break;
    case 3:
        if (w.m_state == 2) w.m_state = 0;
        break;
    }
    return false;
}

// GuiButton right-button handler after the tooltip click: a press on a hovered button queues the right action.
template<class Host> bool ButtonRightMouse(ui::GuiButtonData& w, int state, Host& host)
{
    if (state == 1 && host.Hovered() && w.m_state == 2) {
        w.m_state = 4;
        w.m_clickTime = host.Now();
        w.m_repeatDelay = 0.75f;
        w.m_rightClick = 1;
        return true;
    }
    return false;
}

// The part of the button's Update after the state-variable check, for an enabled button: blink timer, release of a pressed button, pending click.
template<class Host> void ButtonTick(ui::GuiButtonData& w, float dt, Host& host)
{
    if (w.m_blink) {
        const double elapsed = static_cast<double>(w.m_blinkTime) + dt;
        w.m_blinkTime = static_cast<float>(elapsed);
        if (elapsed > 0.75) {
            w.m_blinkTime = 0;
            w.m_blinkFrame = !w.m_blinkFrame;
            host.Invalidate();
        }
    }
    if (w.m_state == 1) {
        if (!w.m_flashing && static_cast<double>(host.Now()) - w.m_clickTime >= 0.05f && !host.Busy()) w.m_state = host.Hovered() ? 2 : 0;
    } else if (w.m_state == 4 && static_cast<double>(host.Now()) - w.m_clickTime >= 0.05f) {
        host.Click(w.m_rightClick != 0);
        if (w.m_state != 3) w.m_state = 1;
        w.m_clicked = 1;
        w.m_clickTime = host.Now();
    }
}

// Radio flags: 2 unselected, 4 selected, 8 hovered, 0x10 disabled. Each returns the new flags; a change means the caller invalidates.
inline uint32_t RadioHoverFlags(uint32_t flags, bool hovered)
{
    return hovered != bool(flags & 8) ? flags ^ 8 : flags;
}
inline uint32_t RadioSelectFlags(uint32_t flags, bool selected)
{
    const uint32_t flag = selected ? 4 : 2;
    return flags & flag ? flags : (flags & ~(flag == 4 ? 2u : 4u)) | flag;
}
inline uint32_t RadioDisabledFlags(uint32_t flags, bool disabled)
{
    return disabled != bool(flags & 0x10) ? flags ^ 0x10 : flags;
}

// ---- constructors over the game services -----------------------------------------------------------------------------------------------
template<class Env>
void ConstructButton(ui::GuiButtonData& w, const char* graphic, const char* text, const char* left, const char* right, Env& env)
{
    w.m_stateVariable = 0;
    w.m_text = nullptr;
    w.m_repeatDelay = 0.75f;
    w.m_graphic = nullptr;
    w.m_instance = nullptr;
    w.m_leftAction = env.FindAction(left);
    w.m_rightAction = env.FindAction(right);
    w.m_repeat = 0;
    w.m_drawnState = 3;
    if (!ascii::EqualsNoCase(text, "None")) {
        const ButtonText parsed = ParseButtonText(text);
        w.m_textSource = parsed.textSource;
        w.m_stateVariable = parsed.stateVariable;
        w.m_text = env.CopyString(text + parsed.prefix);
    }
    w.m_state = 0;
    if (!ascii::EqualsNoCase(graphic, "NONE")) {
        w.m_graphic = env.CopyString(graphic);
        ui::SpriteInstance* sprite = env.CreateSprite(w.m_graphic);
        w.m_x1 = gui_widgets::Add(w.m_x0, env.SpriteWidth(sprite));
        w.m_y1 = gui_widgets::Add(w.m_y0, env.SpriteHeight(sprite));
        env.ReleaseSprite(sprite);
        w.m_baseX1 = w.m_x1;
        w.m_baseY1 = w.m_y1;
    }
    w.m_blink = 0;
    w.m_blinkTime = 0;
    w.m_flashing = 0;
    w.m_rightClick = 0;
    w.m_type = 0;
    w.m_blinkFrame = 1;
}

template<class Env> void ConstructCheckBox(ui::GuiCheckBoxData& w, const char* graphic, const char* key, Env& env)
{
    w.m_variable = env.CopyString(key);
    w.m_instance = env.CreateSprite(graphic);
    w.m_type = 1;
}

template<class Env> void ConstructRadio(ui::GuiRadioButtonData& w, const char* graphic, const char* text, int font, int value, Env& env)
{
    w.m_text = nullptr;
    w.m_flags = 2;
    w.m_value = value;
    w.m_font = font;
    w.m_type = 21;
    w.m_instance = env.CreateSprite(graphic);
    env.SetAnimationType(w.m_instance, 3);
    if (!ascii::EqualsNoCase(text, "None")) w.m_text = env.CopyString(text);
}

template<class Env> void ConstructRadioGroup(ui::GuiRadioGroupData& w, const char* key, const char* graphic, Env& env)
{
    w.m_children = env.AllocateNode();
    w.m_children->m_next = w.m_children->m_prev = w.m_children;
    w.m_type = 20;
    w.m_variable = env.CopyString(key);
    if (!ascii::EqualsNoCase(graphic, "none")) w.m_instance = env.CreateSprite(graphic);
}

template<class Env> void ConstructScrollBar(ui::GuiScrollBarData& w, uint32_t top, uint32_t count, uint32_t rows, uint32_t id, Env& env)
{
    w.m_id = id;
    w.m_top = top;
    w.m_count = 0;
    w.m_visibleRows = rows;
    w.m_dragging = 0;
    w.m_thumb = nullptr;
    w.m_extra = 0;
    w.m_type = 5;

    ui::SpriteInstance* sprite = env.CreateUnobservedSprite("UI3_scrollbar_up");
    w.m_buttonHeight = env.SpriteHeight(sprite);
    const int arrowWidth = env.SpriteWidth(sprite);
    env.ReleaseSprite(sprite);
    w.m_trackHeight = static_cast<float>(static_cast<double>(gui_widgets::Sub(w.m_y1, w.m_y0)) -
            static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(w.m_buttonHeight) << 1)));
    ScrollSetCount(w, count); // no thumb yet: it is placed below
    const int left = w.m_x0, top0 = w.m_y0;
    const int right = gui_widgets::Add(arrowWidth, left);
    ui::GuiButtonData* up = env.NewButton(left, top0, right, gui_widgets::Add(w.m_buttonHeight, top0), "UI3_scrollbar_up");
    w.m_up = up;
    up->m_repeat = 1;
    const int bottom = w.m_y1;
    ui::GuiButtonData* down = env.NewButton(left, gui_widgets::Sub(bottom, w.m_buttonHeight), right, bottom, "UI3_scrollbar_down");
    w.m_down = down;
    down->m_repeat = 1;
    const int thumbLeft = gui_widgets::Add(w.m_x0, 1), thumbTop = w.m_y0;
    w.m_thumb = env.NewMainItem(thumbLeft, thumbTop, gui_widgets::Add(thumbLeft, 12), gui_widgets::Add(w.m_thumbHeight, thumbTop), 5);
    gui_widgets::SetPosition(*w.m_thumb, w.m_thumb->m_x0, ScrollThumbY(w));
    w.m_track = env.NewMainItem(w.m_x0, w.m_y0, w.m_x1, w.m_y1, 4);
    env.SetVisibleBase(w, 1);
    env.SetVisible(*w.m_up, 1u);
    env.SetVisible(*w.m_down, 1u);
}

template<class Env> void ConstructSlider(ui::GuiSliderData& w, const char* variable, uint32_t param0, uint32_t param1, int frame, Env& env)
{
    const int left = w.m_x0, top = w.m_y0, right = w.m_x1;
    w.m_param0 = param0;
    w.m_param1 = param1;
    const int trackTop = gui_widgets::Add(top, 5);
    w.m_track = env.NewMainItem(left, trackTop, right, gui_widgets::Add(trackTop, 3), 6);
    // Width / 2 truncates toward zero.
    const int knobLeft = gui_widgets::Sub(gui_widgets::Add(w.m_x0, gui_widgets::Sub(w.m_x1, w.m_x0) / 2), 5);
    w.m_knob = env.NewImage(knobLeft, w.m_y0, gui_widgets::Add(knobLeft, 7), gui_widgets::Add(w.m_y0, 0xd), "UI3_slider_button");
    env.SetFrame(w.m_knob, frame);
    w.m_variable = env.CopyString(variable);
    w.m_range = gui_widgets::Sub(w.m_x1, w.m_x0);
    w.m_dragging = 0;
    w.m_type = 6;
    const ui::GuiWidget& knob = *w.m_knob;
    w.m_range = gui_widgets::Add(w.m_range, gui_widgets::Sub(knob.m_x0, knob.m_x1));
}

template<class Env>
void ConstructNumberAdjust(ui::GuiNumberAdjustData& w, int kind, int font, const char* variable, uint8_t wrap, uint8_t tips, Env& env)
{
    w.m_kind = kind;
    w.m_font = font;
    w.m_wrap = wrap;
    const int left = w.m_x0, top = w.m_y0, right = w.m_x1, bottom = w.m_y1;
    w.m_provider = nullptr;
    w.m_left = nullptr;
    w.m_right = nullptr;
    w.m_value = 0;
    w.m_type = 0x13;
    w.m_left = env.NewImage(left, top, right, bottom, "UI3_field_n_left");
    env.SetFrame(w.m_left, 0);
    int x = gui_widgets::Add(left, 0x21);
    if (w.m_kind == 1) {
        w.m_right = env.NewImage(x, top, gui_widgets::Add(left, 0x32), bottom, "UI3_field_n_right");
        env.SetFrame(w.m_right, 0);
        x = gui_widgets::Add(left, 0x39);
    }
    ui::GuiButtonData* dec = env.NewButton(x, top, gui_widgets::Add(x, 8), bottom, "UI3_dec_flat");
    w.m_dec = dec;
    dec->m_repeat = 1;
    if (tips) env.SetTooltip(*w.m_dec, "NUMWIDGET_TIPTEXT_DEC");
    x = gui_widgets::Add(x, 10);
    ui::GuiButtonData* inc = env.NewButton(x, top, gui_widgets::Add(x, 8), bottom, "UI3_inc_flat");
    w.m_inc = inc;
    inc->m_repeat = 1;
    if (tips) env.SetTooltip(*w.m_inc, "NUMWIDGET_TIPTEXT_INC");
    w.m_part = 0;
    env.SetFrame(w.m_left, 1);
    if (w.m_right) env.SetFrame(w.m_right, 0);
    w.m_variable = env.CopyString(variable);
}

} // namespace obl::gui_controls

namespace obl::ui {
struct GuiButton;
struct GuiCheckBox;
struct GuiNumberAdjust;
struct GuiRadioButton;
struct GuiRadioGroup;
struct GuiScrollBar;
struct GuiSlider;
struct GuiWidget;
} // namespace obl::ui

namespace obl::gui {
bool RegisterGuiControls();

namespace slider_facade {

ui::GuiSlider* Construct(
        ui::GuiSlider* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* variable, float param0, float param1, int32_t frame);
} // namespace slider_facade

// GuiScrollBar: the list and table code calls Construct, SetItemCount and SetTopRow.
namespace scrollbar_facade {
ui::GuiScrollBar* Construct(
        ui::GuiScrollBar* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t top, uint32_t count, uint32_t rows, uint32_t id);
void SetItemCount(ui::GuiScrollBar* w, uint32_t count);
void SetTopRow(ui::GuiScrollBar* w, uint32_t top);
} // namespace scrollbar_facade

// GuiNumberAdjust: the table code and the script leaves call Construct.
namespace number_adjust_facade {
ui::GuiNumberAdjust* Construct(ui::GuiNumberAdjust* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t kind, int32_t font,
        const char* variable, uint8_t wrap, uint8_t tips);
} // namespace number_adjust_facade

// GuiCheckBox: the script leaves call Construct.
namespace check_box_facade {
ui::GuiCheckBox* Construct(ui::GuiCheckBox* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* graphic, const char* key);
} // namespace check_box_facade

// GuiRadioButton: the script leaves call Construct.
namespace radio_button_facade {
ui::GuiRadioButton* Construct(ui::GuiRadioButton* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* graphic, const char* text,
        int32_t font, int32_t value);
} // namespace radio_button_facade

// GuiRadioGroup: the script leaves call Construct.
namespace radio_group_facade {
ui::GuiRadioGroup* Construct(ui::GuiRadioGroup* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* key, const char* graphic);
} // namespace radio_group_facade

// GuiButton actions: the manager, action and dialog code set a button by its action name (the widget walk stays in their modules).
namespace button_action_facade {
void DisableIfAction(ui::GuiButton* w, const char* name);
void ResetStateByAction(ui::GuiButton* w, const char* name);
void SetBlinkIfAction(ui::GuiButton* w, const char* name, uint32_t value);
void SetFlashingByAction(ui::GuiButton* w, const char* name, uint32_t value);
void SetVisibleIfAction(ui::GuiButton* w, const char* name, uint32_t value);
void SetGraphicByAction(ui::GuiButton* w, const char* name, const char* graphic);
} // namespace button_action_facade

// GuiButton: the list, radar, table and script leaves code call Construct.
namespace button_facade {
ui::GuiButton* Construct(ui::GuiButton* w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* graphic, const char* text,
        const char* left, const char* right);
} // namespace button_facade
} // namespace obl::gui
