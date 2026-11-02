#include "unit_test.h"
#include "game/ui/gui_manager_dialogs.h"
#include <vector>
#include <string>
using namespace obl::gui_manager_dialogs;
namespace {
struct Env {
    std::vector<std::string> events;
    Manager* manager;
    void ClearSurface(Manager*, int) { events.push_back("clear"); }
    void Invalidate(obl::ui::GuiDialogData* d) { events.push_back("I" + std::to_string(d->m_id)); }
    void Refresh(obl::ui::GuiDialogData* d)
    {
        OBL_CHECK(manager->m_variableProvider == d);
        events.push_back("R" + std::to_string(d->m_id));
    }
    void InvalidateByName(Manager* m, const char* name, uint8_t clear) { InvalidateDialog(*this, m, name, clear); }
};
} // namespace
OBL_TEST_CASE("GuiManager list queries keep pending entries and choose the first duplicate")
{
    Manager manager{};
    Node head{}, first{}, second{};
    obl::ui::GuiDialogData a{}, b{};
    a.m_name = b.m_name = const_cast<char*>("ObjectBrowser");
    a.m_minimized = -4;
    b.m_minimized = 9;
    obl::ui::GuiManagerDialogEntry ea{&a, 0, 2, &a}, eb{&b, 0, 1, &b};
    head.m_next = &first;
    first.m_next = &second;
    second.m_next = &head;
    first.m_entry = &ea;
    second.m_entry = &eb;
    View(&manager).m_dialogs = &head;
    OBL_CHECK(IsDialogOpen(&manager, "ObjectBrowser"));
    OBL_CHECK(!IsDialogOpen(&manager, "objectbrowser"));
    OBL_CHECK_EQ(GetDialogMinimized(&manager, "ObjectBrowser"), -4);
    SetDialogMinimized(&manager, "ObjectBrowser", 12);
    OBL_CHECK_EQ(a.m_minimized, 12);
    OBL_CHECK_EQ(b.m_minimized, 9);
    Env env{{}, &manager};
    InvalidateDialog(env, &manager, "ObjectBrowser", 1);
    OBL_CHECK(env.events.empty());
    InvalidateAllDialogs(env, &manager, 0);
    OBL_CHECK_EQ(env.events.size(), 2u);
    head.m_next = &head;
    OBL_CHECK(!IsDialogOpen(&manager, "ObjectBrowser"));
    OBL_CHECK_EQ(GetDialogMinimized(&manager, "Missing"), 0);
}
OBL_TEST_CASE("GuiManager context restores provider and distinguishes unchanged indices")
{
    Manager manager{};
    Node head{}, first{};
    obl::ui::GuiDialogData dialog{};
    dialog.m_name = const_cast<char*>("ObjectBrowser");
    dialog.m_id = 3;
    obl::ui::GuiManagerDialogEntry entry{&dialog, 0, 1, &dialog};
    head.m_next = &first;
    first.m_next = &head;
    first.m_entry = &entry;
    View(&manager).m_dialogs = &head;
    manager.m_variableProvider = &head;
    Env env{{}, &manager};
    SetContext(env, &manager, 1, 7);
    OBL_CHECK((env.events == std::vector<std::string>{"R3", "clear", "I3"}));
    OBL_CHECK(manager.m_variableProvider == &head);
    env.events.clear();
    SetContext(env, &manager, 1, 7);
    OBL_CHECK(env.events.empty());
    SetContext(env, &manager, 7, 9);
    OBL_CHECK(env.events.empty());
    SetContext(env, &manager, 0, 0);
    OBL_CHECK((env.events == std::vector<std::string>{"I3"}));
    env.events.clear();
    SetContext(env, &manager, 6, 1);
    OBL_CHECK((env.events == std::vector<std::string>{"I3"}));
}
namespace {
using obl::ui::GuiDialogData;
using obl::ui::GuiManagerDialogEntry;
struct OpenEnv {
    std::vector<std::string> events;
    std::vector<Node*> nodes;
    std::vector<GuiManagerDialogEntry*> entries;
    bool hasPosition{};
    int savedX{}, savedY{};
    uint32_t screenWidth{1024}, screenHeight{768};
    void StorePosition(int x, int y, int key)
    {
        events.push_back("P" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(key));
    }
    bool FindPosition(int, int* x, int* y)
    {
        *x = savedX;
        *y = savedY;
        return hasPosition;
    }
    void ProviderClosed(void* provider) { events.push_back("C" + std::to_string(*static_cast<int*>(provider))); }
    GuiManagerDialogEntry* NewEntry()
    {
        entries.push_back(new GuiManagerDialogEntry{});
        return entries.back();
    }
    void ScreenSize(uint32_t& w, uint32_t& h)
    {
        w = screenWidth;
        h = screenHeight;
    }
    void Translate(GuiDialogData* d, int dx, int dy)
    {
        events.push_back("T" + std::to_string(dx) + "," + std::to_string(dy));
        d->m_x0 += dx;
        d->m_y0 += dy;
        d->m_x1 += dx;
        d->m_y1 += dy;
    }
    int CreateSurface(Manager*, int w, int h)
    {
        events.push_back("S" + std::to_string(w) + "x" + std::to_string(h));
        return 5;
    }
    void ClearSurface(Manager*, int) { events.push_back("clear"); }
    void SetSurfaceOrigin(Manager*, int, int x, int y) { events.push_back("O" + std::to_string(x) + "," + std::to_string(y)); }
    void PrependNode(Manager* m, GuiManagerDialogEntry* entry)
    {
        Node* node = new Node{};
        nodes.push_back(node);
        node->m_entry = entry;
        Node* head = View(m).m_dialogs;
        node->m_next = head->m_next;
        node->m_previous = head;
        head->m_next->m_previous = node;
        head->m_next = node;
    }
    void Invalidate(GuiDialogData*) { events.push_back("I"); }
    void PrepareOpen(GuiDialogData*) { events.push_back("Prep"); }
    void SelectTab(GuiDialogData*, int tab) { events.push_back("Tab" + std::to_string(tab)); }
    ~OpenEnv()
    {
        for (auto* n : nodes) delete n;
        for (auto* e : entries) delete e;
    }
};
struct Fixture {
    Manager manager{};
    Node head{};
    GuiDialogData dialog{};
    obl::ui::GuiManagerDefinitionNode definition{};
    int providerValue{7};
    Fixture()
    {
        head.m_next = head.m_previous = &head;
        View(&manager).m_dialogs = &head;
        View(&manager).m_definitions = &definition;
        dialog.m_name = const_cast<char*>("Panel");
        dialog.m_id = 4;
        dialog.m_positionKey = 3;
        dialog.m_x0 = 0;
        dialog.m_y0 = 0;
        dialog.m_x1 = 100;
        dialog.m_y1 = 50;
        dialog.m_movable = 1;
        definition.m_dialog = &dialog;
    }
};
} // namespace
OBL_TEST_CASE("GuiManager ShowDialog opens once per provider and replaces the same position key")
{
    Fixture f;
    OpenEnv env;
    OBL_CHECK(!ShowDialog(env, &f.manager, "Missing", nullptr, false));
    OBL_CHECK(ShowDialog(env, &f.manager, "Panel", &f.providerValue, true));
    OBL_CHECK_EQ(GetDialogCount(&f.manager), 1);
    OBL_CHECK_EQ(f.dialog.m_modal, 1);
    OBL_CHECK((env.events == std::vector<std::string>{"S100x50", "clear", "O0,0", "I", "Prep"}));
    env.events.clear();
    OBL_CHECK(ShowDialog(env, &f.manager, "Panel", &f.providerValue, false)); // duplicate dialog+provider: nothing happens
    OBL_CHECK(env.events.empty());
    OBL_CHECK_EQ(GetDialogCount(&f.manager), 1);
    OBL_CHECK(ShowDialog(env, &f.manager, "Panel", nullptr, false)); // other provider: the old window with the same key is told to close
    OBL_CHECK_EQ(GetDialogCount(&f.manager), 2);
    OBL_CHECK_EQ(env.entries[0]->m_state, 2);
    OBL_CHECK_EQ(env.events[0], "C7");
}
OBL_TEST_CASE("GuiManager ShowDialog restores a remembered position only inside the screen")
{
    Fixture f;
    OpenEnv env;
    env.hasPosition = true;
    env.savedX = 30;
    env.savedY = 20;
    ShowDialog(env, &f.manager, "Panel", nullptr, false);
    OBL_CHECK_EQ(env.events[0], "T30,20");
    OBL_CHECK_EQ(env.events[3], "O30,20");
    Fixture g;
    OpenEnv far;
    far.hasPosition = true;
    far.savedX = 1000;
    far.savedY = 0;
    ShowDialog(far, &g.manager, "Panel", nullptr, false);
    OBL_CHECK_EQ(far.events[0], "S100x50"); // 1100 does not fit 1024
    Fixture h;
    OpenEnv modal;
    modal.hasPosition = true;
    modal.savedX = 5;
    ShowDialog(modal, &h.manager, "Panel", nullptr, true);
    OBL_CHECK_EQ(modal.events[0], "S100x50");
    Fixture k;
    OpenEnv camera;
    camera.hasPosition = true;
    View(&k.manager).m_mode = 6;
    ShowDialog(camera, &k.manager, "Panel", nullptr, false);
    OBL_CHECK_EQ(camera.events[0], "S100x50");
}
OBL_TEST_CASE("GuiManager close paths mark pending close, remember the position and call the provider")
{
    Fixture f;
    OpenEnv env;
    ShowDialog(env, &f.manager, "Panel", &f.providerValue, false);
    env.events.clear();
    f.dialog.m_x0 = 8;
    f.dialog.m_y0 = 9;
    OBL_CHECK(CloseDialogByProvider(env, &f.manager, &f.providerValue));
    OBL_CHECK_EQ(env.entries[0]->m_state, 2);
    OBL_CHECK((env.events == std::vector<std::string>{"P8,9,3", "C7"}));
    env.events.clear();
    OBL_CHECK(!CloseDialogByProvider(env, &f.manager, nullptr)); // no provider-less entry here
    OBL_CHECK(env.events.empty());
    OBL_CHECK(!CloseDialog(env, &f.manager, "Other"));
}
OBL_TEST_CASE("GuiManager CloseDialogById closes every entry when the id is unrecognized")
{
    Fixture f;
    OpenEnv env;
    ShowDialog(env, &f.manager, "Panel", &f.providerValue, false);
    ShowDialog(env, &f.manager, "Panel", nullptr, false);
    env.events.clear();
    OBL_CHECK(!CloseDialogById(env, &f.manager, 99));
    OBL_CHECK_EQ(env.entries[0]->m_state, 2);
    OBL_CHECK_EQ(env.entries[1]->m_state, 2);
    env.entries[0]->m_state = 1;
    env.entries[1]->m_state = 1;
    env.events.clear();
    OBL_CHECK(CloseDialogById(env, &f.manager, 4)); // the first match only, which is the newest window
    OBL_CHECK_EQ(env.entries[1]->m_state, 2);
    OBL_CHECK_EQ(env.entries[0]->m_state, 1);
}
OBL_TEST_CASE("GuiManager CloseFirstDialog skips modal, animating and pending entries")
{
    Fixture f;
    OpenEnv env;
    ShowDialog(env, &f.manager, "Panel", &f.providerValue, false);
    env.events.clear();
    auto* entry = env.entries[0];
    OBL_CHECK_EQ(entry->m_state, 0);
    CloseFirstDialog(env, &f.manager);
    OBL_CHECK_EQ(entry->m_state, 0);
    entry->m_state = 1;
    f.dialog.m_animating = 1;
    CloseFirstDialog(env, &f.manager);
    OBL_CHECK_EQ(entry->m_state, 1);
    f.dialog.m_animating = 0;
    f.dialog.m_modal = 1;
    OBL_CHECK(IsBlockingDialogOpen(&f.manager));
    CloseFirstDialog(env, &f.manager);
    OBL_CHECK_EQ(entry->m_state, 1);
    f.dialog.m_modal = 0;
    OBL_CHECK(!IsBlockingDialogOpen(&f.manager));
    CloseFirstDialog(env, &f.manager);
    OBL_CHECK_EQ(entry->m_state, 2);
    OBL_CHECK((env.events == std::vector<std::string>{"C7"}));
}
OBL_TEST_CASE("GuiManager ToggleDialog and ShowDialogWithText")
{
    Fixture f;
    OpenEnv env;
    OBL_CHECK(ToggleDialog(env, &f.manager, "Panel", nullptr, false));
    OBL_CHECK(!ToggleDialog(env, &f.manager, "Panel", nullptr, false));
    OBL_CHECK_EQ(env.entries.size(), 1u);
    ShowDialogWithText(env, &f.manager, "Panel", "KEY", nullptr, false);
    OBL_CHECK_EQ(std::string(f.manager.m_scriptTextKey), "KEY");
    SelectDialogTab(env, &f.manager, "Panel", 3);
    OBL_CHECK_EQ(env.events.back(), "Tab3");
}
namespace {
struct PanelEnv : OpenEnv {
    void StorePosition(int, int, int) {} // the positions are covered by the close tests above
    void PlayRefusal() { events.push_back("bad"); }
    bool CloseDialog(Manager* m, const char* name)
    {
        events.push_back(std::string("close:") + name);
        return ::CloseDialog(*this, m, name);
    }
    void ShowDialog(Manager* m, const char* name, void* provider, bool modal)
    {
        size_t before = events.size();
        ::ShowDialog(*this, m, name, provider, modal);
        events.resize(before); // the nested window set-up is covered by the ShowDialog tests
        events.push_back(std::string("show:") + name + (modal ? ":modal" : ""));
    }
    void ScanSaveSlots() { events.push_back("scan"); }
    void FillLoadList() { events.push_back("fillLoad"); }
    void FillSaveList() { events.push_back("fillSave"); }
    void MusicPanelVisibilityChanged(void*, bool visible) { events.push_back(visible ? "music:on" : "music:off"); }
    void SetButtonGraphic(obl::ui::GuiWidget*, const char* action, const char* sprite)
    {
        events.push_back(std::string("skin:") + action + ":" + sprite);
    }
    void Animate(GuiDialogData*, int type, float speed, int delay)
    {
        events.push_back("anim" + std::to_string(type) + ":" + std::to_string(speed) + ":" + std::to_string(delay));
    }
};
struct Rig {
    Manager manager{};
    Node head{};
    std::vector<GuiDialogData> dialogs;
    std::vector<obl::ui::GuiManagerDefinitionNode> definitions;
    obl::ui::GuiManagerWidgetNode buttonA{}, buttonB{};
    obl::ui::GuiWidget widgetA{}, widgetB{};
    Rig(std::initializer_list<const char*> names)
    {
        head.m_next = head.m_previous = &head;
        View(&manager).m_dialogs = &head;
        dialogs.resize(names.size());
        definitions.resize(names.size());
        size_t i = 0;
        for (const char* name : names) {
            dialogs[i].m_name = const_cast<char*>(name);
            dialogs[i].m_id = static_cast<int>(i);
            dialogs[i].m_positionKey = static_cast<int>(i);
            dialogs[i].m_x1 = 10;
            dialogs[i].m_y1 = 10;
            definitions[i].m_dialog = &dialogs[i];
            definitions[i].m_next = i + 1 < names.size() ? &definitions[i + 1] : nullptr;
            ++i;
        }
        View(&manager).m_definitions = definitions.data();
        widgetA.m_type = 0;
        widgetB.m_type = 3;
        buttonA.m_widget = &widgetA;
        buttonA.m_next = &buttonB;
        buttonB.m_widget = &widgetB;
        View(&manager).m_widgets = &buttonA;
    }
};
} // namespace
OBL_TEST_CASE("GuiManager panels refuse in mode 2 or behind an active modal dialog")
{
    Rig rig{"LoadGameMenu", "SaveGameMenu", "InGameOptions", "MP3Panel", "BuildPanel", "SceneryPanel", "PauseBox"};
    PanelEnv env;
    View(&rig.manager).m_mode = 2;
    ToggleLoadGameMenu(env, &rig.manager);
    ToggleBuildPanel(env, &rig.manager);
    OBL_CHECK((env.events == std::vector<std::string>{"bad", "bad"}));
    OBL_CHECK_EQ(View(&rig.manager).m_requestedMode, 0);
    View(&rig.manager).m_mode = 0;
    env.events.clear();
    ShowDialog(env, &rig.manager, "PauseBox", nullptr, true);
    env.entries[0]->m_state = 1;
    env.events.clear();
    ToggleMusicPanel(env, &rig.manager);
    ToggleSceneryPanel(env, &rig.manager);
    OBL_CHECK((env.events == std::vector<std::string>{"bad", "bad"}));
}
OBL_TEST_CASE("GuiManager load and save menus replace each other and refill their list")
{
    Rig rig{"LoadGameMenu", "SaveGameMenu"};
    PanelEnv env;
    ToggleLoadGameMenu(env, &rig.manager);
    OBL_CHECK((env.events == std::vector<std::string>{"close:SaveGameMenu", "scan", "show:LoadGameMenu", "fillLoad"}));
    env.events.clear();
    ToggleLoadGameMenu(env, &rig.manager);
    OBL_CHECK((env.events == std::vector<std::string>{"close:LoadGameMenu"}));
    env.events.clear();
    ToggleSaveGameMenu(env, &rig.manager);
    OBL_CHECK((env.events == std::vector<std::string>{"close:LoadGameMenu", "scan", "show:SaveGameMenu", "fillSave"}));
}
OBL_TEST_CASE("GuiManager options and music panels")
{
    Rig rig{"InGameOptions", "MP3Panel"};
    PanelEnv env;
    ToggleInGameOptions(env, &rig.manager);
    OBL_CHECK_EQ(env.events[0], "show:InGameOptions");
    OBL_CHECK_EQ(env.events.back(), "Tab0");
    env.events.clear();
    ToggleInGameOptions(env, &rig.manager);
    OBL_CHECK((env.events == std::vector<std::string>{"close:InGameOptions"}));
    env.events.clear();
    ToggleMusicPanel(env, &rig.manager);
    OBL_CHECK((env.events == std::vector<std::string>{"close:MP3Panel", "show:MP3Panel", "music:on"}));
    env.events.clear();
    ToggleMusicPanel(env, &rig.manager);
    OBL_CHECK((env.events == std::vector<std::string>{"close:MP3Panel", "music:off"}));
}
OBL_TEST_CASE("GuiManager build and scenery panels")
{
    Rig rig{"BuildPanel", "SceneryPanel", "Other"};
    PanelEnv env;
    ShowDialog(env, &rig.manager, "Other", nullptr, false);
    env.events.clear();
    ToggleBuildPanel(env, &rig.manager);
    OBL_CHECK_EQ(View(&rig.manager).m_requestedMode, 4);
    OBL_CHECK_EQ(env.entries[0]->m_state, 2); // every open dialog was asked to close first

    OBL_CHECK_EQ(env.events[0], "close:BuildPanel");
    env.events.clear();
    View(&rig.manager).m_requestedMode = 0;
    ToggleSceneryPanel(env, &rig.manager);
    OBL_CHECK_EQ(env.events[0], "close:BuildPanel");
    OBL_CHECK_EQ(env.events[1], "skin:MinSceneryPanel:UI3_sc_in"); // only the type 0 button
    OBL_CHECK_EQ(env.events[2], "show:SceneryPanel");
    OBL_CHECK_EQ(env.events.back(), "anim1:1.000000:0");
    OBL_CHECK_EQ(View(&rig.manager).m_requestedMode, 0);
    env.events.clear();
    ToggleSceneryPanel(env, &rig.manager); // already open: slide it out and request mode 4
    OBL_CHECK((env.events == std::vector<std::string>{"anim2:1.000000:0"}));
    OBL_CHECK_EQ(View(&rig.manager).m_requestedMode, 4);
}
OBL_TEST_CASE("GuiManager pause box opens unless the scenario ended")
{
    Rig rig{"PauseBox", "ScenarioWon"};
    PanelEnv env;
    ShowPauseUnlessScenarioEnded(env, &rig.manager);
    OBL_CHECK_EQ(env.events[0], "show:PauseBox:modal");
    env.events.clear();
    ClosePauseDialog(env, &rig.manager);
    OBL_CHECK((env.events == std::vector<std::string>{"close:PauseBox"}));
    Rig ended{"PauseBox", "ScenarioWon"};
    PanelEnv other;
    ShowDialog(other, &ended.manager, "ScenarioWon", nullptr, false);
    other.events.clear();
    ShowPauseUnlessScenarioEnded(other, &ended.manager);
    OBL_CHECK(other.events.empty());
}
namespace {
struct UpdateEnv {
    std::vector<std::string> events;
    Manager* manager;
    void SetDialogOpen(obl::ui::GuiDialogData* d, bool open) { events.push_back((open ? "open" : "close") + std::to_string(d->m_id)); }
    void UpdateDialog(obl::ui::GuiDialogData* d, float) { events.push_back("update" + std::to_string(d->m_id)); }
    void ReleaseSurface(int surface) { events.push_back("release" + std::to_string(surface)); }
    void DeleteEntry(obl::ui::GuiManagerDialogEntry*) { events.push_back("delete"); }
    void EraseNode(Node* node)
    {
        node->m_previous->m_next = node->m_next;
        node->m_next->m_previous = node->m_previous;
        events.push_back("erase");
    }
};
} // namespace
OBL_TEST_CASE("GuiManager UpdateDialogs removes closing entries, opens pending ones, then updates active ones")
{
    Manager manager{};
    Node head{}, n1{}, n2{}, n3{};
    obl::ui::GuiDialogData d1{}, d2{}, d3{};
    d1.m_id = 1;
    d2.m_id = 2;
    d3.m_id = 3;
    int provider2 = 0, provider3 = 0;
    obl::ui::GuiManagerDialogEntry e1{&d1, 7, 2, nullptr}, e2{&d2, 8, 0, &provider2}, e3{&d3, 9, 1, &provider3};
    Node* nodes[] = {&head, &n1, &n2, &n3};
    for (int i = 0; i < 4; ++i) {
        nodes[i]->m_next = nodes[(i + 1) % 4];
        nodes[i]->m_previous = nodes[(i + 3) % 4];
    }
    n1.m_entry = &e1;
    n2.m_entry = &e2;
    n3.m_entry = &e3;
    View(&manager).m_dialogs = &head;
    UpdateEnv env{{}, &manager};
    UpdateDialogs(env, &manager, 0.5f);
    std::vector<std::string> expected = {"close1", "close1", "release7", "delete", "erase", "open2", "update2", "update3"};
    OBL_CHECK(env.events == expected);
    OBL_CHECK_EQ(e2.m_state, 1);
    OBL_CHECK(head.m_next == &n2);
    OBL_CHECK(manager.m_variableProvider == &provider3);
}
OBL_TEST_CASE("GuiManager FindListBox matches only list boxes and ShowMessageBox stores the key first")
{
    Manager manager{};
    obl::ui::GuiListBoxData box{}, other{};
    char name[] = "Box";
    box.m_type = kListBoxType;
    box.m_name = name;
    other.m_type = 5;
    other.m_name = name;
    obl::ui::GuiManagerWidgetNode second{nullptr, &box}, first{&second, &other};
    View(&manager).m_widgets = &first;
    OBL_CHECK(FindListBox(&manager, "Box") == &box);
    OBL_CHECK(FindListBox(&manager, "box") == nullptr);
    struct ShowEnv {
        const char* seen{};
        void ShowDialog(Manager* m, const char*, void*, bool) { seen = m->m_messageTextKey; }
    } env;
    ShowMessageBox(env, &manager, "MessageOKBox", "Key", nullptr, true);
    OBL_CHECK(std::string(env.seen) == "Key");
}
