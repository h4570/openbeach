#include "core/char16.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_widgets_leaves.h"
#include "unit_test.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <cmath>

using namespace obl::gui_widgets;
using Widget = obl::ui::GuiWidget;

OBL_TEST_CASE("gui_widgets.image_hit_clears_hover_and_draw_uses_persistent_instance")
{
    obl::ui::GuiImageData w{};
    Construct(w, 10, 20, 30, 40);
    OBL_CHECK(ImageHitTest(w, 30, 40));
    OBL_CHECK(!ImageHitTest(w, 31, 40));
    OBL_CHECK_EQ(w.m_hovered, 0);
    obl::gui_canvas::RecordingCanvas canvas;
    auto sprite = canvas.SeedSprite("image", {1, 20, 20}, 3, 7, 1);
    DrawImage(w, sprite, canvas);
    DrawImage(w, sprite, canvas);
    OBL_REQUIRE(canvas.calls.size() == 1);
    OBL_CHECK_EQ(canvas.calls[0].depth, 0u);
    OBL_CHECK_EQ(canvas.calls[0].frame, 7);
    OBL_CHECK_EQ(w.m_dirty, 0);
    OBL_CHECK_EQ(canvas.released, 0);
}

OBL_TEST_CASE("gui_widgets.bar_keeps_two_pixel_minimum_and_uses_cached_dimensions")
{
    obl::ui::GuiBarData w{};
    Construct(w, 10, 20, 111, 29);
    w.m_min = 0;
    w.m_max = 100;
    w.m_width = 101;
    w.m_height = 9;
    w.m_lastValue = -1;
    obl::gui_canvas::RecordingCanvas canvas;
    auto pack = [](int r, int g, int b) { return static_cast<uint16_t>((r << 8) ^ (g << 3) ^ b); };
    DrawBar(w, 0, pack, canvas);
    OBL_REQUIRE(canvas.calls.size() == 2);
    OBL_CHECK_EQ(canvas.calls[0].left, 10);
    OBL_CHECK_EQ(canvas.calls[0].right, 12);
    OBL_CHECK_EQ(canvas.calls[1].left, 12);
    OBL_CHECK_EQ(canvas.calls[1].right, 111);
    OBL_CHECK_EQ(w.m_lastValue, 0.0f);
    DrawBar(w, 0, pack, canvas);
    OBL_CHECK_EQ(canvas.calls.size(), 2u);
    DrawBar(w, 50.0000001, pack, canvas);
    OBL_CHECK_EQ(canvas.calls.size(), 4u);
    // Comparison is to the unrounded double, so this value redraws even though its float store remains 50.
    DrawBar(w, 50.0000001, pack, canvas);
    OBL_CHECK_EQ(canvas.calls.size(), 6u);
    w.m_dirty = 1;
    w.m_colorMode = 1;
    DrawBar(w, 1000, pack, canvas);
    OBL_CHECK_EQ(w.m_lastValue, 100.0f);
    OBL_CHECK_EQ(canvas.calls[6].color, pack(0, 200, 0));
}

