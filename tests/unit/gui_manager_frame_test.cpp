#include "unit_test.h"
#include <new>
#include "game/ui/gui_manager_frame.h"
#include "game/ui/gui_modes_types.h"

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace obl::gui_manager_frame;
using obl::ui::GuiManagerDialogEntry;
using obl::ui::Renderer;

namespace {
// A manager, a renderer and up to four dialogs in plain memory; the env records what the logic asks the game to do.
struct FakeMode : obl::ui::GuiMode {
    FakeMode() : GuiMode(nullptr) {}
    void Update(float) override {}
    bool OnMouseWheel(int, int, int) override { return false; }
    bool OnMouse(int, int, int, int) override { return false; }
};
FakeMode g_newMode;

struct Fixture {
    alignas(View) std::array<uint8_t, sizeof(View)> manager{};
    Renderer renderer{};
    std::array<Dialog, 4> dialogs{};
    std::array<GuiManagerDialogEntry, 4> entries{};
    std::array<Node, 5> nodes{};
    View* view = reinterpret_cast<View*>(manager.data());
    Renderer* render = &renderer;
    Fixture()
    {
        view->m_renderer = render;
        nodes[4].m_next = nodes[4].m_previous = &nodes[4];
        view->m_dialogs = &nodes[4];
    }
    Dialog* D(int i) { return &dialogs[i]; }
    void Add(int i, int state, bool modal, const char* name = "Dialog")
    {
        D(i)->m_name = const_cast<char*>(name);
        D(i)->m_modal = modal;
        entries[i] = {D(i), 0, state, reinterpret_cast<void*>(static_cast<uintptr_t>(0x100 + i))};
        Node* head = &nodes[4];
        nodes[i] = {head, head->m_previous, &entries[i]};
        head->m_previous->m_next = &nodes[i];
        head->m_previous = &nodes[i];
    }
};

struct Env {
    Fixture& f;
    std::vector<std::string> log;
    bool hovered[4]{}, takes[4]{}, mainHovered{}, mainTakes{};
    int x = 50, y = 60, wheel = 0, left = 1, right = 0;
    int Index(Dialog* d)
    {
        for (int i = 0; i < 4; ++i)
            if (f.D(i) == d) return i;
        return -1;
    }
    bool DialogHovered(Dialog* d)
    {
        log.push_back("H" + std::to_string(Index(d)));
        return hovered[Index(d)];
    }
    bool DialogWheel(Dialog* d, int, int, int w)
    {
        log.push_back("W" + std::to_string(Index(d)) + ":" + std::to_string(w));
        return false;
    }
    bool DialogMouse(Dialog* d, int, int, int s)
    {
        log.push_back("M" + std::to_string(Index(d)) + ":" + std::to_string(s));
        return takes[Index(d)];
    }
    void SelectTab(Dialog* d, int tab) { log.push_back("tab" + std::to_string(Index(d)) + ":" + std::to_string(tab)); }
    bool WidgetHovered(Widget*)
    {
        log.push_back("mainH");
        return mainHovered;
    }
    bool WidgetMouseLeft(Widget*, int, int, int s)
    {
        log.push_back("mainL:" + std::to_string(s));
        return mainTakes;
    }
    void WidgetMouseRight(Widget*, int, int, int s) { log.push_back("mainR:" + std::to_string(s)); }
    void WidgetMouse(Widget*, int, int, int s) { log.push_back("widget:" + std::to_string(s)); }
    bool CursorInMapView() { return true; }
    int CursorX() { return x; }
    int CursorY() { return y; }
    int Wheel() { return wheel; }
    int LeftButton() { return left; }
    int RightButton() { return right; }
    void RadarMouse(View*, int, int, int s) { log.push_back("radar:" + std::to_string(s)); }
    void ProgressBarMouse(View*, int, int, int s) { log.push_back("progress:" + std::to_string(s)); }
    void ModeWheel(void*, int, int, int) { log.push_back("modeW"); }
    void ModeMouse(void*, int, int, int l, int r) { log.push_back("modeM:" + std::to_string(l) + "," + std::to_string(r)); }
    void MoveDialogToFront(View* frame, Node* node)
    {
        log.push_back("front" + std::to_string(Index(node->m_entry->m_dialog)));
        node->m_previous->m_next = node->m_next;
        node->m_next->m_previous = node->m_previous;
        Node* head = frame->m_dialogs;
        node->m_next = head->m_next;
        node->m_previous = head;
        head->m_next->m_previous = node;
        head->m_next = node;
    }
    void ModeExit(void*) { log.push_back("exit"); }
    void ModeDelete(void*) { log.push_back("delete"); }
    void ModeEnter(void*) { log.push_back("enter"); }
    obl::ui::GuiMode* NewMode(View*, int mode)
    {
        log.push_back("new" + std::to_string(mode));
        return &g_newMode;
    }
    void SetGuiBarHeight(void*, int height) { log.push_back("bar" + std::to_string(height)); }
    void SyncBarSurfaces(View*) { log.push_back("sync"); }
    void Camera(int32_t* cx, int32_t* cy)
    {
        *cx = 100;
        *cy = 51;
    }
    void Neighbour(uint32_t tx, uint32_t ty, int direction, uint32_t* out)
    {
        out[0] = tx + (direction == 4 ? 0 : 1);
        out[1] = ty + (direction == 2 ? 0 : 1);
    }
    void TileToScreen(const uint32_t* tile, int32_t* out)
    {
        out[0] = static_cast<int32_t>(tile[0]) * 64;
        out[1] = static_cast<int32_t>(tile[1]) * 32;
    }
    void SetMoving(void*, bool moving) { log.push_back(moving ? "moving" : "still"); }
    void DrawLine(void*, int x0, int y0, int x1, int y1, uint32_t)
    {
        log.push_back(std::to_string(x0) + "," + std::to_string(y0) + "-" + std::to_string(x1) + "," + std::to_string(y1));
    }
};
} // namespace

