#include "unit_test.h"
#include "game/ui/gui_manager_buttons.h"
#include <string>
#include <vector>
using namespace obl::gui_manager_buttons;
namespace layout = obl::layout;
namespace ui = obl::ui;
namespace {
struct Env {
    std::vector<std::string> events;
    bool inScenario = true, worldLoaded = true, footprint = false;
    int wharf = 0;
    uint32_t width = 800;
    std::vector<obl::object::Base*> staff;
    obl::object::Base* selected = nullptr;
    obl::messages::Message posted{};
    bool hasPosted = false;
    static std::string Id(Widget* w) { return std::to_string(w->m_id); }
    void DisableIfAction(Widget* w, const char* a) { events.push_back("disable " + Id(w) + " " + a); }
    void ResetState(Widget* w, const char* a) { events.push_back("reset " + Id(w) + " " + a); }
    void SetBlink(Widget* w, const char* a, bool b) { events.push_back("blink " + Id(w) + " " + a + (b ? " 1" : " 0")); }
    void SetGraphic(Widget* w, const char* a, const char* s) { events.push_back("graphic " + Id(w) + " " + a + " " + s); }
    void SetFlashing(Widget* w, const char* a, bool f) { events.push_back("flash " + Id(w) + " " + a + (f ? " 1" : " 0")); }
    void SetVisible(Widget* w, const char* a, bool v) { events.push_back("visible " + Id(w) + " " + a + (v ? " 1" : " 0")); }
    bool BuildingInScenario(const char* key)
    {
        events.push_back(std::string("scenario ") + key);
        return inScenario;
    }
    bool WorldLoaded() { return worldLoaded; }
    int CountBuildings(obl::BuildingBehavior behavior)
    {
        events.push_back(std::string("count ") + obl::BuildingBehaviorName(behavior));
        return wharf;
    }
    uint32_t ScreenWidth() { return width; }
    bool BuildingHasFootprint(int id)
    {
        events.push_back("footprint " + std::to_string(id));
        return footprint;
    }
    void ShowDialog(Manager*, const char* name) { events.push_back(std::string("show ") + name); }
    void ClearStaffSelection() { events.push_back("clear"); }
    obl::object::Base* const* List(char cls, uint32_t& count)
    {
        events.push_back("list " + std::to_string(cls));
        count = static_cast<uint32_t>(staff.size());
        return staff.data();
    }
    void Select(obl::object::Base* o)
    {
        selected = o;
        events.push_back("select " + std::to_string(o->m_id));
    }
    void Post(const obl::messages::Message& m)
    {
        posted = m;
        hasPosted = true;
    }
};
// A widget list of one button (id 1), one non-button (id 2, type 3) and another button (id 3), the way the HUD mixes bars and buttons.
struct Hud {
    Manager manager{};
    Widget widgets[3]{};
    obl::ui::GuiManagerWidgetNode nodes[3]{};
    Hud()
    {
        for (int i = 0; i < 3; ++i) {
            widgets[i].m_id = i + 1;
            widgets[i].m_type = i == 1 ? 3 : kButtonWidgetType;
            nodes[i].m_widget = &widgets[i];
            nodes[i].m_next = i < 2 ? &nodes[i + 1] : nullptr;
        }
        View(&manager).m_widgets = &nodes[0];
    }
};
} // namespace

OBL_TEST_CASE("action button walks touch buttons only, in list order")
{
    Hud hud;
    Env env;
    DisableActionButtons(env, &hud.manager, "A");
    ResetActionButtons(env, &hud.manager, "B");
    SetActionButtonsBlink(env, &hud.manager, "C", true);
    FlashButtonsByAction(env, &hud.manager, "D", false);
    SetButtonGraphic(env, &hud.manager, "E", "sprite");
    const std::vector<std::string> expected = {"disable 1 A", "disable 3 A", "reset 1 B", "reset 3 B", "blink 1 C 1", "blink 3 C 1",
            "flash 1 D 0", "flash 3 D 0", "graphic 1 E sprite", "graphic 3 E sprite"};
    OBL_CHECK(env.events == expected);
    View(&hud.manager).m_widgets = nullptr;
    env.events.clear();
    DisableActionButtons(env, &hud.manager, "A");
    OBL_CHECK(env.events.empty());
}

OBL_TEST_CASE("staff buttons show the icon when staff is hired and the browser otherwise")
{
    Hud hud;
    Env env;
    UpdateStaffButtons(env, &hud.manager, 10, true);
    const std::vector<std::string> hired = {"visible 1 BuilderIcon 1", "visible 3 BuilderIcon 1", "visible 1 BuilderBrowser 0",
            "visible 3 BuilderBrowser 0", "scenario BUILDERYARD"};
    OBL_CHECK(env.events == hired);
    env.events.clear();
    UpdateStaffButtons(env, &hud.manager, 7, false);
    const std::vector<std::string> none = {"visible 1 LifeguardBrowser 1", "visible 3 LifeguardBrowser 1", "visible 1 LifeguardIcon 0",
            "visible 3 LifeguardIcon 0", "scenario BAYWATCH"};
    OBL_CHECK(env.events == none);
}

