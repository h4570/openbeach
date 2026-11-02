#include "unit_test.h"

#include "game/world/world_view_logic.h"
#include "game/objects/object_manager_state.h"
#include "game/world/world_state.h"
#include "game/ui/cursor_types.h"
#include "game/ui/map_view.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_view;

namespace {

struct Fixture;
Fixture* g_fixture = nullptr; // the Env reads the camera and the cursor of the current fixture

struct Fixture {
    std::unique_ptr<world_data::World> world = std::make_unique<world_data::World>();
    ui::MapView* view = new ui::MapView(); // value-initialized: zeroed fields, an empty burn queue
    ui::Cursor cursor{};
    ui::ViewState state; // the map view's input and drawing state
    Fixture()
    {
        world->m_mapView.reset(view);
        state.m_cursor = &cursor;
        cursor.m_x = 100;
        cursor.m_y = 100;
        g_fixture = this;
    }
    ~Fixture()
    {
        delete view;
        g_fixture = nullptr;
    }
};

struct Env {
    std::vector<std::string> calls;
    std::vector<int> held, pressed;
    float dt = 0.5f;
    int zoom = 1;
    uint32_t width = 800, height = 600;
    bool nudge = false, mainHovered = false, inRect = true, canUse = true;
    int rightButton = 0, deltaX = 0, deltaY = 0;
    uint32_t table[3] = {0x3f800000u, 0x40000000u, 0x41000000u};

    static bool Has(const std::vector<int>& keys, int dik)
    {
        for (int k : keys)
            if (k == dik) return true;
        return false;
    }
    void Log(const std::string& text) { calls.push_back(text); }

    int ZoomLevel() { return zoom; }
    uint32_t RenderWidth() { return width; }
    uint32_t RenderHeight() { return height; }
    void SetZoomLevel(int level) { Log("zoom" + std::to_string(level)); }
    void RandomizeRainDrops() { Log("rain"); }
    void SetCameraPos(int x, int y) { Log("pos " + std::to_string(x) + " " + std::to_string(y)); }
    int CameraX() { return g_fixture->view->m_cameraX; }
    int CameraY() { return g_fixture->view->m_cameraY; }
    int CursorX() { return g_fixture->cursor.m_x; }
    int CursorY() { return g_fixture->cursor.m_y; }

    float FrameDelta() { return dt; }
    bool KeyHeld(int dik) { return Has(held, dik); }
    void AddVelocity(float x, float y) { Log("vel " + std::to_string(x) + " " + std::to_string(y)); }
    void ClearKeyHover() { Log("hover"); }
    bool NudgeScroll() { return nudge; }
    int RightButton() { return rightButton; }
    int MouseDeltaX() { return deltaX; }
    int MouseDeltaY() { return deltaY; }
    bool MainItemHovered() { return mainHovered; }
    bool CursorInViewRect() { return inRect; }
    bool CanUseWorldCameraInput() { return canUse; }
    void SetCursorShape(int shape) { Log("shape" + std::to_string(shape)); }
    void SetDragMode(bool on) { Log(on ? "drag1" : "drag0"); }

    bool KeyPressed(int dik) { return Has(pressed, dik); }
    void ShowPause() { Log("showpause"); }
    void ClosePause() { Log("closepause"); }
    void TogglePanel(Panel panel) { Log("panel" + std::to_string(static_cast<int>(panel))); }
    void SetPaused(bool paused) { Log(paused ? "pause1" : "pause0"); }
    void SetGameSpeed(int level) { Log("speed" + std::to_string(level)); }

    void ResetViewport() { Log("reset"); }
    void DrawSea() { Log("sea"); }
    void DarkenByDepth() { Log("darken"); }
    void DrawObjects(void** list, int count, uint32_t arg)
    {
        Log("objects " + std::to_string(reinterpret_cast<uintptr_t>(list) != 0) + " n" + std::to_string(count) + " a" +
                std::to_string(arg));
    }
    void DrawRain() { Log("drawrain"); }
    void SetActiveSurface(int surface) { Log("surface" + std::to_string(surface)); }
    void SetRenderVisible(bool on) { Log(on ? "visible1" : "visible0"); }
    void SetMoving(bool on) { Log(on ? "moving1" : "moving0"); }
    void RenderVisible(void**, int count) { Log("render n" + std::to_string(count)); }
};

bool Is(const Env& env, std::initializer_list<const char*> expected)
{
    if (env.calls.size() != expected.size()) return false;
    size_t i = 0;
    for (const char* text : expected)
        if (env.calls[i++] != text) return false;
    return true;
}

} // namespace