OBL_TEST_CASE("UpdateMouse forces state 3 below a hovered dialog and moves the taker to the front")
{
    Fixture f;
    f.Add(0, 1, false);
    f.Add(1, 2, false); // closing: skipped
    f.Add(2, 1, false);
    f.Add(3, 1, false);
    Env env{f};
    env.hovered[0] = true;
    env.takes[2] = true;
    env.right = 2;
    FakeMode mode;
    f.view->m_modeObject = &mode;
    f.view->m_mode = 4;
    UpdateMouse(env, f.view);
    const std::vector<std::string> expected = {"M0:1", "H0", "M2:3", "H2", "front2", "radar:3", "progress:3"};
    OBL_CHECK(env.log == expected);
    OBL_CHECK(f.view->m_dialogs->m_next == &f.nodes[2]);
    OBL_CHECK(f.view->m_variableProvider == f.entries[2].m_provider);
}

OBL_TEST_CASE("UpdateMouse keeps the order while any dialog is modal and gives the rest to the HUD and the mode")
{
    Fixture f;
    f.Add(0, 1, false);
    f.Add(1, 0, true); // not active, but modal: still blocks the reordering
    Env env{f};
    env.wheel = 120;
    FakeMode mode;
    f.view->m_modeObject = &mode;
    f.view->m_mode = 4;
    f.view->m_mainItem = reinterpret_cast<Widget*>(&mode);
    UpdateMouse(env, f.view);
    const std::vector<std::string> expected = {"W0:120", "modeW", "M0:1", "H0", "mainL:1", "mainR:0", "radar:1", "progress:1", "modeM:1,0"};
    OBL_CHECK(env.log == expected);

    Env taken{f};
    taken.takes[0] = true;
    f.Add(2, 1, false);
    UpdateMouse(taken, f.view);
    // The front dialog took the click: no reordering, no left click for the HUD or the mode, but the HUD still gets the right button.
    OBL_CHECK((taken.log == std::vector<std::string>{"M0:1", "H0", "mainR:0", "radar:1", "progress:1"}));
}

OBL_TEST_CASE("SwitchMode replaces the mode object; mode 3 has none and mode 6 drops the bar")
{
    Fixture f;
    FakeMode old;
    f.view->m_modeObject = &old;
    f.view->m_mode = 4;
    f.view->m_requestedMode = 3;
    Env env{f};
    SwitchMode(env, f.view);
    OBL_CHECK((env.log == std::vector<std::string>{"exit", "delete", "sync"}));
    OBL_CHECK(f.view->m_modeObject == nullptr);
    OBL_CHECK(f.view->m_mode == 3);

    Env none{f};
    f.view->m_requestedMode = 6;
    SwitchMode(none, f.view);
    OBL_CHECK((none.log == std::vector<std::string>{"new6", "bar0", "enter", "sync"}));

    Env same{f};
    SwitchMode(same, f.view);
    OBL_CHECK(same.log.empty());
}