OBL_TEST_CASE("staff buttons of a building missing from the scenario are disabled, icon first")
{
    Hud hud;
    Env env;
    env.inScenario = false;
    UpdateStaffButtons(env, &hud.manager, 12, false);
    OBL_CHECK_EQ(env.events.size(), 9u);
    OBL_CHECK(env.events[5] == "disable 1 SecurityIcon");
    OBL_CHECK(env.events[7] == "disable 1 SecurityBrowser");
    env.events.clear();
    UpdateStaffButtons(env, &hud.manager, 6, true);
    UpdateStaffButtons(env, &hud.manager, 13, true);
    OBL_CHECK(env.events.empty());
    OBL_CHECK(StaffNames(9) && std::string(StaffNames(9)->building) == "REP");
}

OBL_TEST_CASE("ferry button graphic follows the wharf and the screen width")
{
    Hud hud;
    Env env;
    UpdateFerryButtonGraphic(env, &hud.manager);
    OBL_CHECK(env.events[1] == "graphic 1 FerryPanel UI3_bar_blob_coach");
    env.events.clear();
    env.width = 799;
    UpdateFerryButtonGraphic(env, &hud.manager);
    OBL_CHECK(env.events[1] == "graphic 1 FerryPanel UI2_bar_blob_coach");
    env.events.clear();
    env.wharf = 2;
    UpdateFerryButtonGraphic(env, &hud.manager);
    OBL_CHECK(env.events[1] == "graphic 1 FerryPanel UI2_bar_blob_ship");
    env.events.clear();
    env.width = 1024;
    UpdateFerryButtonGraphic(env, &hud.manager);
    OBL_CHECK(env.events[1] == "graphic 1 FerryPanel UI3_bar_blob_ship");
    env.events.clear();
    env.worldLoaded = false;
    UpdateFerryButtonGraphic(env, &hud.manager);
    OBL_CHECK(env.events.empty());
}

OBL_TEST_CASE("cancel build mode asks for mode 5 only for a building with a footprint")
{
    Hud hud;
    Env env;
    View(&hud.manager).m_context[0] = 7;
    View(&hud.manager).m_requestedMode = 2;
    ShowCancelBuildMode(env, &hud.manager);
    OBL_CHECK_EQ(View(&hud.manager).m_requestedMode, 2);
    env.footprint = true;
    ShowCancelBuildMode(env, &hud.manager);
    OBL_CHECK_EQ(View(&hud.manager).m_requestedMode, 5);
    OBL_CHECK(env.events.back() == "show CancelBuildMode");
    OBL_CHECK(env.events[0] == "footprint 7");
}

OBL_TEST_CASE("ToggleSelection clears an already selected object")
{
    Hud hud;
    ToggleSelection(&hud.manager, 5);
    OBL_CHECK_EQ(View(&hud.manager).m_selection, 5);
    ToggleSelection(&hud.manager, 5);
    OBL_CHECK_EQ(View(&hud.manager).m_selection, -1);
    ToggleSelection(&hud.manager, 5);
    ToggleSelection(&hud.manager, 9);
    OBL_CHECK_EQ(View(&hud.manager).m_selection, 9);
    ToggleSelection(&hud.manager, -1);
    OBL_CHECK_EQ(View(&hud.manager).m_selection, -1);
}

OBL_TEST_CASE("staff cycling prefers the last member needing attention, else rotates and wraps")
{
    obl::object::Base a{}, b{}, c{};
    a.m_id = 10;
    b.m_id = 11;
    c.m_id = 12;
    Env env;
    env.staff = {&a, &b, &c};
    uint32_t index = 0;
    OBL_CHECK_EQ(SelectStaffByClass(env, 10, &index), 11u);
    OBL_CHECK_EQ(index, 1u);
    OBL_CHECK_EQ(SelectStaffByClass(env, 10, &index), 12u);
    OBL_CHECK_EQ(SelectStaffByClass(env, 10, &index), 10u);
    OBL_CHECK_EQ(index, 0u);
    OBL_CHECK(env.events[0] == "clear");
    OBL_CHECK(env.events[1] == "list 10");
    OBL_CHECK_EQ(env.posted.sender, 10);
    OBL_CHECK_EQ(env.posted.receiver, 10);
    OBL_CHECK_EQ(env.posted.type, 0x65);
    OBL_CHECK_EQ(env.posted.data, 0);
    // The attention flag overrides the rotation, the last flagged member wins and the index follows it.
    a.m_requiresAttention = 1;
    b.m_requiresAttention = 1;
    index = 2;
    OBL_CHECK_EQ(SelectStaffByClass(env, 10, &index), 11u);
    OBL_CHECK_EQ(index, 1u);
    OBL_CHECK(env.selected == &b);
    // A stale index past the end wraps without reading beyond the list.
    a.m_requiresAttention = b.m_requiresAttention = 0;
    index = 40;
    OBL_CHECK_EQ(SelectStaffByClass(env, 10, &index), 10u);
    OBL_CHECK_EQ(index, 0u);
}

OBL_TEST_CASE("staff cycling of an empty class does nothing")
{
    Env env;
    uint32_t index = 3;
    OBL_CHECK_EQ(SelectStaffByClass(env, 8, &index), 0u);
    OBL_CHECK(!env.hasPosted);
    OBL_CHECK(env.selected == nullptr);
    OBL_CHECK_EQ(index, 3u);
}