OBL_TEST_CASE("world_view.zoom_without_a_change_does_nothing")
{
    Fixture f;
    Env env;
    f.state.m_zoomRequest = 1;
    ApplyZoomRequest(f.state, env);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("world_view.zoom_keeps_the_view_centre")
{
    Fixture f;
    f.view->m_cameraX = 1000;
    f.view->m_cameraY = 800;
    Env env;
    env.zoom = 1;
    f.state.m_zoomRequest = 0; // normal -> in: a quarter of the view back to the centre
    ApplyZoomRequest(f.state, env);
    OBL_CHECK(Is(env, {"zoom0", "rain", "pos 1200 950"}));
    env = Env();
    env.zoom = 0;
    f.state.m_zoomRequest = 2; // in -> out: undo the quarter, then take half
    ApplyZoomRequest(f.state, env);
    OBL_CHECK(Is(env, {"zoom2", "rain", "pos 400 350"}));
    env = Env();
    env.zoom = 2;
    f.state.m_zoomRequest = 1; // out -> normal: give the half back
    ApplyZoomRequest(f.state, env);
    OBL_CHECK(Is(env, {"zoom1", "rain", "pos 1400 1100"}));
}

OBL_TEST_CASE("world_view.camera_keys_and_edge_scroll")
{
    Fixture f;
    Env env;
    env.held = {kKeyLeft, kKeyDown};
    UpdateCameraInput(f.state, env);
    OBL_CHECK(Is(env, {"vel -75.000000 0.000000", "vel 0.000000 75.000000", "hover"}));
    env = Env();
    env.nudge = true;
    f.cursor.m_x = 0;
    f.cursor.m_y = 599;
    UpdateCameraInput(f.state, env);
    OBL_CHECK(Is(env, {"vel -75.000000 0.000000", "vel 0.000000 75.000000"}));
    env = Env();
    env.nudge = true;
    f.cursor.m_x = 799;
    f.cursor.m_y = 0;
    UpdateCameraInput(f.state, env);
    OBL_CHECK(Is(env, {"vel 75.000000 0.000000", "vel 0.000000 -75.000000"}));
}

OBL_TEST_CASE("world_view.right_button_drag_moves_and_marks_the_state")
{
    Fixture f;
    Env env;
    env.rightButton = kRightButtonHeld;
    env.deltaX = 4;
    env.deltaY = -2;
    UpdateCameraInput(f.state, env);
    OBL_CHECK(Is(env, {"vel 20.000000 -10.000000", "shape3", "drag1"}));
    OBL_CHECK_EQ(f.state.m_cameraDragging, 1);
    env.calls.clear();
    UpdateCameraInput(f.state, env); // already dragging: only the velocity
    OBL_CHECK(Is(env, {"vel 20.000000 -10.000000"}));
    env.calls.clear();
    env.rightButton = 0;
    UpdateCameraInput(f.state, env);
    OBL_CHECK(Is(env, {"shape0", "drag0"}));
    OBL_CHECK_EQ(f.state.m_cameraDragging, 0);
}

OBL_TEST_CASE("world_view.drag_over_a_gui_item_changes_nothing")
{
    Fixture f;
    Env env;
    env.rightButton = kRightButtonHeld;
    env.mainHovered = true;
    env.deltaX = 9;
    UpdateCameraInput(f.state, env);
    OBL_CHECK(env.calls.empty());
    OBL_CHECK_EQ(f.state.m_cameraDragging, 0);
    env.mainHovered = false;
    env.inRect = false;
    env.canUse = false; // outside the view and the GUI refuses: no velocity and no cursor shape, but the drag mode starts
    UpdateCameraInput(f.state, env);
    OBL_CHECK(Is(env, {"drag1"}));
}

OBL_TEST_CASE("world_view.pause_hotkey_toggles_unless_forced")
{
    Fixture f;
    Env env;
    env.pressed = {kKeyPause};
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.world->m_paused, 0); // Submission leaves the world alone until the tick.
    f.world->m_paused = 1;
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.world->m_paused, 1);
    OBL_CHECK(Is(env, {"pause1", "showpause", "pause0", "closepause"}));
    env.calls.clear();
    f.world->m_paused = 0;
    f.world->m_forcedPaused = 1;
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.world->m_paused, 0);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("world_view.panel_keys_toggle_in_the_fixed_order")
{
    Fixture f;
    Env env;
    env.pressed = {kKeyScenery, kKeyLoadMenu, kKeyBuild, kKeySaveMenu, kKeyMusic, kKeyOptions};
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK(Is(env, {"panel0", "panel1", "panel2", "panel3", "panel4", "panel5"}));
}