OBL_TEST_CASE("SwitchMode builds the object of the current mode when it has none")
{
    // The gui manager constructor leaves mode 4 without its object; a save loaded before the first switch requests mode 4 again.
    Fixture f;
    f.view->m_modeObject = nullptr;
    f.view->m_mode = 4;
    f.view->m_requestedMode = 4;
    Env env{f};
    SwitchMode(env, f.view);
    OBL_CHECK((env.log == std::vector<std::string>{"new4", "enter", "sync"}));
    OBL_CHECK(f.view->m_mode == 4);

    // Mode 3 has no object: nothing to build.
    f.view->m_modeObject = nullptr;
    f.view->m_mode = 3;
    f.view->m_requestedMode = 3;
    Env none{f};
    SwitchMode(none, f.view);
    OBL_CHECK(none.log.empty());
}

OBL_TEST_CASE("Tile outline subtracts the camera and halves at zoom level 2 toward zero")
{
    Fixture f;
    Env env{f};
    f.render->m_zoomLevel = 2;
    DrawTileOutline(env, f.view, 1, 1, 0);
    // corner (64,32), dir4 (64,64), dir2 (128,32), dir3 (128,64); camera (100,51): (-36,-19) / 2 = (-18,-9)
    const std::vector<std::string> expected = {"still", "-18,-9--18,6", "-18,-9-14,-9", "14,6--18,6", "14,6-14,-9", "moving"};
    OBL_CHECK(env.log == expected);

    Env far{f};
    f.render->m_zoomLevel = 3;
    DrawTileOutline(far, f.view, 1, 1, 0);
    OBL_CHECK((far.log == std::vector<std::string>{"still", "moving"}));
}

OBL_TEST_CASE("Queries: cursor over GUI, camera input, resolution request, follow offset")
{
    Fixture f;
    f.Add(0, 2, false);
    Env env{f};
    env.hovered[0] = true;
    OBL_CHECK(!IsCursorOverGui(env, f.view, true)); // a closing dialog does not count
    f.view->m_radarHovered = 1;
    OBL_CHECK(!IsCursorOverGui(env, f.view, false));
    OBL_CHECK(IsCursorOverGui(env, f.view, true));
    f.view->m_progressHovered = 1;
    OBL_CHECK(IsCursorOverGui(env, f.view, false));

    f.view->m_mode = 4;
    OBL_CHECK(CanUseWorldCameraInput(env, f.view));
    f.view->m_mode = 5;
    OBL_CHECK(!CanUseWorldCameraInput(env, f.view));

    f.render->m_width = 800;
    f.render->m_height = 600;
    RequestResolution(f.view, 800, 600);
    OBL_CHECK(f.view->m_resolutionRequested == 0);
    RequestResolution(f.view, 1024, 600);
    OBL_CHECK(f.view->m_resolutionRequested == 1);
    OBL_CHECK(f.view->m_requestedWidth == 1024);

    OBL_CHECK(FollowOffset(1024, 1.0f) == 512);
    OBL_CHECK(FollowOffset(1024, 0.5f) == 1024);
    OBL_CHECK(FollowOffset(1023, 2.0f) == 255);
}

OBL_TEST_CASE("SetMode rolls out a minimized scenery panel for the placing modes")
{
    Fixture f;
    f.Add(0, 1, false, "SceneryPanel");
    f.D(0)->m_minimized = 1;
    struct ModeEnv : Env {
        void AnimateDialog(Dialog*, int type, float, int) { log.push_back("animate" + std::to_string(type)); }
        void SetButtonGraphic(Widget*, const char*, const char*) { log.push_back("graphic"); }
    } env{{f}};
    SetMode(env, f.view, 7);
    OBL_CHECK(f.view->m_requestedMode == 7);
    OBL_CHECK(f.D(0)->m_minimized == 0);
    OBL_CHECK((env.log == std::vector<std::string>{"animate4"}));
    SetMode(env, f.view, 8); // already rolled out
    OBL_CHECK(env.log.size() == 1);
}

