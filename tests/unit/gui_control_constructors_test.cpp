// Constructors of the control widgets (src/game/ui/gui_controls.h), field by field. The other widget families are covered next to
// their logic: gui_containers_test, gui_tabs_test, gui_dialog_test, gui_lists_test, gui_tables_test and gui_table_frame_test.
#include "game/ui/gui_controls.h"
#include "unit_test.h"

#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace obl;

namespace {

void LeftAction(int) {}
void RightAction(int) {}

ui::SpriteInstance* FakeSprite(uintptr_t index)
{
    return reinterpret_cast<ui::SpriteInstance*>(index * 16); // never dereferenced: the environment answers for it
}

struct FakeEnv {
    std::vector<std::string> log;
    std::vector<std::unique_ptr<char[]>> strings;
    std::deque<ui::GuiButtonData> buttons;
    std::deque<ui::GuiWidget> widgets;
    std::vector<std::unique_ptr<ui::GuiContainerChildNode>> nodes;
    std::map<const ui::GuiWidget*, std::string> names;
    int spriteWidth = 40, spriteHeight = 25;
    int created = 0, released = 0;

    ui::GuiAction FindAction(const char* name)
    {
        if (!std::strcmp(name, "left")) return &LeftAction;
        if (!std::strcmp(name, "right")) return &RightAction;
        return nullptr;
    }
    char* CopyString(const char* text)
    {
        const size_t size = std::strlen(text) + 1;
        strings.push_back(std::make_unique<char[]>(size));
        std::memcpy(strings.back().get(), text, size);
        return strings.back().get();
    }
    ui::SpriteInstance* CreateSprite(const char* name)
    {
        log.push_back(std::string("sprite:") + name);
        return FakeSprite(static_cast<uintptr_t>(++created));
    }
    ui::SpriteInstance* CreateUnobservedSprite(const char* name)
    {
        log.push_back(std::string("unobserved:") + name);
        return FakeSprite(static_cast<uintptr_t>(++created));
    }
    void ReleaseSprite(ui::SpriteInstance*) { ++released; }
    int SpriteWidth(const ui::SpriteInstance*) { return spriteWidth; }
    int SpriteHeight(const ui::SpriteInstance*) { return spriteHeight; }
    void SetAnimationType(ui::SpriteInstance*, int type) { log.push_back("animation:" + std::to_string(type)); }
    ui::GuiContainerChildNode* AllocateNode()
    {
        nodes.push_back(std::make_unique<ui::GuiContainerChildNode>());
        return nodes.back().get();
    }
    ui::GuiButtonData* NewButton(int x0, int y0, int x1, int y1, const char* graphic)
    {
        buttons.emplace_back(x0, y0, x1, y1);
        names[&buttons.back()] = graphic;
        return &buttons.back();
    }
    ui::GuiWidget* NewMainItem(int x0, int y0, int x1, int y1, int variant)
    {
        widgets.emplace_back(x0, y0, x1, y1);
        names[&widgets.back()] = "item" + std::to_string(variant);
        return &widgets.back();
    }
    ui::GuiWidget* NewImage(int x0, int y0, int x1, int y1, const char* sprite)
    {
        widgets.emplace_back(x0, y0, x1, y1);
        names[&widgets.back()] = sprite;
        return &widgets.back();
    }
    void SetFrame(ui::GuiWidget* image, int frame) { log.push_back("frame:" + names[image] + ":" + std::to_string(frame)); }
    void SetVisibleBase(ui::GuiWidget&, uint32_t visible) { log.push_back("self-visible:" + std::to_string(visible)); }
    void SetVisible(ui::GuiWidget& widget, uint32_t visible) { log.push_back("visible:" + names[&widget] + ":" + std::to_string(visible)); }
    void SetTooltip(ui::GuiWidget& widget, const char* text) { log.push_back("tooltip:" + names[&widget] + ":" + text); }
};

void CheckRect(const ui::GuiWidget& w, int x0, int y0, int x1, int y1)
{
    OBL_CHECK_EQ(w.m_x0, x0);
    OBL_CHECK_EQ(w.m_y0, y0);
    OBL_CHECK_EQ(w.m_x1, x1);
    OBL_CHECK_EQ(w.m_y1, y1);
}

} // namespace