namespace {
// Fills the fields only: the C++ object pointer before them stays valid for the sanitizers' vptr check.
Widget Filled(uint8_t byte)
{
    Widget w;
    const size_t fields = offsetof(Widget, m_x0);
    std::memset(reinterpret_cast<uint8_t*>(&w) + fields, byte, sizeof(w) - fields);
    return w;
}

OBL_TEST_CASE("gui_widgets.text_gates_precedence_and_preserved_alignment")
{
    obl::ui::GuiTextData w{};
    Construct(w, 10, 20, 110, 70);
    w.m_font = 2;
    w.m_color = 0x123456;
    w.m_text = u"borrowed";
    w.m_ownedText = const_cast<obl::Char16*>(u"fallback");
    w.m_drawOnSurface = 1;
    obl::gui_canvas::RecordingCanvas canvas;
    canvas.SeedText(2, {0, 0, 0, 0, 0, true, 2});
    obl::gui_widgets::DrawText(w, false, canvas);
    OBL_CHECK_EQ(canvas.calls.size(), 1u);
    OBL_CHECK(canvas.calls[0].text == u"borrowed");
    OBL_CHECK_EQ(canvas.calls[0].state.align, 2);
    OBL_CHECK(!canvas.calls[0].state.outlined);
    OBL_CHECK_EQ(w.m_dirty, 0);
    obl::gui_widgets::DrawText(w, false, canvas);
    obl::gui_widgets::DrawText(w, true, canvas);
    OBL_CHECK_EQ(canvas.calls.size(), 1u);
    w.m_drawOnSurface = 0;
    w.m_text = nullptr;
    w.m_alignFlags = 0x47;
    obl::gui_widgets::DrawText(w, true, canvas);
    OBL_CHECK_EQ(canvas.calls.size(), 2u);
    OBL_CHECK(canvas.calls[1].text == u"fallback");
    OBL_CHECK_EQ(canvas.calls[1].state.align, 1);
    OBL_CHECK(canvas.calls[1].state.outlined);
    OBL_CHECK_EQ(w.m_dirty, 0);
    w.m_dirty = 1;
    w.m_drawOnSurface = 1;
    w.m_ownedText = nullptr;
    obl::gui_widgets::DrawText(w, false, canvas);
    OBL_CHECK_EQ(canvas.calls.size(), 2u);
    OBL_CHECK_EQ(w.m_dirty, 0);
}

OBL_TEST_CASE("gui_widgets.tga_loads_once_and_draws_fixed_screen_rectangle")
{
    struct Env {
        std::string calls;
        void SetVisible(obl::ui::GuiTgaData& w, uint32_t v)
        {
            w.m_visible = static_cast<uint8_t>(v);
            calls += 'v';
        }
        void* Load(const char*)
        {
            calls += 'l';
            return this;
        }
        void BuildPalette(void*) { calls += 'p'; }
        void DestroyImage(void*) { calls += 'f'; }
        void DrawImage(void* image, int x, int y, int l, int t, int r, int b, int mode, uint32_t flags, uint32_t mask)
        {
            OBL_CHECK(image == this);
            OBL_CHECK_EQ(x, 0);
            OBL_CHECK_EQ(y, 0);
            OBL_CHECK_EQ(l, 0);
            OBL_CHECK_EQ(t, 0);
            OBL_CHECK_EQ(r, 800);
            OBL_CHECK_EQ(b, 600);
            OBL_CHECK_EQ(mode, 0);
            OBL_CHECK_EQ(flags, 0u);
            OBL_CHECK_EQ(mask, 0u);
            calls += 'd';
        }
    } env;
    obl::ui::GuiTgaData w{};
    obl::gui_widgets::SetTgaVisible(w, 0x12340080, env);
    OBL_CHECK_EQ(w.m_visible, 0x80);
    obl::gui_widgets::SetTgaVisible(w, 1, env);
    obl::gui_widgets::DrawTga(w, env);
    obl::gui_widgets::SetTgaVisible(w, 0x100, env);
    obl::gui_widgets::SetTgaVisible(w, 0, env);
    OBL_CHECK(w.m_image == nullptr);
    OBL_CHECK(env.calls == "vlpvdvfv");
}
} // namespace

OBL_TEST_CASE("gui_widgets.construct_writes_exactly_its_own_fields")
{
    Widget w = Filled(0xcd);
    Construct(w, 10, 20, 30, 40);
    OBL_CHECK_EQ(w.m_x0, 10);
    OBL_CHECK_EQ(w.m_y0, 20);
    OBL_CHECK_EQ(w.m_x1, 30);
    OBL_CHECK_EQ(w.m_y1, 40);
    OBL_CHECK_EQ(w.m_baseX0, 10);
    OBL_CHECK_EQ(w.m_baseY0, 20);
    OBL_CHECK_EQ(w.m_baseX1, 30);
    OBL_CHECK_EQ(w.m_baseY1, 40);
    OBL_CHECK_EQ(w.m_hoverTime, 0.0f);
    OBL_CHECK_EQ(w.m_visible, 0);
    OBL_CHECK_EQ(w.m_tooltipX, 0);
    OBL_CHECK_EQ(w.m_tooltipY, 0);
    OBL_CHECK(w.m_tooltip == nullptr);
    OBL_CHECK_EQ(w.m_hovered, 0);
    OBL_CHECK_EQ(w.m_held, 0);
    OBL_CHECK_EQ(w.m_clicked, 0);
    OBL_CHECK_EQ(w.m_dirty, 1);
    // Type and id belong to the derived constructor and stay untouched.
    OBL_CHECK_EQ(w.m_type, static_cast<int32_t>(0xcdcdcdcd));
    OBL_CHECK_EQ(w.m_id, static_cast<int32_t>(0xcdcdcdcd));
}