namespace {
// The surface, drawing and update calls on top of the mouse Env; every call lands in the log, the answers come from the fields.
struct FrameEnv : Env {
    bool resize = false, world = true, systemCursor = false, systemCursorHidden = false, creditsFinished = false;
    uint64_t lost = 0;
    std::string S(int v) { return std::to_string(v); }
    bool HasWorld() { return world; }
    bool IsSurfaceLost(void*, int s)
    {
        log.push_back("lost" + S(s));
        return (lost >> s) & 1;
    }
    void RestoreSurface(void*, int s) { log.push_back("restore" + S(s)); }
    void ClearSurface(void*, int s) { log.push_back("clear" + S(s)); }
    void RecreateSurface(void*, int s) { log.push_back("recreate" + S(s)); }
    void SetActiveSurface(void*, int s) { log.push_back("active" + S(s)); }
    void SetSurfaceOrigin(void*, int s, int ox, int oy) { log.push_back("origin" + S(s) + ":" + S(ox) + "," + S(oy)); }
    bool ResizeSurface(void*, int s, int w, int h)
    {
        log.push_back("resize" + S(s) + ":" + S(w) + "x" + S(h));
        return resize;
    }
    void DrawSprite(void*, int sx, int sy, uint32_t depth, float time)
    {
        log.push_back("sprite" + S(sx) + "," + S(sy) + "," + S(static_cast<int>(depth)) + "," + S(static_cast<int>(time)));
    }
    void DrawProgressBar(View*) { log.push_back("progressbar"); }
    void RenderCursor() { log.push_back("cursor"); }
    bool SystemCursorSetting() { return systemCursor; }
    bool SystemCursorHidden() { return systemCursorHidden; }
    std::string W(void* w)
    {
        return w == &widgets[0]    ? "main"
                : w == &widgets[1] ? "progress"
                : w == &widgets[2] ? "extra"
                : w == &widgets[3] ? "radar"
                : w == &widgets[4] ? "button"
                                   : "?";
    }
    static inline std::array<uint8_t, 5> widgets{}; // shared: the widget pointers outlive one env
    void WidgetRedraw(void* w)
    {
        log.push_back("redraw:" + (Index(static_cast<Dialog*>(w)) >= 0 ? "d" + S(Index(static_cast<Dialog*>(w))) : W(w)));
    }
    void WidgetDraw(void* w)
    {
        log.push_back("draw:" + (Index(static_cast<Dialog*>(w)) >= 0 ? "d" + S(Index(static_cast<Dialog*>(w))) : W(w)));
    }
    void WidgetUpdate(void* w, float) { log.push_back("update:" + W(w)); }
    void DialogRefresh(void*) { log.push_back("refresh"); }
    void ModeDraw(void*) { log.push_back("modedraw"); }
    void ModeUpdate(void*, float) { log.push_back("modeupdate"); }
    void DialogOrigin(void* d, int* ox, int* oy)
    {
        *ox = 10 * (Index(static_cast<Dialog*>(d)) + 1);
        *oy = 5;
    }
    void SwitchMode(View*) { log.push_back("switch"); }
    void UpdateDialogs(View*, float) { log.push_back("dialogs"); }
    void UpdateMouse(View*) { log.push_back("mouse"); }
    void ChangeResolution(int w, int h) { log.push_back("res" + S(w) + "x" + S(h)); }
    void SetSettingsResolution(int w, int h) { log.push_back("setres" + S(w) + "x" + S(h)); }
    void ClearDialogPositions() { log.push_back("clearpositions"); }
    void RequestGuiReload() { log.push_back("reload"); }
    void RandomizeRainDrops() { log.push_back("rain"); }
    void UpdateRadarViewRect(View*) { log.push_back("radarrect"); }
    void RelayoutProgressBar(View*) { log.push_back("relayout"); }
    void UpdateMusicPlayer(void*) { log.push_back("music"); }
    void UpdateTickerList(void*) { log.push_back("ticker"); }
    void ShiftRadarViewRect(View*) { log.push_back("shift"); }
    void UpdateCredits(obl::ui::GuiCredits*, float) { log.push_back("credits"); }
    bool CreditsFinished(obl::ui::GuiCredits*) { return creditsFinished; }
    void DestructCredits(obl::ui::GuiCredits*) { log.push_back("destruct"); }
    void FreeCredits(obl::ui::GuiCredits*) { log.push_back("free"); }
    void StoreDialogPosition(int px, int py, int key) { log.push_back("store" + S(px) + "," + S(py) + "," + S(key)); }
    void ProviderClosed(void*) { log.push_back("provider"); }
    void ShowDialog(View*, const char* name, void*, bool) { log.push_back(std::string("show:") + name); }
};
} // namespace

