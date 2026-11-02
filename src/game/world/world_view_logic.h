#pragma once

#include "core/key_code.h"

#include "game/objects/object_manager_state.h"
#include "game/ui/view_state.h"
#include "game/world/world_state.h"

#include <cstdint>
#include <cstring>

// Input, camera and drawing methods of the World class (docs/architecture/commands.md, GUI wiring). The order of the calls into
// other objects is what matters; those calls stay behind an `Env` (the game side makes the real calls, the unit tests record them). Each function lists the members of its Env.
// world::ApplyZoomRequest the zoom level the hotkeys/buttons asked for, applied to the renderer, the rain and the camera
// world::UpdateCameraInput arrow keys, edge scroll and right-button drag move the camera
// world::UpdateHotkeys P, F2/F4/F5, M/B/S panels, PgUp/PgDn zoom, '='/'-' game speed
// world::Draw sea, three object lists, optional depth darkening, rain
// world::DrawObjectOverlays the visible-object pass over the same three lists
namespace obl::world_view {

constexpr auto kKeyLeft = input::KeyCode::Left;
constexpr auto kKeyRight = input::KeyCode::Right;
constexpr auto kKeyUp = input::KeyCode::Up;
constexpr auto kKeyDown = input::KeyCode::Down;
constexpr auto kKeyPause = input::KeyCode::P;
constexpr auto kKeyLoadMenu = input::KeyCode::F5;
constexpr auto kKeySaveMenu = input::KeyCode::F4;
constexpr auto kKeyOptions = input::KeyCode::F2;
constexpr auto kKeyMusic = input::KeyCode::M;
constexpr auto kKeyBuild = input::KeyCode::B;
constexpr auto kKeyScenery = input::KeyCode::S;
constexpr auto kKeyZoomIn = input::KeyCode::PageUp;
constexpr auto kKeyZoomOut = input::KeyCode::PageDown;
constexpr auto kKeySpeedUp = input::KeyCode::Equals;
constexpr auto kKeySpeedDown = input::KeyCode::Minus;
constexpr float kKeyScrollPixelsPerSecond = 150.0f;
constexpr float kDragScale = 10.0f;
constexpr int kRightButtonHeld = 2; // right button state: 0 up, 1 pressed this update, 2 held
constexpr int kCursorShapeNormal = 0, kCursorShapeDrag = 3;

enum class Panel { LoadGame, SaveGame, Options, Music, Build, Scenery };

// ---- world::ApplyZoomRequest --------------------------------------------------------------------------------------------------
//   int ZoomLevel()                         the renderer's zoom level
//   uint32_t RenderWidth() / RenderHeight() the renderer's size (unsigned: shifted logically)
//   void SetZoomLevel(int)                  sets the renderer's zoom level
//   void RandomizeRainDrops()               re-randomizes the rain drops
//   void SetCameraPos(int x, int y)         clamps and updates the float camera
//   int CameraX() / CameraY()               the map view's camera position, read after the rain
// The camera keeps the point at the centre of the view: the previous level takes its offset back, the new level adds its own (a quarter of the view
// size at level 0, minus half at level 2, normal in between).
template<class Env> void ApplyZoomRequest(ui::ViewState& view, Env& env)
{
    const int current = env.ZoomLevel();
    if (view.m_zoomRequest == current) return;
    env.SetZoomLevel(view.m_zoomRequest);
    env.RandomizeRainDrops();
    const uint32_t width = env.RenderWidth(), height = env.RenderHeight();
    int x = env.CameraX(), y = env.CameraY();
    if (current == 0) {
        x -= static_cast<int>(width >> 2);
        y -= static_cast<int>(height >> 2);
    } else if (current == 2) {
        x += static_cast<int>(width >> 1);
        y += static_cast<int>(height >> 1);
    }
    if (view.m_zoomRequest == 0) {
        x += static_cast<int>(width >> 2);
        y += static_cast<int>(height >> 2);
    } else if (view.m_zoomRequest == 2) {
        env.SetCameraPos(x - static_cast<int>(width >> 1), y - static_cast<int>(height >> 1));
        return;
    }
    env.SetCameraPos(x, y);
}

// ---- world::UpdateCameraInput -------------------------------------------------------------------------------------------------
//   float FrameDelta()                      the frame time
//   bool KeyHeld(int code)                   whether the key is held
//   void AddVelocity(float x, float y)      adds to the camera velocity of the map view
//   void ClearKeyHover()                    clears the GUI's key hover (an arrow key moved the camera)
//   bool NudgeScroll()                      the [Game] NudgeScroll option: the screen edge scrolls
//   int RenderWidth() / RenderHeight()      the renderer's size
//   int RightButton() / MouseDeltaX() / MouseDeltaY()   the right button state and the mouse delta
//   bool MainItemHovered()                  whether the GUI's main item is hovered
//   bool CursorInViewRect()                 whether the cursor lies in the map view rectangle
//   bool CanUseWorldCameraInput()           whether the GUI allows world camera input
//   void SetCursorShape(int)                sets the cursor shape
//   void SetDragMode(bool)                  sets the cursor's drag mode
//   int CursorX() / CursorY()               the cursor position
// The edge scroll reads the cursor position straight from the world's cursor (nothing in between moves it). The drag velocity is the integer mouse delta times the frame time times
// 10, in double precision as extended precision computes it (the product of two floats is exact there).
template<class Env> void UpdateCameraInput(ui::ViewState& view, Env& env)
{
    const float step = env.FrameDelta() * kKeyScrollPixelsPerSecond;
    const bool left = env.KeyHeld(kKeyLeft);
    if (left) env.AddVelocity(-step, 0.0f);
    const bool right = env.KeyHeld(kKeyRight);
    if (right) env.AddVelocity(step, 0.0f);
    const bool up = env.KeyHeld(kKeyUp);
    if (up) env.AddVelocity(0.0f, -step);
    const bool down = env.KeyHeld(kKeyDown);
    if (down) env.AddVelocity(0.0f, step);
    if (down || up || right || left) env.ClearKeyHover();

    if (env.NudgeScroll()) {
        const float edge = env.FrameDelta() * kKeyScrollPixelsPerSecond;
        const int cursorX = env.CursorX(), cursorY = env.CursorY();
        if (cursorX == 0) env.AddVelocity(-edge, 0.0f);
        if (cursorX == static_cast<int>(env.RenderWidth()) - 1) env.AddVelocity(edge, 0.0f);
        if (cursorY == 0) env.AddVelocity(0.0f, -edge);
        if (cursorY == static_cast<int>(env.RenderHeight()) - 1) env.AddVelocity(0.0f, edge);
    }

    if (env.RightButton() == kRightButtonHeld) {
        if (env.MainItemHovered()) return;
        if (env.CursorInViewRect()) {
            const double dt = env.FrameDelta();
            const int dy = env.MouseDeltaY();
            const int dx = env.MouseDeltaX();
            env.AddVelocity(static_cast<float>(static_cast<double>(dx) * dt * kDragScale),
                    static_cast<float>(static_cast<double>(dy) * dt * kDragScale));
        }
        if (!view.m_cameraDragging) {
            if (env.CanUseWorldCameraInput()) env.SetCursorShape(kCursorShapeDrag);
            env.SetDragMode(true);
            view.m_cameraDragging = 1;
        }
    } else if (view.m_cameraDragging) {
        view.m_cameraDragging = 0;
        env.SetCursorShape(kCursorShapeNormal);
        env.SetDragMode(false);
    }
}

// ---- world::UpdateHotkeys -----------------------------------------------------------------------------------------------------
//   bool KeyPressed(int code)                whether the key was pressed in this update
//   void ShowPause() / ClosePause()         shows or closes the pause dialog
//   void TogglePanel(Panel)                 toggles the GUI panel
//   int ZoomLevel()                         the renderer's zoom level
//   void SetPaused(bool) / SetGameSpeed(int) Submit semantic commands; no world writes before the next tick
// The panels are tested in the order below (pause, F5, F4, F2, M, B, S, zoom, speed), so a frame with several keys calls them in that order.
// The speed level is clamped on the way up (`< 2`) and down (`!= 0`); the executor rejects an invalid requested level.
template<class Env> void UpdateHotkeys(const world::World& world, ui::ViewState& view, Env& env)
{
    if (env.KeyPressed(kKeyPause) && world.m_forcedPaused == 0) {
        const bool paused = world.m_paused == 0;
        env.SetPaused(paused);
        if (paused)
            env.ShowPause();
        else
            env.ClosePause();
    }
    if (env.KeyPressed(kKeyLoadMenu)) env.TogglePanel(Panel::LoadGame);
    if (env.KeyPressed(kKeySaveMenu)) env.TogglePanel(Panel::SaveGame);
    if (env.KeyPressed(kKeyOptions)) env.TogglePanel(Panel::Options);
    if (env.KeyPressed(kKeyMusic)) env.TogglePanel(Panel::Music);
    if (env.KeyPressed(kKeyBuild)) env.TogglePanel(Panel::Build);
    if (env.KeyPressed(kKeyScenery)) env.TogglePanel(Panel::Scenery);
    if (env.KeyPressed(kKeyZoomIn)) {
        if (env.ZoomLevel() == 1)
            view.m_zoomRequest = 0;
        else if (env.ZoomLevel() == 2)
            view.m_zoomRequest = 1;
    }
    if (env.KeyPressed(kKeyZoomOut)) {
        if (env.ZoomLevel() == 0)
            view.m_zoomRequest = 1;
        else if (env.ZoomLevel() == 1)
            view.m_zoomRequest = 2;
    }
    int level = world.m_gameSpeedLevel;
    if (env.KeyPressed(kKeySpeedUp)) {
        if (static_cast<uint32_t>(level) < 2) ++level;
        env.SetGameSpeed(level);
    }
    if (env.KeyPressed(kKeySpeedDown)) {
        if (level != 0) --level;
        env.SetGameSpeed(level);
    }
}

// ---- world::Draw and world::DrawObjectOverlays ---------------------------------------------------------------------------
//   void ResetViewport() / DrawSea() / DarkenByDepth()      MapView::ResetViewport / MapView::DrawSea / MapView::DarkenByDepth
//   void DrawObjects(void** list, int count, uint32_t arg)  MapView::DrawObjects
//   void DrawRain()                                         weather::runtime::DrawRain
//   void SetActiveSurface(int)                              ui::renderer::SetActiveSurface
//   void SetRenderVisible(bool)                             the renderer's visibility flag
//   void SetMoving(bool)                                    moving::SetMoving of the renderer
//   void RenderVisible(void** list, int count)              MapView::RenderVisibleObjects
// The lists go buildings, "others" (scenery, staff, vehicles), guests; the counts are read when the list is drawn, not up front.
template<class Env> void Draw(world::World& world, const ui::ViewState& view, Env& env)
{
    object::Manager& objects = *world.m_objectManager;
    env.ResetViewport();
    env.DrawSea();
    const uint32_t arg = view.m_drawObjectsArg;
    env.DrawObjects(reinterpret_cast<void**>(objects.m_buildings.data()), static_cast<int>(objects.m_buildings.size()), arg);
    env.DrawObjects(reinterpret_cast<void**>(objects.m_others.data()), static_cast<int>(objects.m_others.size()), arg);
    env.DrawObjects(reinterpret_cast<void**>(objects.m_guests.data()), static_cast<int>(objects.m_guests.size()), arg);
    if (view.m_darkenByDepth) env.DarkenByDepth();
    env.DrawRain();
}

template<class Env> void DrawObjectOverlays(world::World& world, Env& env)
{
    object::Manager& objects = *world.m_objectManager;
    env.SetActiveSurface(0);
    env.SetRenderVisible(true);
    env.SetMoving(true);
    env.RenderVisible(reinterpret_cast<void**>(objects.m_buildings.data()), static_cast<int>(objects.m_buildings.size()));
    env.RenderVisible(reinterpret_cast<void**>(objects.m_others.data()), static_cast<int>(objects.m_others.size()));
    env.RenderVisible(reinterpret_cast<void**>(objects.m_guests.data()), static_cast<int>(objects.m_guests.size()));
    env.SetMoving(false);
    env.SetRenderVisible(false);
}

} // namespace obl::world_view