OBL_TEST_CASE("gui_widgets.base_class_names_itself")
{
    OBL_CHECK(std::string(Widget{}.ClassName()) == "GuiWidget");
}

OBL_TEST_CASE("gui_widgets.origin_translate_and_position_keep_the_base_rectangle")
{
    Widget w = Filled(0);
    Construct(w, 10, 20, 110, 70);
    SetOrigin(w, 5, -7);
    OBL_CHECK_EQ(w.m_x0, 15);
    OBL_CHECK_EQ(w.m_y0, 13);
    OBL_CHECK_EQ(w.m_x1, 115);
    OBL_CHECK_EQ(w.m_y1, 63);
    OBL_CHECK_EQ(w.m_baseX0, 10);
    OBL_CHECK_EQ(w.m_baseY1, 70);
    Translate(w, 1, 2); // relative to the current rectangle, not the base
    OBL_CHECK_EQ(w.m_x0, 16);
    OBL_CHECK_EQ(w.m_y0, 15);
    OBL_CHECK_EQ(w.m_x1, 116);
    OBL_CHECK_EQ(w.m_y1, 65);
    SetPosition(w, 100, 200); // keeps the 100 x 50 size
    OBL_CHECK_EQ(w.m_x0, 100);
    OBL_CHECK_EQ(w.m_y0, 200);
    OBL_CHECK_EQ(w.m_x1, 200);
    OBL_CHECK_EQ(w.m_y1, 250);
    OBL_CHECK_EQ(w.m_baseX0, 10);
    OBL_CHECK_EQ(w.m_baseX1, 110);
    SetOrigin(w, 0, 0); // SetOrigin always starts from the base again
    OBL_CHECK_EQ(w.m_x0, 10);
    OBL_CHECK_EQ(w.m_y1, 70);
}

OBL_TEST_CASE("gui_widgets.coordinates_wrap_like_32_bit_registers")
{
    Widget w = Filled(0);
    Construct(w, std::numeric_limits<int32_t>::max(), 0, std::numeric_limits<int32_t>::max(), 0);
    Translate(w, 1, 0);
    OBL_CHECK_EQ(w.m_x0, std::numeric_limits<int32_t>::min());
    SetPosition(w, std::numeric_limits<int32_t>::max(), 0); // width 0 stays 0
    OBL_CHECK_EQ(w.m_x1, std::numeric_limits<int32_t>::max());
}

OBL_TEST_CASE("gui_widgets.hit_test_is_inclusive_and_only_a_hit_sets_hover")
{
    Widget w = Filled(0);
    Construct(w, 10, 20, 30, 40);
    for (int x : {10, 30})
        for (int y : {20, 40}) {
            w.m_hovered = 0;
            OBL_CHECK(HitTest(w, x, y));
            OBL_CHECK_EQ(w.m_hovered, 1);
        }
    for (auto [x, y] : {std::pair{9, 30}, {31, 30}, {20, 19}, {20, 41}}) {
        w.m_hovered = 1; // a miss does not clear a previous hover
        OBL_CHECK(!HitTest(w, x, y));
        OBL_CHECK_EQ(w.m_hovered, 1);
        w.m_hovered = 0;
        OBL_CHECK(!HitTest(w, x, y));
        OBL_CHECK_EQ(w.m_hovered, 0);
    }
}

OBL_TEST_CASE("gui_widgets.visibility_flag_copies_the_raw_byte_and_hides_on_zero")
{
    Widget w = Filled(0);
    OBL_CHECK(!SetVisibleFlag(w, 1));
    OBL_CHECK_EQ(w.m_visible, 1);
    OBL_CHECK(!SetVisibleFlag(w, 0x80));
    OBL_CHECK_EQ(w.m_visible, 0x80);
    OBL_CHECK(SetVisibleFlag(w, 0));
    OBL_CHECK_EQ(w.m_visible, 0);
}

OBL_TEST_CASE("gui_widgets.bounds_and_invalidate")
{
    Widget w = Filled(0);
    Construct(w, 1, 2, 3, 4);
    w.m_dirty = 0;
    Invalidate(w);
    OBL_CHECK_EQ(w.m_dirty, 1);
    const Rect r = GetBounds(w);
    OBL_CHECK_EQ(r.x0, 1);
    OBL_CHECK_EQ(r.y0, 2);
    OBL_CHECK_EQ(r.x1, 3);
    OBL_CHECK_EQ(r.y1, 4);
}