OBL_TEST_CASE("SyncBarSurfaces sizes the bar by the screen width and rebuilds only when the surface changed")
{
    Fixture f;
    f.Add(0, 1, false);
    f.Add(1, 1, false);
    f.view->m_barSurface = 1;
    f.view->m_radarSurface = 2;
    f.view->m_cursorSurface = 3;
    f.view->m_progressSurface = 4;
    f.entries[0].m_surface = 10;
    f.entries[1].m_surface = 11;
    f.render->m_width = 799;
    f.render->m_height = 600;
    FrameEnv narrow{{f}};
    SyncBarSurfaces(narrow, f.view);
    OBL_CHECK(f.view->m_barHeight == 0x4b);
    OBL_CHECK((narrow.log == std::vector<std::string>{"bar70", "resize1:799x75"}));

    f.render->m_width = 800;
    FrameEnv wide{{f}};
    wide.resize = true;
    f.view->m_radarWidget = reinterpret_cast<obl::ui::GuiWidget*>(&wide.widgets[3]);
    f.view->m_radarButton = reinterpret_cast<obl::ui::GuiWidget*>(&wide.widgets[4]);
    f.view->m_progressButton = reinterpret_cast<obl::ui::GuiWidget*>(&wide.widgets[1]);
    SyncBarSurfaces(wide, f.view);
    OBL_CHECK(f.view->m_barHeight == 0x5e);
    OBL_CHECK((wide.log ==
            std::vector<std::string>{"bar88", "resize1:800x94", "clear1", "origin1:0,506", "recreate3", "clear3", "recreate2", "clear2",
                    "recreate4", "clear4", "recreate10", "clear10", "recreate11", "clear11", "redraw:radar", "redraw:button",
                    "redraw:progress"}));
    OBL_CHECK(f.view->m_radarDirty == 1);
    OBL_CHECK(f.view->m_progressDirty == 1);

    f.view->m_mode = 6; // no GUI bar: the height stays, SetGuiBarHeight is not called
    f.view->m_radarWidget = nullptr;
    FrameEnv none{{f}};
    SyncBarSurfaces(none, f.view);
    OBL_CHECK(none.log.front() == "resize1:800x94");
}

OBL_TEST_CASE("DrawSurfaces restores lost surfaces and draws the dialogs from the last one to the first")
{
    Fixture f;
    f.Add(0, 1, false);
    f.Add(1, 2, false); // closing: not drawn
    f.Add(2, 1, false);
    f.entries[0].m_surface = 10;
    f.entries[1].m_surface = 11;
    f.entries[2].m_surface = 12;
    f.view->m_barSurface = 1;
    f.view->m_radarSurface = 2;
    f.view->m_cursorSurface = 3;
    f.view->m_progressSurface = 4;
    FrameEnv env{{f}};
    f.view->m_mainItem = reinterpret_cast<obl::ui::GuiWidget*>(&env.widgets[0]);
    f.view->m_progressButton = reinterpret_cast<obl::ui::GuiWidget*>(&env.widgets[1]);
    env.lost = (1u << 4) | (1u << 12);
    DrawSurfaces(env, f.view);
    OBL_CHECK((env.log ==
            std::vector<std::string>{"moving", "lost1", "lost2", "lost4", "restore4", "clear4", "redraw:progress", "lost3", "progressbar",
                    "active1", "draw:main", "lost12", "restore12", "clear12", "redraw:d2", "active12", "origin12:30,5", "draw:d2", "lost10",
                    "active10", "origin10:10,5", "draw:d0", "active3", "cursor", "still"}));
    OBL_CHECK(f.view->m_progressDirty == 1);
    OBL_CHECK(f.view->m_variableProvider == reinterpret_cast<void*>(0x100));

    // Outside gameplay the dialogs also get their two refresh slots; the software cursor is off while the system one is used.
    FrameEnv menu{{f}};
    menu.world = false;
    menu.systemCursor = true;
    f.view->m_dialogs->m_next->m_entry->m_state = 2;
    f.view->m_dialogs->m_previous->m_entry->m_state = 2;
    DrawSurfaces(menu, f.view);
    OBL_CHECK((menu.log == std::vector<std::string>{"lost1", "lost2", "lost4", "lost3", "progressbar", "active1", "draw:main", "still"}));
}

