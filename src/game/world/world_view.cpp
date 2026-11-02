#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/renderer_api.h"
#include "game/ui/cursor.h"
#include "game/ui/input.h"
#include "game/world/world_view.h"
#include "game/game/settings_store.h"
#include "game/weather/weather.h"

#include "game/game/game_accessors.h"
#include "game/game/command_loop.h"
#include "game/game/game.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/map_view.h"
#include "game/ui/renderer_types.h"
#include "game/world/world_state.h"
#include "game/ui/cursor_types.h"
#include "core/call_counter.h"
#include "game/world/world_view_logic.h"

#include <cstdint>

namespace obl::world {
namespace {

using layout::World;
using world_view::Panel;

// Call counters (`call-report <name>`).
test::CallCounter g_zoomCalls("world.apply-zoom-request");
test::CallCounter g_cameraCalls("world.update-camera-input");
test::CallCounter g_hotkeysCalls("world.update-hotkeys");
test::CallCounter g_drawCalls("world.draw");
test::CallCounter g_overlaysCalls("world.draw-object-overlays");

// ---- the game side of the Env contracts in src/game/world/world_view_logic.h ------------------------------------------------------------

class GameEnv {
public:
    explicit GameEnv(World& world) : m_world(world), m_view(game::GetContext().view) {}

    // zoom
    int ZoomLevel() { return game::GetRenderer()->m_zoomLevel; }
    uint32_t RenderWidth() { return static_cast<uint32_t>(game::GetRenderer()->m_width); }
    uint32_t RenderHeight() { return static_cast<uint32_t>(game::GetRenderer()->m_height); }
    void SetZoomLevel(int level) { ui::renderer::SetZoomLevel(game::GetRenderer(), level); }
    void RandomizeRainDrops() { weather::runtime::RandomizeRainDrops(); }
    void SetCameraPos(int x, int y) { (m_world.m_mapView)->SetCameraPos(x, y); }
    int CameraX() { return static_cast<const ui::MapView*>(m_world.m_mapView.get())->m_cameraX; }
    int CameraY() { return static_cast<const ui::MapView*>(m_world.m_mapView.get())->m_cameraY; }

    // camera
    float FrameDelta() { return game::GetGameClock()->m_deltaTime; }
    bool KeyHeld(int code) { return InputKeyHeld(*m_view.m_input, code); }
    void AddVelocity(float x, float y) { (m_world.m_mapView)->AddCameraVelocity(x, y); }
    void ClearKeyHover() { game::GetGuiManager()->m_selection = -1; }
    bool NudgeScroll() { return game::settings_store::Current().m_nudgeScroll != 0; }
    int RightButton() { return m_view.m_input->m_rightButton; }
    int MouseDeltaX() { return m_view.m_input->m_deltaX; }
    int MouseDeltaY() { return m_view.m_input->m_deltaY; }
    bool MainItemHovered() { return game::GetGuiManager()->IsMainItemHovered(); }
    bool CursorInViewRect()
    {
        const auto* view = static_cast<const ui::MapView*>(m_world.m_mapView.get());
        return CursorIsInRect(Software(), view->m_viewLeft, view->m_viewTop, view->m_viewWidth, view->m_viewHeight);
    }
    bool CanUseWorldCameraInput() { return game::GetGuiManager()->CanUseWorldCameraInput(); }
    void SetCursorShape(int shape) { CursorSetShape(Software(), shape); }
    void SetDragMode(bool on) { CursorSetDragMode(Software(), on ? 1 : 0); }
    int CursorX() { return m_view.m_cursor->m_x; }
    int CursorY() { return m_view.m_cursor->m_y; }

    // hotkeys
    bool KeyPressed(int code) { return InputKeyPressed(*m_view.m_input, code); }
    void ShowPause() { game::GetGuiManager()->ShowPauseUnlessScenarioEnded(); }
    void ClosePause() { game::GetGuiManager()->ClosePauseDialog(); }
    void TogglePanel(Panel panel)
    {
        static void (ui::GuiManager::* const kToggles[])() = {&ui::GuiManager::ToggleLoadGameMenu, &ui::GuiManager::ToggleSaveGameMenu,
                &ui::GuiManager::ToggleInGameOptions, &ui::GuiManager::ToggleMusicPanel, &ui::GuiManager::ToggleBuildPanel,
                &ui::GuiManager::ToggleSceneryPanel};
        (game::GetGuiManager()->*kToggles[static_cast<int>(panel)])();
    }
    void SetPaused(bool paused) { game::SubmitCommand(command::SetPaused{paused}); }
    void SetGameSpeed(int level) { game::SubmitCommand(command::SetGameSpeed{level}); }

    // drawing
    void ResetViewport() { (m_world.m_mapView)->ResetViewport(); }
    void DrawSea() { (m_world.m_mapView)->DrawSea(); }
    void DarkenByDepth() { (m_world.m_mapView)->DarkenByDepth(); }
    void DrawObjects(void** list, int count, uint32_t arg)
    {
        (m_world.m_mapView)->DrawObjects(reinterpret_cast<object::Base**>(list), count, static_cast<int>(arg));
    }
    void DrawRain() { weather::runtime::DrawRain(m_world.m_weather); }
    void SetActiveSurface(int surface) { ui::renderer::SetActiveSurface(game::GetRenderer(), surface); }
    void SetRenderVisible(bool on) { game::GetRenderer()->m_renderVisibleObjects = on ? 1 : 0; }
    void SetMoving(bool on) { game::GetRenderer()->m_directMode = on ? 1 : 0; }
    void RenderVisible(void** list, int count) { (m_world.m_mapView)->RenderVisibleObjects(reinterpret_cast<object::Base**>(list), count); }

private:
    World& m_world;
    ui::ViewState& m_view;
    // The view borrows the Game's software cursor through its position-only record.
    ui::SoftwareCursor* Software() { return reinterpret_cast<ui::SoftwareCursor*>(m_view.m_cursor); }
};

// ---- entry points ---------------------------------------------------------------------------------------------------------------

void ApplyZoomRequestEntry(World* world)
{
    g_zoomCalls.Count();
    GameEnv env(*world);
    world_view::ApplyZoomRequest(game::GetContext().view, env);
}

void UpdateCameraInputEntry(World* world)
{
    g_cameraCalls.Count();
    GameEnv env(*world);
    world_view::UpdateCameraInput(game::GetContext().view, env);
}

void UpdateHotkeysEntry(World* world)
{
    g_hotkeysCalls.Count();
    GameEnv env(*world);
    world_view::UpdateHotkeys(*world, game::GetContext().view, env);
}

void DrawWorldEntry(World* world)
{
    g_drawCalls.Count();
    GameEnv env(*world);
    world_view::Draw(*world, game::GetContext().view, env);
}

void DrawOverlaysEntry(World* world)
{
    g_overlaysCalls.Count();
    GameEnv env(*world);
    world_view::DrawObjectOverlays(*world, env);
}

} // namespace

void ApplyZoomRequest(World& world)
{
    ApplyZoomRequestEntry(&world);
}
void UpdateCameraInput(World& world)
{
    UpdateCameraInputEntry(&world);
}
void UpdateHotkeys(World& world)
{
    UpdateHotkeysEntry(&world);
}
void Draw(World& world)
{
    DrawWorldEntry(&world);
}
void DrawObjectOverlays(World& world)
{
    DrawOverlaysEntry(&world);
}

} // namespace obl::world