OBL_TEST_CASE("gui_control_constructors.button_sizes_itself_from_the_sprite_and_owns_its_strings")
{
    FakeEnv env;
    ui::GuiButtonData w(10, 20, 11, 21);
    gui_controls::ConstructButton(w, "UI3_ok", "MAIN_TIPTEXT_BUILD", "left", "right", env);
    CheckRect(w, 10, 20, 50, 45);
    OBL_CHECK_EQ(w.m_baseX0, 10);
    OBL_CHECK_EQ(w.m_baseY0, 20);
    OBL_CHECK_EQ(w.m_baseX1, 50);
    OBL_CHECK_EQ(w.m_baseY1, 45);
    OBL_CHECK_EQ(w.m_type, 0);
    OBL_CHECK_EQ(w.m_state, 0);
    OBL_CHECK_EQ(w.m_drawnState, 3);
    OBL_CHECK_EQ(w.m_repeat, 0);
    OBL_CHECK_EQ(w.m_repeatDelay, 0.75f);
    OBL_CHECK_EQ(w.m_blinkFrame, 1);
    OBL_CHECK(w.m_leftAction == &LeftAction);
    OBL_CHECK(w.m_rightAction == &RightAction);
    OBL_CHECK(std::string(w.m_text) == "MAIN_TIPTEXT_BUILD");
    OBL_CHECK(std::string(w.m_graphic) == "UI3_ok");
    OBL_CHECK(w.m_instance == nullptr); // the measuring sprite goes back; Draw makes its own
    OBL_CHECK(w.m_tooltip == nullptr);
    OBL_CHECK_EQ(env.created, 1);
    OBL_CHECK_EQ(env.released, 1);
}

OBL_TEST_CASE("gui_control_constructors.button_without_graphic_or_caption_keeps_its_rectangle_and_owns_nothing")
{
    FakeEnv env;
    ui::GuiButtonData w(1, 2, 3, 4);
    gui_controls::ConstructButton(w, "none", "NONE", "NoSuchAction", "NONE", env);
    CheckRect(w, 1, 2, 3, 4);
    OBL_CHECK(w.m_text == nullptr);
    OBL_CHECK(w.m_graphic == nullptr);
    OBL_CHECK(w.m_leftAction == nullptr);
    OBL_CHECK_EQ(env.created, 0);
    OBL_CHECK(env.strings.empty());
}

OBL_TEST_CASE("gui_control_constructors.button_caption_prefix_selects_the_source_and_is_not_copied")
{
    struct Case {
        const char* text;
        const char* caption;
        int source, state;
    };
    for (const Case& c :
            {Case{"BUTVAR_TEST", "TEST", 1, 0}, Case{"BUTSTATEVAR_TEST", "TEST", 0, 1}, Case{"BUTSTATENTVAR_TEST", "TEST", 0, 2},
                    Case{"butvar_TEST", "butvar_TEST", 0, 0}, Case{"", "", 0, 0}, Case{"BUTVAR_", "", 1, 0}}) {
        FakeEnv env;
        ui::GuiButtonData w(0, 0, 9, 9);
        gui_controls::ConstructButton(w, "NONE", c.text, "NONE", "NONE", env);
        OBL_CHECK(std::string(w.m_text) == c.caption);
        OBL_CHECK_EQ(w.m_textSource, c.source);
        OBL_CHECK_EQ(w.m_stateVariable, c.state);
    }
}

OBL_TEST_CASE("gui_control_constructors.check_box_owns_its_key_and_keeps_its_sprite")
{
    FakeEnv env;
    ui::GuiCheckBoxData w(1, 2, 31, 22);
    gui_controls::ConstructCheckBox(w, "UI3_check", "SEA_EFFECTS", env);
    CheckRect(w, 1, 2, 31, 22);
    OBL_CHECK_EQ(w.m_type, 1);
    OBL_CHECK_EQ(w.m_value, 0);
    OBL_CHECK(std::string(w.m_variable) == "SEA_EFFECTS");
    OBL_CHECK(w.m_instance == FakeSprite(1));
    OBL_CHECK_EQ(env.released, 0);
    OBL_CHECK(env.log == (std::vector<std::string>{"sprite:UI3_check"}));
}