OBL_TEST_CASE("Update runs the frame in order, applies a pending resolution and counts the tooltip delay down")
{
    Fixture f;
    FrameEnv env{{f}};
    FakeMode mode;
    f.view->m_mainItem = reinterpret_cast<obl::ui::GuiWidget*>(&env.widgets[0]);
    f.view->m_radarButton = reinterpret_cast<obl::ui::GuiWidget*>(&env.widgets[4]);
    f.view->m_progressButton = reinterpret_cast<obl::ui::GuiWidget*>(&env.widgets[1]);
    f.view->m_modeObject = &mode;
    f.view->m_resolutionRequested = 1;
    f.view->m_requestedWidth = 1280;
    f.view->m_requestedHeight = 720;
    f.view->m_requestedMode = 6;
    f.view->m_tooltipCooldown = 0.5f;
    f.view->m_radarEnabled = 1;
    f.view->m_progressVisible = 1;
    f.view->m_variableProvider = &mode;
    Update(env, f.view, 0.75f);
    OBL_CHECK((env.log ==
            std::vector<std::string>{"switch", "update:main", "dialogs", "mouse", "modeupdate", "res1280x720", "setres1280x720",
                    "clearpositions", "reload", "rain", "radarrect", "relayout", "music", "ticker", "update:button", "shift",
                    "update:progress"}));
    OBL_CHECK(f.view->m_requestedMode == kDefaultMode);
    OBL_CHECK(f.view->m_resolutionRequested == 0);
    OBL_CHECK(f.view->m_tooltipCooldown == 0.0f);
    OBL_CHECK(f.view->m_variableProvider == nullptr);

    FrameEnv slow{{f}};
    f.view->m_tooltipCooldown = 2.0f;
    Update(slow, f.view, 0.5f);
    OBL_CHECK(f.view->m_tooltipCooldown == 1.5f);
    f.view->m_tooltipCooldown = -1.0f;
    Update(slow, f.view, 0.5f);
    OBL_CHECK(f.view->m_tooltipCooldown == -1.0f); // a negative value is left alone: only a positive delay counts down
}

OBL_TEST_CASE("Update ends the credits: closes every dialog, remembers where it stood and opens the extras screen")
{
    Fixture f;
    f.Add(0, 1, false);
    f.Add(1, 1, false);
    f.D(0)->m_positionKey = 7;
    obl::ui::GuiCredits credits{};
    f.view->m_credits = &credits;
    f.view->m_mode = 4;
    f.view->m_requestedMode = 4;
    FrameEnv running{{f}};
    Update(running, f.view, 0.1f);
    OBL_CHECK((running.log == std::vector<std::string>{"switch", "dialogs", "mouse", "music", "ticker", "credits"}));
    OBL_CHECK(f.view->m_credits == &credits);

    FrameEnv env{{f}};
    env.creditsFinished = true;
    Update(env, f.view, 0.1f);
    OBL_CHECK((env.log ==
            std::vector<std::string>{"switch", "dialogs", "mouse", "music", "ticker", "credits", "destruct", "free", "store10,5,7",
                    "provider", "store20,5,0", "provider", "show:ExtrasScreen"}));
    OBL_CHECK(f.view->m_credits == nullptr);
    OBL_CHECK(f.entries[0].m_state == 2);
    OBL_CHECK(f.entries[1].m_state == 2);

    // Without a GUI bar (mode 6) the positions are not remembered.
    f.view->m_credits = &credits;
    f.view->m_requestedMode = 6;
    FrameEnv bare{{f}};
    bare.creditsFinished = true;
    Update(bare, f.view, 0.1f);
    OBL_CHECK((bare.log ==
            std::vector<std::string>{"switch", "dialogs", "mouse", "music", "ticker", "credits", "destruct", "free", "provider", "provider",
                    "show:ExtrasScreen"}));
}