namespace {
struct TooltipEnv {
    float dt = 0.02f, delay = 1.5f;
    bool hit = true, hovered = true;
    std::string calls;
    float DeltaTime()
    {
        calls += "dt ";
        return dt;
    }
    bool HitTest(Widget&, int, int)
    {
        calls += "hit ";
        return hit;
    }
    bool IsHovered(Widget&)
    {
        calls += "hovered ";
        return hovered;
    }
    float Delay()
    {
        calls += "delay ";
        return delay;
    }
    void Hide(Widget&) { calls += "hide "; }
    void Show(Widget&, int, int, const char*) { calls += "show "; }
};
Widget TooltipWidget()
{
    Widget w{};
    static char text[] = "tip";
    w.m_tooltip = text;
    w.m_tooltipX = 10;
    w.m_tooltipY = 20;
    return w;
}
} // namespace

OBL_TEST_CASE("gui_widgets.tooltip_accumulates_stationary_time_and_adds_twice_on_crossing")
{
    auto w = TooltipWidget();
    TooltipEnv env;
    w.m_hoverTime = 1.49f;
    OBL_CHECK(!TrackTooltip(w, 10, 20, 0, env));
    OBL_CHECK_EQ(w.m_hoverTime, (1.49f + env.dt) + env.dt);
    OBL_CHECK_EQ(env.calls, std::string("dt hit delay show "));
    // Once displayed, movement inside the widget does not reset the timer.
    env.calls.clear();
    const float previous = w.m_hoverTime;
    TrackTooltip(w, 11, 21, 0, env);
    OBL_CHECK_EQ(w.m_hoverTime, previous + env.dt);
    OBL_CHECK_EQ(w.m_tooltipX, 11);
    OBL_CHECK_EQ(w.m_tooltipY, 21);
    OBL_CHECK_EQ(env.calls, std::string("dt hit delay show "));
}

OBL_TEST_CASE("gui_widgets.tooltip_movement_before_delay_and_leaving_reset_time")
{
    auto w = TooltipWidget();
    TooltipEnv env;
    w.m_hoverTime = 1.0f;
    TrackTooltip(w, 11, 20, 0, env);
    OBL_CHECK_EQ(w.m_hoverTime, 0.0f);
    OBL_CHECK_EQ(env.calls, std::string("dt hit delay "));
    env.calls.clear();
    env.hit = false;
    w.m_hoverTime = 3;
    TrackTooltip(w, 12, 20, 0, env);
    OBL_CHECK_EQ(w.m_hoverTime, 0.0f);
    OBL_CHECK_EQ(env.calls, std::string("dt hit hide "));
    env.calls.clear();
    w.m_tooltip = nullptr;
    w.m_hoverTime = 4;
    TrackTooltip(w, 13, 20, 1, env);
    OBL_CHECK_EQ(w.m_hoverTime, 4.0f);
    OBL_CHECK_EQ(w.m_tooltipX, 13);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("gui_widgets.tooltip_expiration_and_mouse_states_have_fixed_hide_order")
{
    for (int state : {1, 2, 3}) {
        auto w = TooltipWidget();
        TooltipEnv env;
        w.m_hoverTime = 1.5f;
        TrackTooltip(w, 10, 20, state, env);
        OBL_CHECK_EQ(w.m_hoverTime, 7.0f);
        OBL_CHECK_EQ(env.calls, std::string("dt hit delay hide hide "));
    }
    auto w = TooltipWidget();
    TooltipEnv env;
    w.m_hoverTime = 7;
    TrackTooltip(w, 10, 20, 0, env);
    OBL_CHECK_EQ(w.m_hoverTime, 7.0f);
    OBL_CHECK_EQ(env.calls, std::string("dt hit delay hide "));
}

OBL_TEST_CASE("gui_widgets.click_uses_virtual_hover_and_accepts_equal_or_unordered_timer")
{
    for (float timer : {1.5f, std::numeric_limits<float>::quiet_NaN()}) {
        auto w = TooltipWidget();
        TooltipEnv env;
        w.m_hoverTime = timer;
        OBL_CHECK(!HideTooltipOnClick(w, 1, env));
        OBL_CHECK_EQ(w.m_hoverTime, 7.0f);
        OBL_CHECK_EQ(env.calls, std::string("hovered delay hide "));
    }
    auto w = TooltipWidget();
    TooltipEnv env;
    env.hovered = false;
    HideTooltipOnClick(w, 2, env);
    OBL_CHECK_EQ(env.calls, std::string("hovered "));
    env.calls.clear();
    env.hovered = true;
    HideTooltipOnClick(w, 3, env);
    OBL_CHECK_EQ(env.calls, std::string("hovered delay "));
}

OBL_TEST_CASE("gui_widgets.tooltip_delay_uses_only_positive_cooldown")
{
    obl::ui::GuiManager gui{};
    for (float cooldown : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()}) {
        gui.m_tooltipCooldown = cooldown;
        OBL_CHECK_EQ(TooltipDelay(gui), 1.5f);
    }
    gui.m_tooltipCooldown = std::numeric_limits<float>::min();
    OBL_CHECK_EQ(TooltipDelay(gui), 0.05f);
}

