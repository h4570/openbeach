#include "core/char16.h"
#include "game/ui/gui_controls.h"
#include "unit_test.h"

#include <string>
#include <vector>

using namespace obl;

OBL_TEST_CASE("gui_controls.button_direction_and_blink_use_direction_count")
{
    ui::GuiButtonData w{};
    for (int state = 0; state < 5; ++state) {
        w.m_state = state;
        OBL_CHECK_EQ(gui_controls::ButtonDirection(w, 3), (state == 4 ? 1 : state) % 3);
    }
    w.m_blink = 0x80;
    w.m_blinkFrame = 0;
    OBL_CHECK_EQ(gui_controls::ButtonDirection(w, 3), 0);
    w.m_blinkFrame = 0x80;
    OBL_CHECK_EQ(gui_controls::ButtonDirection(w, 3), 2);
    OBL_CHECK_EQ(gui_controls::ButtonDirection(w, 1), 0);
}
OBL_TEST_CASE("gui_controls.checkbox_clamps_direction_preserves_other_values_and_dirty_gate")
{
    ui::GuiCheckBoxData w{};
    w.m_x0 = 7;
    w.m_y0 = 9;
    w.m_dirty = 1;
    w.m_value = 1;
    gui_canvas::RecordingCanvas c;
    auto sprite = c.SeedSprite("check", {1, 10, 10}, 0, 7, 3);
    gui_controls::DrawCheckBox(w, sprite, 1, c);
    OBL_REQUIRE(c.calls.size() == 1);
    OBL_CHECK_EQ(c.calls[0].frame, 7);
    OBL_CHECK_EQ(c.calls[0].direction, 0);
    OBL_CHECK_EQ(c.calls[0].time, 999999.0f);
    w.m_value = 8;
    w.m_dirty = 1;
    gui_controls::DrawCheckBox(w, sprite, 1, c);
    OBL_CHECK_EQ(c.calls[1].direction, 0);
    gui_controls::DrawCheckBox(w, sprite, 1, c);
    OBL_CHECK_EQ(c.calls.size(), 2u);
}
OBL_TEST_CASE("gui_controls.radio_flags_select_frame_and_overlay_uses_wrapping_rect")
{
    for (uint32_t flags = 0; flags < 32; ++flags) {
        const int expected = flags & 16 ? (flags & 4 ? 4 : 3) : (flags & 4 ? 2 : (flags & 8 ? 1 : 0));
        OBL_CHECK_EQ(gui_controls::RadioFrame(flags), expected);
    }
    ui::GuiRadioButtonData w{};
    w.m_flags = 0x1c;
    w.m_dirty = 1;
    gui_canvas::RecordingCanvas c;
    auto sprite = c.SeedSprite("radio", {5, 10, 10}, 3, 0, 2);
    gui_controls::DrawRadio(w, sprite, c);
    OBL_REQUIRE(c.calls.size() == 1);
    OBL_CHECK_EQ(c.calls[0].frame, 4);
    OBL_CHECK_EQ(c.calls[0].direction, 2);
    gui_controls::DrawRadio(w, sprite, c);
    OBL_CHECK_EQ(c.calls.size(), 1u);
    w.m_x0 = 10;
    w.m_x1 = 40;
    w.m_y0 = 20;
    w.m_y1 = 50;
    gui_controls::TextRect(w, 2, 0xffffffff, u"radio", c);
    OBL_REQUIRE(c.calls.size() == 2);
    OBL_CHECK_EQ(c.calls[1].state.y, 23);
    OBL_CHECK_EQ(c.calls[1].state.height, 28);
    OBL_CHECK_EQ(c.calls[1].state.align, 2);
    OBL_CHECK_EQ(c.calls[1].wrapped, true);
}
OBL_TEST_CASE("gui_controls.scroll_thumb_height_has_a_six_pixel_minimum_and_clamps_top")
{
    ui::GuiScrollBarData w{};
    w.m_trackHeight = 100.0f;
    w.m_visibleRows = 5;
    w.m_top = 40;
    OBL_CHECK(gui_controls::ScrollSetCount(w, 20));
    OBL_CHECK_EQ(w.m_thumbHeight, 25);
    OBL_CHECK_EQ(w.m_extra, 0);
    OBL_CHECK_EQ(w.m_top, 15u);
    OBL_CHECK(!gui_controls::ScrollSetCount(w, 20));
    gui_controls::ScrollSetCount(w, 100);
    OBL_CHECK_EQ(w.m_thumbHeight, 6);
    OBL_CHECK_EQ(w.m_extra, 1);
    OBL_CHECK_EQ(w.m_top, 15u);
    // count 0 divides by zero: the product is NaN or infinity and the float-to-int conversion returns 0, so the minimum applies.
    gui_controls::ScrollSetCount(w, 0);
    OBL_CHECK_EQ(w.m_thumbHeight, 6);
    OBL_CHECK_EQ(w.m_extra, 6);
    OBL_CHECK_EQ(w.m_top, 15u); // count - rows wraps to a huge unsigned limit
}
OBL_TEST_CASE("gui_controls.scroll_thumb_position_follows_top_and_never_goes_above_the_up_arrow")
{
    ui::GuiScrollBarData w{};
    w.m_y0 = 100;
    w.m_buttonHeight = 15;
    w.m_trackHeight = 90.0f;
    w.m_extra = 0;
    w.m_count = 30;
    w.m_top = 0;
    OBL_CHECK_EQ(gui_controls::ScrollThumbY(w), 115);
    w.m_top = 10;
    OBL_CHECK_EQ(gui_controls::ScrollThumbY(w), 145);
    w.m_extra = 6;
    w.m_top = 15;
    OBL_CHECK_EQ(gui_controls::ScrollThumbY(w), 157);
    w.m_count = 0;
    w.m_top = 0; // 0/0: NaN counts as "below zero" and is replaced by 0
    OBL_CHECK_EQ(gui_controls::ScrollThumbY(w), 115);
}
OBL_TEST_CASE("gui_controls.scroll_drag_is_limited_to_the_track_and_maps_back_to_a_row")
{
    OBL_CHECK_EQ(gui_controls::ScrollClampDelta(50, 120, 150, 115, 170), 20);
    OBL_CHECK_EQ(gui_controls::ScrollClampDelta(-50, 120, 150, 115, 170), -5);
    OBL_CHECK_EQ(gui_controls::ScrollClampDelta(7, 120, 150, 115, 170), 7);
    ui::GuiScrollBarData w{};
    w.m_trackHeight = 100.0f;
    w.m_thumbHeight = 20;
    w.m_count = 50;
    w.m_visibleRows = 10;
    OBL_CHECK_EQ(gui_controls::ScrollDragTop(w, 175, 115), 30u);
    OBL_CHECK_EQ(gui_controls::ScrollDragTop(w, 115, 115), 0u);
}
OBL_TEST_CASE("gui_controls.slider_clamps_x_and_quantizes_values_to_hundredths")
{
    OBL_CHECK_EQ(gui_controls::SliderClampX(50, 100, 200), 100);
    OBL_CHECK_EQ(gui_controls::SliderClampX(250, 100, 200), 193);
    OBL_CHECK_EQ(gui_controls::SliderClampX(150, 100, 200), 150);
    OBL_CHECK_EQ(gui_controls::SliderValue(100, 100, 93), 0.0f);
    OBL_CHECK_EQ(gui_controls::SliderValue(193, 100, 93), 1.0f);
    OBL_CHECK_EQ(gui_controls::SliderValue(300, 100, 93), 1.0f);
    OBL_CHECK_EQ(gui_controls::SliderValue(90, 100, 93), 0.0f);
    OBL_CHECK_EQ(gui_controls::SliderValue(100, 100, 0), 0.0f); // 0/0
    OBL_CHECK_EQ(gui_controls::SliderQuantize(0.5f), static_cast<float>(50.0 * static_cast<double>(0.01f)));
    OBL_CHECK_EQ(gui_controls::SliderQuantize(0.019f), static_cast<float>(1.0 * static_cast<double>(0.01f)));
    ui::GuiSliderData w{};
    w.m_x0 = 10;
    w.m_range = 93;
    OBL_CHECK_EQ(gui_controls::SliderKnobX(w, 0.5f), 56);
}
OBL_TEST_CASE("gui_controls.number_adjust_steps_wrap_and_stop_exactly_at_the_limits")
{
    OBL_CHECK_EQ(gui_controls::AdjustStep(5.0f, 0, false, 1), 6.0f);
    OBL_CHECK_EQ(gui_controls::AdjustStep(5.0f, 0, false, -1), 4.0f);
    OBL_CHECK_EQ(gui_controls::AdjustStep(5.25f, 1, false, 1), static_cast<float>(5.25 + static_cast<double>(0.1f)));
    OBL_CHECK_EQ(gui_controls::AdjustStep(5.25f, 2, false, 1), 5.25f);
    // over the upper limit: |integer part + fraction| of the old value
    OBL_CHECK_EQ(gui_controls::AdjustStep(999.5f, 0, false, 1), 999.5f);
    // under zero: no wrap flag gives 0 + fraction, wrap gives 999 + fraction
    OBL_CHECK_EQ(gui_controls::AdjustStep(0.5f, 0, false, -1), 0.5f);
    OBL_CHECK_EQ(gui_controls::AdjustStep(0.5f, 0, true, -1), 999.5f);
    OBL_CHECK_EQ(gui_controls::AdjustStep(-0.5f, 0, false, -1), 0.5f); // |0 + (-0.5)|
}