OBL_TEST_CASE("gui_control_constructors.radio_button_keeps_value_font_and_optional_caption")
{
    FakeEnv env;
    ui::GuiRadioButtonData w(1, 2, 31, 22);
    gui_controls::ConstructRadio(w, "UI3_radio", "MAIN_TIPTEXT_BUILD", 2, -3, env);
    CheckRect(w, 1, 2, 31, 22);
    OBL_CHECK_EQ(w.m_type, 21);
    OBL_CHECK_EQ(w.m_flags, 2u);
    OBL_CHECK_EQ(w.m_value, -3);
    OBL_CHECK_EQ(w.m_font, 2);
    OBL_CHECK(std::string(w.m_text) == "MAIN_TIPTEXT_BUILD");
    OBL_CHECK(env.log == (std::vector<std::string>{"sprite:UI3_radio", "animation:3"}));

    ui::GuiRadioButtonData plain(0, 0, 1, 1);
    gui_controls::ConstructRadio(plain, "UI3_radio", "None", 0, 0, env);
    OBL_CHECK(plain.m_text == nullptr);
}

OBL_TEST_CASE("gui_control_constructors.radio_group_starts_with_an_empty_list_and_an_optional_sprite")
{
    FakeEnv env;
    ui::GuiRadioGroupData w(1, 2, 31, 22);
    gui_controls::ConstructRadioGroup(w, "PLAYLIST_MODE", "UI3_back", env);
    CheckRect(w, 1, 2, 31, 22);
    OBL_CHECK_EQ(w.m_type, 20);
    OBL_CHECK(std::string(w.m_variable) == "PLAYLIST_MODE");
    OBL_CHECK(w.m_children->m_next == w.m_children);
    OBL_CHECK(w.m_children->m_prev == w.m_children);
    OBL_CHECK(w.m_instance == FakeSprite(1));

    ui::GuiRadioGroupData plain(0, 0, 1, 1);
    gui_controls::ConstructRadioGroup(plain, "", "None", env);
    OBL_CHECK(plain.m_instance == nullptr);
    OBL_CHECK(std::string(plain.m_variable).empty());
}

OBL_TEST_CASE("gui_control_constructors.scroll_bar_builds_arrows_thumb_and_track_from_the_arrow_sprite")
{
    FakeEnv env;
    env.spriteWidth = 14;
    env.spriteHeight = 10;
    ui::GuiScrollBarData w(10, 20, 24, 120);
    gui_controls::ConstructScrollBar(w, 99, 20, 5, 7, env);
    CheckRect(w, 10, 20, 24, 120);
    OBL_CHECK_EQ(w.m_type, 5);
    OBL_CHECK_EQ(w.m_id, 7);
    OBL_CHECK_EQ(w.m_count, 20u);
    OBL_CHECK_EQ(w.m_visibleRows, 5u);
    OBL_CHECK_EQ(w.m_top, 15u); // clamped to count - rows
    OBL_CHECK_EQ(w.m_buttonHeight, 10);
    OBL_CHECK_EQ(w.m_trackHeight, 80.0f);
    OBL_CHECK_EQ(w.m_thumbHeight, 20);
    OBL_CHECK_EQ(w.m_extra, 0);
    OBL_CHECK_EQ(w.m_dragging, 0);
    OBL_REQUIRE(w.m_up && w.m_down && w.m_thumb && w.m_track);
    CheckRect(*w.m_up, 10, 20, 24, 30);
    CheckRect(*w.m_down, 10, 110, 24, 120);
    CheckRect(*w.m_thumb, 11, 90, 23, 110);
    CheckRect(*w.m_track, 10, 20, 24, 120);
    OBL_CHECK_EQ(env.buttons[0].m_repeat, 1);
    OBL_CHECK_EQ(env.buttons[1].m_repeat, 1);
    OBL_CHECK_EQ(env.names[w.m_thumb], "item5");
    OBL_CHECK_EQ(env.names[w.m_track], "item4");
    OBL_CHECK_EQ(env.released, 1);
    OBL_CHECK(env.log ==
            (std::vector<std::string>{
                    "unobserved:UI3_scrollbar_up", "self-visible:1", "visible:UI3_scrollbar_up:1", "visible:UI3_scrollbar_down:1"}));
}