namespace {
struct TreeEnv {
    std::string calls;
    int consume = -1;
    obl::ui::GuiContainer* container = nullptr;
    bool OnMouseLeft(Widget& w, int x, int y, int state)
    {
        OBL_CHECK_EQ(x, -10);
        OBL_CHECK_EQ(y, 20);
        OBL_CHECK_EQ(state, 3);
        calls += std::to_string(w.m_id);
        return w.m_id == consume;
    }
    void DrawOverlay(Widget& w)
    {
        calls += std::to_string(w.m_id);
        if (container) container->m_children = container->m_children->m_next->m_next;
    }
    void DeletingDestruct(Widget&, uint32_t flags)
    {
        OBL_CHECK_EQ(flags, 1u);
        calls += "delete";
    }
    void SetVisible(Widget& w, uint8_t value)
    {
        calls += "visible";
        w.m_visible = value;
    }
    void Invalidate(Widget& w)
    {
        OBL_CHECK(w.m_visible != 0);
        calls += "invalidate";
    }
};
} // namespace

OBL_TEST_CASE("gui_widgets.container_preserves_order_and_stops_at_the_first_consumer")
{
    using Node = obl::ui::GuiContainerChildNode;
    Widget a{}, b{};
    a.m_id = 0;
    b.m_id = 1;
    Node sentinel{}, first{}, second{};
    sentinel.m_next = &first;
    first.m_next = &second;
    second.m_next = &sentinel;
    first.m_widget = &a;
    second.m_widget = &b;
    obl::ui::GuiContainer container{};
    container.m_children = &sentinel;
    for (int consume : {-1, 0, 1}) {
        TreeEnv env;
        env.consume = consume;
        OBL_CHECK_EQ(ContainerOnMouseLeft(container, -10, 20, 3, env), consume >= 0);
        OBL_CHECK_EQ(env.calls, consume == 0 ? std::string("0") : std::string("01"));
    }
    TreeEnv env;
    ContainerDrawOverlay(container, env);
    OBL_CHECK_EQ(env.calls, std::string("01"));
    env.calls.clear();
    env.container = &container;
    ContainerDrawOverlay(container, env);
    OBL_CHECK_EQ(env.calls, std::string("0"));
    container.m_children = &sentinel;
    sentinel.m_next = &sentinel;
    env.calls.clear();
    OBL_CHECK(!ContainerOnMouseLeft(container, -10, 20, 3, env));
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("gui_widgets.destroy_widget_keeps_the_wrapper_and_rejects_unrecognized_types")
{
    Widget w{};
    obl::ui::GuiWidgetListNode node{};
    node.m_widget = &w;
    for (int type : {-1, std::numeric_limits<int32_t>::min(), 0, 1, 21, 22, 23, std::numeric_limits<int32_t>::max()}) {
        TreeEnv env;
        w.m_type = type;
        DestroyWidget(node, env);
        OBL_CHECK_EQ(env.calls, type >= 0 && type <= 22 ? std::string("delete") : std::string());
        OBL_CHECK(node.m_widget == &w);
    }
    TreeEnv env;
    node.m_widget = nullptr;
    DestroyWidget(node, env);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("gui_widgets.visible_invalidate_uses_the_raw_low_byte_after_the_base_setter")
{
    for (uint32_t value : {0u, 1u, 0x80u, 0x100u, 0xabcdef80u}) {
        Widget w{};
        TreeEnv env;
        SetVisibleInvalidate(w, value, env);
        OBL_CHECK_EQ(w.m_visible, static_cast<uint8_t>(value));
        OBL_CHECK_EQ(env.calls, static_cast<uint8_t>(value) ? std::string("visibleinvalidate") : std::string("visible"));
    }
}