namespace {
// Construct, CreateBarSurfaces and Destruct: allocation, construction and teardown calls are logged; the New* calls answer from `objects`.
struct LifeEnv : FrameEnv {
    std::array<std::array<uint32_t, 0x10>, 4> objects{};
    std::array<Node, 1> head{};
    View* manager = reinterpret_cast<View*>(&head);
    int nextSurface = 20;
    obl::ui::SpriteAnim anim{};
    obl::ui::SpriteAnim* anims[2]{nullptr, &anim};
    obl::ui::Sprite sprite{};
    obl::ui::SpriteInstance instance{};
    int news = 0;
    void SetManager(View* frame)
    {
        manager = frame;
        log.push_back(frame ? "manager" : "nomanager");
    }
    void* AllocateListHead()
    {
        log.push_back("head");
        return head.data();
    }
    void FreeListHead(Node*) { log.push_back("freehead"); }
    void* Renderer() { return f.render; }
    void CreateBarSurfaces(View*) { log.push_back("barsurfaces"); }
    void* NewTooltip()
    {
        log.push_back("tooltip");
        return objects[news++].data();
    }
    void* NewMusicPlayer()
    {
        log.push_back("music");
        return objects[news++].data();
    }
    void* NewTickerList()
    {
        log.push_back("ticker");
        return objects[news++].data();
    }
    void CreateProgressBar(View*) { log.push_back("progressbar"); }
    int32_t CreateSurface(void*, int w, int h, int)
    {
        log.push_back("create" + S(w) + "x" + S(h));
        return nextSurface++;
    }
    const char* RadarPanelSpriteName() { return "radar"; }
    const char* ProgressPanelSpriteName() { return "progress"; }
    obl::ui::SpriteInstance* CreateSprite(const char* name)
    {
        log.push_back(std::string("sprite:") + name);
        anim.m_width = name[0] == 'r' ? 120 : 200;
        anim.m_height = name[0] == 'r' ? 90 : 30;
        sprite.m_anims = anims;
        instance.m_sprite = &sprite;
        instance.m_animIndex = 1;
        return &instance;
    }
    void ReleaseSprite(void* s) { log.push_back(s ? "release" : "releasenull"); }
    void SetDialogOpen(void*, bool open) { log.push_back("open" + S(open)); }
    void ReleaseSurface(int s) { log.push_back("releasesurface" + S(s)); }
    void DeleteEntry(void*) { log.push_back("deleteentry"); }
    void FreeNode(void*) { log.push_back("freenode"); }
    void DestroyListNode(void*) { log.push_back("destroynode"); }
    void Delete(void*) { log.push_back("delete"); }
    void ReleaseFont(const void*) { log.push_back("font"); }
    void DeleteWidget(void* w) { log.push_back("deletewidget:" + W(w)); }
    void DestructTooltip(void*) { log.push_back("~tooltip"); }
    void DestructMusicPlayer(void*) { log.push_back("~music"); }
    void DestructCreditsObject(obl::ui::GuiCredits*) { log.push_back("~credits"); }
};
} // namespace

OBL_TEST_CASE("CreateBarSurfaces makes the bar, cursor and panel surfaces from the screen and the panel sprites")
{
    Fixture f;
    f.render->m_width = 1024;
    f.render->m_height = 768;
    f.view->m_mode = 4;
    LifeEnv env{{{f}}};
    OBL_CHECK(CreateBarSurfaces(env, f.view));
    OBL_CHECK((env.log ==
            std::vector<std::string>{"bar88", "create1024x94", "clear20", "origin20:0,674", "create32x32", "clear21", "sprite:radar",
                    "create120x90", "clear22", "release", "sprite:progress", "create200x30", "clear23", "release"}));
    OBL_CHECK(f.view->m_barSurface == 20);
    OBL_CHECK(f.view->m_cursorSurface == 21);
    OBL_CHECK(f.view->m_radarSurface == 22);
    OBL_CHECK(f.view->m_progressSurface == 23);
}