OBL_TEST_CASE("gui_control_constructors.scroll_bar_raises_a_short_thumb_to_its_minimum")
{
    FakeEnv env;
    env.spriteHeight = 10;
    ui::GuiScrollBarData w(0, 0, 14, 100);
    gui_controls::ConstructScrollBar(w, 0, 200, 2, 0, env);
    OBL_CHECK_EQ(w.m_thumbHeight, 6);
    OBL_CHECK_EQ(w.m_extra, 6); // 80 / 200 * 2 = 0.8 px truncates to 0
    CheckRect(*w.m_thumb, 1, 10, 13, 16);
}

OBL_TEST_CASE("gui_control_constructors.slider_centers_its_knob_and_owns_its_variable")
{
    FakeEnv env;
    ui::GuiSliderData w(10, 20, 111, 40);
    gui_controls::ConstructSlider(w, "MUSIC_VOLUME", 7, 9, 2, env);
    CheckRect(w, 10, 20, 111, 40);
    OBL_CHECK_EQ(w.m_type, 6);
    OBL_CHECK_EQ(w.m_param0, 7u);
    OBL_CHECK_EQ(w.m_param1, 9u);
    OBL_CHECK_EQ(w.m_dragging, 0);
    OBL_CHECK_EQ(w.m_range, 94); // width minus the knob's width
    OBL_CHECK(std::string(w.m_variable) == "MUSIC_VOLUME");
    CheckRect(*w.m_track, 10, 25, 111, 28);
    CheckRect(*w.m_knob, 55, 20, 62, 33);
    OBL_CHECK_EQ(env.names[w.m_track], "item6");
    OBL_CHECK(env.log == (std::vector<std::string>{"frame:UI3_slider_button:2"}));
}

OBL_TEST_CASE("gui_control_constructors.number_adjust_lays_out_one_field_without_fraction")
{
    FakeEnv env;
    ui::GuiNumberAdjustData w(100, 5, 190, 18);
    gui_controls::ConstructNumberAdjust(w, 0, 3, "HOURS", 1, 0, env);
    CheckRect(w, 100, 5, 190, 18);
    OBL_CHECK_EQ(w.m_type, 0x13);
    OBL_CHECK_EQ(w.m_kind, 0);
    OBL_CHECK_EQ(w.m_font, 3);
    OBL_CHECK_EQ(w.m_wrap, 1);
    OBL_CHECK_EQ(w.m_part, 0);
    OBL_CHECK(w.m_provider == nullptr);
    OBL_CHECK(w.m_right == nullptr);
    OBL_CHECK(std::string(w.m_variable) == "HOURS");
    CheckRect(*w.m_left, 100, 5, 190, 18);
    CheckRect(*w.m_dec, 133, 5, 141, 18);
    CheckRect(*w.m_inc, 143, 5, 151, 18);
    OBL_CHECK(env.log == (std::vector<std::string>{"frame:UI3_field_n_left:0", "frame:UI3_field_n_left:1"}));
}

OBL_TEST_CASE("gui_control_constructors.number_adjust_with_fraction_and_tooltips")
{
    FakeEnv env;
    ui::GuiNumberAdjustData w(100, 5, 190, 18);
    gui_controls::ConstructNumberAdjust(w, 1, 0, "PRICE", 0, 1, env);
    OBL_REQUIRE(w.m_right != nullptr);
    CheckRect(*w.m_right, 133, 5, 150, 18);
    CheckRect(*w.m_dec, 157, 5, 165, 18);
    CheckRect(*w.m_inc, 167, 5, 175, 18);
    OBL_CHECK_EQ(env.buttons[0].m_repeat, 1);
    OBL_CHECK_EQ(env.buttons[1].m_repeat, 1);
    OBL_CHECK(env.log ==
            (std::vector<std::string>{"frame:UI3_field_n_left:0", "frame:UI3_field_n_right:0", "tooltip:UI3_dec_flat:NUMWIDGET_TIPTEXT_DEC",
                    "tooltip:UI3_inc_flat:NUMWIDGET_TIPTEXT_INC", "frame:UI3_field_n_left:1", "frame:UI3_field_n_right:0"}));
}