namespace {
struct ButtonHost {
    bool hovered = true, busy = false;
    float now = 10.0f;
    std::vector<std::string> calls;
    bool Hovered()
    {
        calls.push_back("hovered");
        return hovered;
    }
    bool Busy()
    {
        calls.push_back("busy");
        return busy;
    }
    void Invalidate() { calls.push_back("invalidate"); }
    float Now() { return now; }
    void Fire(bool right) { calls.push_back(right ? "fire:right" : "fire:left"); }
    void Click(bool right) { calls.push_back(right ? "click:right" : "click:left"); }
};
} // namespace

OBL_TEST_CASE("gui_controls.button_text_prefixes")
{
    auto t = gui_controls::ParseButtonText("BUTVAR_MONEY");
    OBL_CHECK_EQ(t.prefix, 7u);
    OBL_CHECK_EQ(t.textSource, 1);
    OBL_CHECK_EQ(t.stateVariable, 0);
    t = gui_controls::ParseButtonText("BUTSTATENTVAR_X");
    OBL_CHECK_EQ(t.prefix, 14u);
    OBL_CHECK_EQ(t.textSource, 0);
    OBL_CHECK_EQ(t.stateVariable, 2);
    t = gui_controls::ParseButtonText("BUTSTATEVAR_X");
    OBL_CHECK_EQ(t.prefix, 12u);
    OBL_CHECK_EQ(t.stateVariable, 1);
    t = gui_controls::ParseButtonText("butvar_lower");
    OBL_CHECK_EQ(t.prefix, 0u);
    OBL_CHECK_EQ(t.textSource, 0);
}
OBL_TEST_CASE("gui_controls.button_mouse_hover_press_and_leave")
{
    ui::GuiButtonData w{};
    ButtonHost host;
    OBL_CHECK(!gui_controls::ButtonMouse(w, 0, host));
    OBL_CHECK_EQ(w.m_state, 2);
    OBL_CHECK(gui_controls::ButtonMouse(w, 1, host));
    OBL_CHECK_EQ(w.m_state, 4);
    OBL_CHECK_EQ(w.m_clickTime, 10.0f);
    OBL_CHECK_EQ(w.m_repeatDelay, 0.75f);
    OBL_CHECK_EQ(w.m_rightClick, 0);
    w.m_state = 2;
    host.hovered = false;
    gui_controls::ButtonMouse(w, 0, host);
    OBL_CHECK_EQ(w.m_state, 0);
    w.m_state = 2;
    gui_controls::ButtonMouse(w, 3, host);
    OBL_CHECK_EQ(w.m_state, 0);
    w.m_state = 3;
    host.calls.clear();
    gui_controls::ButtonMouse(w, 0, host);
    OBL_CHECK(host.calls.empty()); // disabled: hover is not even asked
}
OBL_TEST_CASE("gui_controls.button_held_repeat_fires_after_delay")
{
    ui::GuiButtonData w{};
    ButtonHost host;
    w.m_state = 1;
    w.m_clickTime = 9.5f;
    w.m_repeatDelay = 0.75f;
    OBL_CHECK(!gui_controls::ButtonMouse(w, 2, host));
    OBL_CHECK_EQ(w.m_clickTime, 10.0f);
    OBL_CHECK_EQ(w.m_held, 1); // no repeat: time restarts
    w.m_repeat = 1;
    w.m_held = 0;
    w.m_clickTime = 9.5f;
    OBL_CHECK(!gui_controls::ButtonMouse(w, 2, host));
    OBL_CHECK_EQ(w.m_held, 1);
    w.m_clickTime = 9.0f;
    OBL_CHECK(gui_controls::ButtonMouse(w, 2, host));
    OBL_CHECK_EQ(host.calls.back(), std::string("fire:left"));
    OBL_CHECK_EQ(w.m_repeatDelay, 0.05f);
    OBL_CHECK_EQ(w.m_clicked, 1);
}
OBL_TEST_CASE("gui_controls.button_right_press_and_tick")
{
    ui::GuiButtonData w{};
    ButtonHost host;
    w.m_state = 2;
    OBL_CHECK(gui_controls::ButtonRightMouse(w, 1, host));
    OBL_CHECK_EQ(w.m_state, 4);
    OBL_CHECK_EQ(w.m_rightClick, 1);
    host.now = 10.01f;
    gui_controls::ButtonTick(w, 0.0f, host);
    OBL_CHECK_EQ(w.m_state, 4); // 0.05 s not elapsed yet
    host.now = 10.1f;
    host.calls.clear();
    gui_controls::ButtonTick(w, 0.0f, host);
    OBL_REQUIRE(host.calls.size() == 1);
    OBL_CHECK_EQ(host.calls[0], std::string("click:right"));
    OBL_CHECK_EQ(w.m_state, 1);
    OBL_CHECK_EQ(w.m_clicked, 1);
    OBL_CHECK_EQ(w.m_clickTime, 10.1f);
    host.now = 10.2f;
    host.hovered = false;
    gui_controls::ButtonTick(w, 0.0f, host);
    OBL_CHECK_EQ(w.m_state, 0);
    w.m_state = 1;
    w.m_clickTime = 0;
    host.busy = true;
    gui_controls::ButtonTick(w, 0.0f, host);
    OBL_CHECK_EQ(w.m_state, 1);
    w.m_blink = 1;
    w.m_blinkTime = 0.5f;
    w.m_blinkFrame = 1;
    host.calls.clear();
    gui_controls::ButtonTick(w, 0.3f, host);
    OBL_CHECK_EQ(w.m_blinkTime, 0.0f);
    OBL_CHECK_EQ(w.m_blinkFrame, 0);
    OBL_CHECK_EQ(host.calls[0], std::string("invalidate"));
}
OBL_TEST_CASE("gui_controls.radio_flag_updates")
{
    OBL_CHECK_EQ(gui_controls::RadioHoverFlags(2, true), 10u);
    OBL_CHECK_EQ(gui_controls::RadioHoverFlags(10, true), 10u);
    OBL_CHECK_EQ(gui_controls::RadioHoverFlags(10, false), 2u);
    OBL_CHECK_EQ(gui_controls::RadioSelectFlags(2, true), 4u);
    OBL_CHECK_EQ(gui_controls::RadioSelectFlags(6, true), 6u); // already has the flag: unchanged, even with both bits
    OBL_CHECK_EQ(gui_controls::RadioSelectFlags(0x1c, false), 0x1au);
    OBL_CHECK_EQ(gui_controls::RadioDisabledFlags(4, true), 0x14u);
    OBL_CHECK_EQ(gui_controls::RadioDisabledFlags(0x14, false), 4u);
}