OBL_TEST_CASE("Construct starts in the default mode with the no-GUI mode requested and owns its helpers")
{
    Fixture f;
    LifeEnv env{{{f}}};
    OBL_CHECK(Construct(env, f.view) == f.view);
    OBL_CHECK(env.manager == f.view);
    OBL_CHECK((env.log == std::vector<std::string>{"manager", "head", "barsurfaces", "tooltip", "music", "ticker", "progressbar"}));
    OBL_CHECK(f.view->m_dialogs == env.head.data());
    OBL_CHECK(f.view->m_dialogs->m_next == f.view->m_dialogs);
    OBL_CHECK(f.view->m_renderer == f.render);
    OBL_CHECK(f.view->m_mode == 4);
    OBL_CHECK(f.view->m_requestedMode == 6);
    OBL_CHECK(f.view->m_selection == -1);
    OBL_CHECK(f.view->m_context[4] == -1);
    OBL_CHECK(f.view->m_context[0] == 0);
    OBL_CHECK(f.view->m_cameraGrabSurface == -1);
    OBL_CHECK(f.view->m_tickerList == env.objects[2].data());
}

OBL_TEST_CASE("Destruct tears everything down in the fixed order and clears the global last")
{
    Fixture f;
    f.Add(0, 1, false);
    LifeEnv env{{{f}}};
    std::array<obl::ui::GuiManagerWidgetNode, 1> widgets{};
    f.view->m_widgets = widgets.data();
    std::array<uint32_t, 2> font{}, renderer{};
    f.view->m_fonts[3] = font.data();
    f.view->m_textRenderers[3] = reinterpret_cast<obl::ui::TextRenderer*>(renderer.data());
    f.view->m_textRenderers[4] = reinterpret_cast<obl::ui::TextRenderer*>(renderer.data()); // no font in slot 4: the renderer stays
    f.view->m_mainItem = reinterpret_cast<obl::ui::GuiWidget*>(&env.widgets[0]);
    f.view->m_progressButton = reinterpret_cast<obl::ui::GuiWidget*>(&env.widgets[1]);
    FakeMode mode;
    f.view->m_modeObject = &mode;
    f.view->m_tooltip = &env.objects[1];
    f.view->m_tickerList = &env.objects[2];
    f.view->m_variableProvider = &env;
    Destruct(env, f.view);
    OBL_CHECK((env.log ==
            std::vector<std::string>{"open0", "releasesurface0", "deleteentry", "freenode", "destroynode", "delete", "font", "delete",
                    "delete", "deletewidget:main", "exit", "delete", "~tooltip", "delete", "delete", "releasenull", "deletewidget:progress",
                    "freehead", "nomanager"}));
    OBL_CHECK(f.view->m_widgets == nullptr);
    OBL_CHECK(f.view->m_variableProvider == nullptr);
    OBL_CHECK(f.view->m_fonts[3] == font.data()); // Destruct leaves the slots, LoadScript empties them
}

namespace {
// Credits rows come from packs/base/gui/credits.json, so the roll ends after however many rows were loaded.
struct CreditsEnv {
    std::vector<obl::ui::GuiCreditEntry> entries;
    std::vector<std::vector<uint8_t>> blocks;
    std::vector<int> fonts;
    std::span<const obl::ui::GuiCreditEntry> CreditEntries() { return entries; }
    void* Block(size_t bytes) { return blocks.emplace_back(bytes).data(); }
    void* NewLine() { return Block(sizeof(obl::ui::GuiCreditsLine)); }
    void* NewText(const char*, int font, uint32_t)
    {
        fonts.push_back(font);
        return std::construct_at(static_cast<obl::ui::GuiTextData*>(Block(sizeof(obl::ui::GuiTextData))));
    }
    void* AllocateNode() { return Block(sizeof(obl::ui::GuiCreditsNode)); }
};
} // namespace

OBL_TEST_CASE("AddCreditsLine stops after the last loaded credit row")
{
    CreditsEnv env;
    env.entries = {{1, 10, 0xff, 0, 0}, {2, 11, 0xee, 1, 1}};
    obl::ui::GuiCreditsNode sentinel{&sentinel, &sentinel, nullptr};
    obl::ui::GuiCredits credits{};
    credits.m_lines = &sentinel;
    AddCreditsLine(env, &credits);
    OBL_CHECK(credits.m_delay == 0xf);
    AddCreditsLine(env, &credits);
    OBL_CHECK(credits.m_delay == 0x1e);
    AddCreditsLine(env, &credits);
    OBL_CHECK(credits.m_nextEntry == 2u);
    OBL_CHECK((env.fonts == std::vector<int>{1, 2}));
    OBL_CHECK(sentinel.m_previous->m_line->m_kind == 1);
}