OBL_TEST_CASE("world_view.zoom_keys_request_one_step")
{
    Fixture f;
    Env env;
    env.pressed = {kKeyZoomIn};
    env.zoom = 1;
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.state.m_zoomRequest, 0);
    env.zoom = 2;
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.state.m_zoomRequest, 1);
    env.zoom = 0; // already fully in: the request is left alone
    f.state.m_zoomRequest = 2;
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.state.m_zoomRequest, 2);
    env.pressed = {kKeyZoomOut};
    env.zoom = 0;
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.state.m_zoomRequest, 1);
    env.zoom = 1;
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.state.m_zoomRequest, 2);
}

OBL_TEST_CASE("world_view.speed_keys_step_and_clamp")
{
    Fixture f;
    Env env;
    env.pressed = {kKeySpeedUp};
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.world->m_gameSpeedLevel, 0);
    OBL_CHECK(Is(env, {"speed1"}));
    f.world->m_gameSpeedLevel = 1;
    UpdateHotkeys(*f.world, f.state, env);
    f.world->m_gameSpeedLevel = 2;
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK_EQ(f.world->m_gameSpeedLevel, 2);
    OBL_CHECK(Is(env, {"speed1", "speed2", "speed2"}));
    env.calls.clear();
    env.pressed = {kKeySpeedDown};
    UpdateHotkeys(*f.world, f.state, env);
    f.world->m_gameSpeedLevel = 1;
    UpdateHotkeys(*f.world, f.state, env);
    f.world->m_gameSpeedLevel = 0;
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK(Is(env, {"speed1", "speed0", "speed0"}));
    OBL_CHECK_EQ(f.world->m_gameSpeedLevel, 0);
    env.calls.clear();
    env.pressed = {kKeySpeedUp, kKeySpeedDown};
    UpdateHotkeys(*f.world, f.state, env);
    OBL_CHECK(Is(env, {"speed1", "speed0"}));
}

OBL_TEST_CASE("world_view.draw_sequences_the_three_lists_and_the_rain")
{
    Fixture f;
    auto objects = std::make_unique<object::Manager>();
    objects->m_buildings.assign(3, nullptr);
    objects->m_guests.assign(5, nullptr);
    objects->m_others.assign(7, nullptr);
    f.world->m_objectManager.reset(objects.get());
    f.state.m_drawObjectsArg = 1;
    Env env;
    Draw(*f.world, f.state, env);
    OBL_CHECK(Is(env, {"reset", "sea", "objects 1 n3 a1", "objects 1 n7 a1", "objects 1 n5 a1", "drawrain"}));
    env.calls.clear();
    f.state.m_darkenByDepth = 1;
    Draw(*f.world, f.state, env);
    OBL_CHECK_EQ(env.calls.size(), 7u);
    OBL_CHECK_EQ(env.calls[5], "darken");
    env.calls.clear();
    DrawObjectOverlays(*f.world, env);
    OBL_CHECK(Is(env, {"surface0", "visible1", "moving1", "render n3", "render n7", "render n5", "moving0", "visible0"}));
}
