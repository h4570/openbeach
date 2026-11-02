#pragma once

#include "game/tables/building_behavior.h"
#include "core/float_to_int.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/renderer_types.h"
#include "game/ui/sprite.h"
#include "game/world/world_state.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace obl::gui_manager_frame {

using View = ui::GuiManager;
using Node = ui::GuiManagerDialogNode;
using Dialog = ui::GuiDialogData;
using Widget = ui::GuiWidget;

inline constexpr int kCameraMode = 2; // GuiModeCamera: the HUD bar ignores the mouse, the world camera follows the cursor rectangle
inline constexpr int kDefaultMode = 4; // GuiModeDefault: the world camera always takes the input
inline constexpr int kNoGuiMode = 6; // no GUI mode: also removes the GUI bar (the renderer's GUI bar height goes to 0)
inline constexpr int kActiveDialog = 1; // GuiManagerDialogEntry::m_state of an open, not closing dialog
inline constexpr int kForcedButtonState = 3; // what the dialogs below a hovered or modal dialog see as both button states

// Whether SwitchMode builds a mode object: every mode but 3 and anything above 10.
inline constexpr uint32_t kModeCount = 11;
inline bool HasModeObject(int mode)
{
    return static_cast<uint32_t>(mode) < kModeCount && mode != 3;
}

inline Node* FindDialog(View* manager, const char* name)
{
    Node* head = manager->m_dialogs;
    Node* node = head->m_next;
    while (node != head && std::strcmp(node->m_entry->m_dialog->m_name, name) != 0) node = node->m_next;
    return node;
}

// ---- Small setters and queries ----

inline void SetFont(View* manager, int index, const void* font)
{
    manager->m_fonts[index] = font;
}
inline void SetTextRenderer(View* manager, int index, void* textRenderer)
{
    manager->m_textRenderers[index] = static_cast<ui::TextRenderer*>(textRenderer);
}

// The game's hardware cursor is shown exactly while the software one is off.
template<class Env> void SetSoftwareCursorEnabled(Env& env, View* manager, bool enabled)
{
    manager->m_softwareCursor = enabled;
    env.SetSystemCursorHidden(!enabled);
}

// Only a size different from the renderer's is remembered; the same size leaves an older pending request in place.
inline void RequestResolution(View* manager, int width, int height)
{
    const auto* renderer = static_cast<const ui::Renderer*>(manager->m_renderer);
    if (renderer->m_width == width && renderer->m_height == height) return;
    manager->m_resolutionRequested = 1;
    manager->m_requestedWidth = width;
    manager->m_requestedHeight = height;
}

inline bool IsRadarHovered(const View* manager)
{
    return manager->m_radarHovered != 0;
}
inline void HideProgressBar(View* manager)
{
    manager->m_progressVisible = 0;
}

template<class Env> bool IsMainItemHovered(Env& env, View* manager)
{
    return manager->m_mainItem && manager->m_mode != kCameraMode && env.WidgetHovered(manager->m_mainItem);
}

// Any active dialog under the cursor, then the HUD bar (not in camera mode), then the radar panel (only when asked) and the progress bar.
template<class Env> bool IsCursorOverGui(Env& env, View* manager, bool includeRadar)
{
    for (Node* node = manager->m_dialogs->m_next; node != manager->m_dialogs;) {
        auto* entry = node->m_entry;
        node = node->m_next;
        if (entry->m_state == kActiveDialog && env.DialogHovered(entry->m_dialog)) return true;
    }
    if (IsMainItemHovered(env, manager)) return true;
    if (includeRadar && manager->m_radarHovered) return true;
    return manager->m_progressHovered != 0;
}

// Camera mode: only while the cursor is inside the map view rectangle; default mode: always; other modes own the mouse.
template<class Env> bool CanUseWorldCameraInput(Env& env, View* manager)
{
    if (manager->m_mode == kCameraMode) return env.CursorInMapView();
    return manager->m_mode == kDefaultMode;
}

inline int32_t FollowOffset(int32_t size, float zoomScale)
{
    const double scale = static_cast<double>(zoomScale);
    return FloatToIntWrap(static_cast<double>(static_cast<uint32_t>(size)) / (scale + scale));
}

// Centres the camera on the followed object; an id that no longer exists stops following.
template<class Env> void UpdateFollowCamera(Env& env, View* manager)
{
    if (manager->m_selection == -1) return;
    const auto* object = env.GetObject(manager->m_selection);
    if (!object) {
        manager->m_selection = -1;
        return;
    }
    float world[3];
    std::memcpy(world, object->m_WorldXYZ, sizeof(world));
    int32_t x = 0, y = 0;
    env.WorldToInt(world, &x, &y);
    const auto* renderer = static_cast<const ui::Renderer*>(manager->m_renderer);
    x -= FollowOffset(renderer->m_width, renderer->m_zoomScale);
    y -= FollowOffset(renderer->m_height, renderer->m_zoomScale);
    env.SetCameraPos(x, y);
}

// ---- Tile outline ----

// The diamond of one tile: its own corner and the neighbours in directions 4, 2 and 3, each in screen pixels minus the camera, joined
// corner->dir4, corner->dir2, dir3->dir4, dir3->dir2. Zoom levels 0 and 1 draw at full size, 2 at half (signed division by 2), others
// draw nothing; the direct mode of the renderer is off around it. A tile on the far edges (neighbour 3 above, neighbour 4 left) is skipped.
template<class Env> void DrawTileOutline(Env& env, View* manager, uint32_t tileX, uint32_t tileY, uint32_t color)
{
    int32_t cameraX = 0, cameraY = 0;
    env.Camera(&cameraX, &cameraY);
    uint32_t right[2], below[2], across[2];
    env.Neighbour(tileX, tileY, 4, right);
    env.Neighbour(tileX, tileY, 2, below);
    env.Neighbour(tileX, tileY, 3, across);
    if (static_cast<int32_t>(across[1]) < static_cast<int32_t>(tileY)) return;
    if (static_cast<int32_t>(tileX) < static_cast<int32_t>(right[0])) return;
    const uint32_t tile[2] = {tileX, tileY};
    int32_t p0[2], p1[2], p2[2], p3[2];
    env.TileToScreen(tile, p0);
    env.TileToScreen(right, p1);
    env.TileToScreen(below, p2);
    env.TileToScreen(across, p3);
    void* renderer = manager->m_renderer;
    env.SetMoving(renderer, false);
    const int zoom = static_cast<const ui::Renderer*>(manager->m_renderer)->m_zoomLevel;
    if (zoom == 0 || zoom == 1 || zoom == 2) {
        const int32_t divisor = zoom == 2 ? 2 : 1;
        auto sx = [&](const int32_t* p) { return (p[0] - cameraX) / divisor; };
        auto sy = [&](const int32_t* p) { return (p[1] - cameraY) / divisor; };
        env.DrawLine(manager->m_renderer, sx(p0), sy(p0), sx(p1), sy(p1), color);
        env.DrawLine(manager->m_renderer, sx(p0), sy(p0), sx(p2), sy(p2), color);
        env.DrawLine(manager->m_renderer, sx(p3), sy(p3), sx(p1), sy(p1), color);
        env.DrawLine(manager->m_renderer, sx(p3), sy(p3), sx(p2), sy(p2), color);
    }
    env.SetMoving(manager->m_renderer, true);
}

// ---- Modes ----

// Requests a mode for the next SwitchMode. The placing modes (scenery, pavement, grass) roll out a minimized scenery panel and turn the
// panel buttons back to their "minimize" graphic; the GUI-less mode removes the GUI bar at once.
template<class Env> void SetMode(Env& env, View* manager, int mode)
{
    static constexpr const char* kPanel = "SceneryPanel";
    manager->m_requestedMode = mode;
    if (mode == 1 || mode == 7 || mode == 8) {
        Node* node = FindDialog(manager, kPanel);
        if (node == manager->m_dialogs || node->m_entry->m_dialog->m_minimized != 1) return;
        node = FindDialog(manager, kPanel);
        if (node != manager->m_dialogs) env.AnimateDialog(node->m_entry->m_dialog, 4, 1.0f, 0);
        node = FindDialog(manager, kPanel);
        if (node != manager->m_dialogs) node->m_entry->m_dialog->m_minimized = 0;
        for (auto* item = manager->m_widgets; item; item = item->m_next)
            if (item->m_widget->m_type == 0) env.SetButtonGraphic(item->m_widget, "MinSceneryPanel", "UI3_sc_in");
    } else if (mode == kNoGuiMode) {
        env.SetGuiBarHeight(manager->m_renderer, 0);
    }
}

// Applies the requested mode: the old mode object exits and is deleted, the new one is built (none for mode 3 and unknown modes), the
// mode number is stored, the new object enters and the bar surfaces follow.
// Our fix: a mode that should have an object but has none switches as well. The manager is built in mode 4 without its object, so a save
// loaded before the first SwitchMode (a fresh process loading at once; in the game the menu's frames always switched to mode 6 first) requested
// mode 4 = the current one and never got the default mode, whose update scrolls the message balloon: the feedback notices then queued
// differently from the same save loaded after play (docs/architecture/headless/index.md).
template<class Env> void SwitchMode(Env& env, View* manager)
{
    if (manager->m_requestedMode == manager->m_mode && (manager->m_modeObject || !HasModeObject(manager->m_mode))) return;
    if (manager->m_modeObject) {
        env.ModeExit(manager->m_modeObject);
        if (manager->m_modeObject) env.ModeDelete(manager->m_modeObject);
        manager->m_modeObject = nullptr;
    }
    const int mode = manager->m_requestedMode;
    if (HasModeObject(mode)) {
        manager->m_modeObject = env.NewMode(manager, mode);
        if (mode == kNoGuiMode) env.SetGuiBarHeight(manager->m_renderer, 0);
    }
    manager->m_mode = manager->m_requestedMode;
    if (manager->m_modeObject) env.ModeEnter(manager->m_modeObject);
    env.SyncBarSurfaces(manager);
}

// ---- Mouse ----

// GuiManager::UpdateProgressBarMouse: hover box of the progress bar and the mouse for its one or two widgets.
template<class Env> void UpdateProgressBarMouse(Env& env, View* manager, int x, int y, int state)
{
    manager->m_progressHovered = 0;
    if (!manager->m_progressVisible) return;
    if (x <= manager->m_progressX1 && manager->m_progressX0 <= x && y <= manager->m_progressY1 && manager->m_progressY0 <= y)
        manager->m_progressHovered = 1;
    env.WidgetMouse(manager->m_progressButton, x, y, state);
    if (manager->m_progressDialog) env.WidgetMouse(manager->m_progressDialog, x, y, state);
}

// One frame of mouse input. The wheel goes to the front dialog (if active) or else the mode object. Then the active dialogs get the left
// button front to back until one takes it; a hovered or modal dialog makes every later receiver see state 3 for both buttons. A dialog
// that took the click moves to the front unless any dialog in the list is modal. The HUD bar (not in camera mode), the radar, the progress
// bar and finally the mode object (only when nobody took the click) follow.
template<class Env> void UpdateMouse(Env& env, View* manager)
{
    const int x = env.CursorX();
    const int y = env.CursorY();
    const int wheel = env.Wheel();
    int left = env.LeftButton();
    int right = env.RightButton();
    if (wheel != 0) {
        Node* first = manager->m_dialogs->m_next;
        bool taken = false;
        if (first != manager->m_dialogs && first->m_entry->m_state == kActiveDialog) {
            manager->m_variableProvider = first->m_entry->m_provider;
            taken = env.DialogWheel(first->m_entry->m_dialog, x, y, wheel);
        }
        if (!taken && manager->m_modeObject) env.ModeWheel(manager->m_modeObject, x, y, wheel);
    }

    bool taken = false, modal = false;
    Node* hit = nullptr;
    for (Node* node = manager->m_dialogs->m_next; node != manager->m_dialogs; node = node->m_next) {
        if (taken) break;
        auto* entry = node->m_entry;
        if (entry->m_state != kActiveDialog) continue;
        Dialog* dialog = entry->m_dialog;
        if (dialog->m_modal) modal = true;
        manager->m_variableProvider = entry->m_provider;
        taken = env.DialogMouse(dialog, x, y, left);
        if (taken) hit = node;
        if (env.DialogHovered(dialog) || modal) left = right = kForcedButtonState;
    }
    if (taken && hit != manager->m_dialogs->m_next) {
        bool anyModal = false;
        for (Node* node = manager->m_dialogs->m_next; node != manager->m_dialogs && !anyModal; node = node->m_next)
            if (node->m_entry->m_dialog->m_modal) anyModal = true;
        if (!anyModal) env.MoveDialogToFront(manager, hit);
    }

    if (manager->m_mode != kCameraMode && manager->m_mainItem && !taken) taken = env.WidgetMouseLeft(manager->m_mainItem, x, y, left);
    if (manager->m_mode != kCameraMode && manager->m_mainItem) env.WidgetMouseRight(manager->m_mainItem, x, y, right);
    env.RadarMouse(manager, x, y, left);
    env.ProgressBarMouse(manager, x, y, left);
    if (manager->m_modeObject && !taken) env.ModeMouse(manager->m_modeObject, x, y, left, right);
}

// ---- Credits ----

template<class Env> ui::GuiCredits* ConstructCredits(Env& env, ui::GuiCredits* manager)
{
    manager->m_nextEntry = 0;
    auto* sentinel = static_cast<ui::GuiCreditsNode*>(env.AllocateNode());
    sentinel->m_next = sentinel;
    sentinel->m_previous = sentinel;
    manager->m_lines = sentinel;
    manager->m_endTimer = 0;
    manager->m_ending = 0;
    return manager;
}

inline void ResetCredits(ui::GuiCredits* manager)
{
    manager->m_nextEntry = 0;
    manager->m_delay = 0;
}

// GuiCredits::Destruct: deletes every line, gives the nodes and then the sentinel back to the pool.
template<class Env> void DestructCredits(Env& env, ui::GuiCredits* manager)
{
    ui::GuiCreditsNode* sentinel = manager->m_lines;
    for (auto* node = sentinel->m_next; node != sentinel; node = node->m_next) env.DeleteLine(node->m_line);
    for (auto* node = sentinel->m_next; node != sentinel;) {
        auto* next = node->m_next;
        env.FreeNode(node);
        node = next;
    }
    sentinel->m_next = sentinel;
    sentinel->m_previous = sentinel;
    env.FreeNode(sentinel);
}

// The credits object of the end-of-game roll: built and reset here, its lines are added later by GuiManager::Update.
template<class Env> void CreateCredits(Env& env, View* manager)
{
    auto* credits = env.NewCredits();
    manager->m_credits = credits;
    env.ResetCredits(credits);
}

template<class Env> void DestroyCredits(Env& env, View* manager)
{
    auto* credits = manager->m_credits;
    if (!credits) return;
    env.DestructCredits(credits);
    env.FreeCredits(credits);
    manager->m_credits = nullptr;
}

inline constexpr int32_t kCreditLastKind = 1;
inline constexpr float kCreditEndSeconds = 5.0f;
inline constexpr float kCreditScrollRate = 30.0f; // pixels per second

// GuiCredits::IsFinished: the end timer is running and has run out.
inline bool IsCreditsFinished(const ui::GuiCredits* manager)
{
    return manager->m_ending != 0 && manager->m_endTimer < 0.0f;
}

template<class Env> void AddCreditsLine(Env& env, ui::GuiCredits* manager)
{
    const auto entries = env.CreditEntries();
    if (manager->m_nextEntry >= entries.size()) return;
    const ui::GuiCreditEntry& entry = entries[manager->m_nextEntry];
    char key[128];
    std::snprintf(key, sizeof(key), "CREDIT_ENTRY_%d", entry.m_number);
    auto* line = static_cast<ui::GuiCreditsLine*>(env.NewLine());
    line->m_text = nullptr;
    auto* text = static_cast<ui::GuiTextData*>(env.NewText(key, entry.m_font, entry.m_color));
    text->m_drawOnSurface = 0; // drawn every frame: the line moves
    line->m_text = text;
    line->m_kind = entry.m_kind;
    manager->m_delay = entry.m_long ? 0x1e : 0xf;
    auto* sentinel = manager->m_lines;
    auto* node = static_cast<ui::GuiCreditsNode*>(env.AllocateNode());
    node->m_line = line;
    node->m_next = sentinel;
    node->m_previous = sentinel->m_previous;
    sentinel->m_previous->m_next = node;
    sentinel->m_previous = node;
    ++manager->m_nextEntry;
}

// GuiCredits::Update: every line moves up by dt * 30 px (the float product plus 1.0, truncated); a line whose bottom edge is
// above the screen is deleted and unlinked (the last one, kind 1, starts the 5 s end timer). Then the delay to the next line runs down
// by the same step and adds the line when it goes negative; the end timer counts down by dt.
template<class Env> void UpdateCredits(Env& env, ui::GuiCredits* manager, float dt)
{
    const float step = static_cast<float>(static_cast<double>(kCreditScrollRate) * static_cast<double>(dt));
    const int pixels = FloatToIntWrap(static_cast<double>(step) + 1.0);
    ui::GuiCreditsNode* sentinel = manager->m_lines;
    ui::GuiCreditsNode* node = sentinel->m_next;
    while (node != sentinel) {
        ui::GuiWidget* text = node->m_line->m_text;
        int32_t bounds[4];
        env.WidgetBounds(text, bounds);
        ui::GuiCreditsNode* next = node->m_next;
        if (bounds[3] < 0) {
            if (node->m_line->m_kind == kCreditLastKind) {
                manager->m_ending = 1;
                manager->m_endTimer = kCreditEndSeconds;
            }
            if (text) env.DeleteWidget(text);
            node->m_previous->m_next = node->m_next;
            node->m_next->m_previous = node->m_previous;
            env.FreeNode(node);
        } else {
            env.WidgetBounds(text, bounds);
            env.WidgetSetPosition(text, bounds[0], bounds[1] - pixels);
        }
        node = next;
    }
    manager->m_delay -= pixels;
    if (manager->m_delay < 0) AddCreditsLine(env, manager);
    if (manager->m_ending) manager->m_endTimer = static_cast<float>(static_cast<double>(manager->m_endTimer) - static_cast<double>(dt));
}

// ---- Options shortcut ----

// Opens the in-game options and selects their second tab (video).
template<class Env> void OpenVideoOptions(Env& env, View* manager)
{
    static constexpr const char* kOptions = "InGameOptions";
    env.ShowDialog(manager, kOptions, nullptr, false);
    Node* node = FindDialog(manager, kOptions);
    if (node != manager->m_dialogs) env.SelectTab(node->m_entry->m_dialog, 1);
}

// ---- Bar surfaces, drawing and the per-frame update ----

inline constexpr uint32_t kWideScreenWidth = 800; // from this width on the bottom bar is the tall one
inline constexpr float kRadarSpriteTime = 99999.0f;
inline constexpr uint32_t kRadarSpriteDepth = 0xffff;
inline constexpr const char* kExtrasScreen = "ExtrasScreen";

// The bottom bar's surface height and the height the renderer keeps free for it (the comparison with 800 is unsigned); the no-GUI mode keeps both.
template<class Env> void SizeBar(Env& env, View* manager, ui::Renderer* renderer)
{
    if (manager->m_mode == kNoGuiMode) return;
    const bool wide = static_cast<uint32_t>(renderer->m_width) >= kWideScreenWidth;
    manager->m_barHeight = wide ? 0x5e : 0x4b;
    env.SetGuiBarHeight(renderer, wide ? 0x58 : 0x46);
}

// GuiManager::SyncBarSurfaces, run by every LoadScript and by SwitchMode: sizes the bottom bar to the screen width; when the
// surface really changed, every surface the GUI draws on is recreated and the widgets that cache their picture redraw.
template<class Env> void SyncBarSurfaces(Env& env, View* manager)
{
    auto* renderer = static_cast<ui::Renderer*>(manager->m_renderer);
    SizeBar(env, manager, renderer);
    if (!env.ResizeSurface(renderer, manager->m_barSurface, renderer->m_width, manager->m_barHeight)) return;
    env.ClearSurface(renderer, manager->m_barSurface);
    env.SetSurfaceOrigin(renderer, manager->m_barSurface, 0, renderer->m_height - manager->m_barHeight);
    for (int surface : {manager->m_cursorSurface, manager->m_radarSurface, manager->m_progressSurface}) {
        env.RecreateSurface(renderer, surface);
        env.ClearSurface(renderer, surface);
    }
    for (Node* node = manager->m_dialogs->m_next; node != manager->m_dialogs; node = node->m_next) {
        env.RecreateSurface(renderer, node->m_entry->m_surface);
        env.ClearSurface(renderer, node->m_entry->m_surface);
    }
    if (manager->m_radarWidget) {
        env.WidgetRedraw(manager->m_radarWidget);
        env.WidgetRedraw(manager->m_radarButton);
        manager->m_radarDirty = 1;
    }
    env.WidgetRedraw(manager->m_progressButton);
    manager->m_progressDirty = 1;
}

// A surface the display lost is restored and cleared; the caller then redraws whatever lives on it.
template<class Env> bool RestoreLostSurface(Env& env, void* renderer, int surface)
{
    if (!env.IsSurfaceLost(renderer, surface)) return false;
    env.RestoreSurface(renderer, surface);
    env.ClearSurface(renderer, surface);
    return true;
}

// GuiManager::DrawSurfaces: the radar panel, the progress bar, the mode object, the HUD bar, every active dialog from the last
// list entry (the one behind) to the first (the front one), then the software cursor. The renderer's direct mode is on only here.
template<class Env> void DrawSurfaces(Env& env, View* manager)
{
    void* renderer = manager->m_renderer;
    if (env.HasWorld()) env.SetMoving(renderer, true);

    if (RestoreLostSurface(env, renderer, manager->m_barSurface) && manager->m_mainItem) env.WidgetRedraw(manager->m_mainItem);
    if (RestoreLostSurface(env, renderer, manager->m_radarSurface)) {
        if (manager->m_radarWidget) {
            env.WidgetRedraw(manager->m_radarWidget);
            env.WidgetRedraw(manager->m_radarButton);
        }
        manager->m_radarDirty = 1;
    }
    if (RestoreLostSurface(env, renderer, manager->m_progressSurface)) {
        if (manager->m_progressDialog) env.WidgetRedraw(manager->m_progressDialog);
        env.WidgetRedraw(manager->m_progressButton);
        manager->m_progressDirty = 1;
    }
    RestoreLostSurface(env, renderer, manager->m_cursorSurface);

    if (manager->m_radarEnabled) {
        env.SetActiveSurface(renderer, manager->m_radarSurface);
        if (manager->m_radarDirty) {
            manager->m_radarDirty = 0;
            env.DrawSprite(manager->m_radarSprite, manager->m_panelX0, manager->m_panelY0, kRadarSpriteDepth, kRadarSpriteTime);
        }
        env.WidgetDraw(manager->m_radarButton);
        if (manager->m_radarBig) env.WidgetDraw(manager->m_radarWidget);
    }
    env.DrawProgressBar(manager);
    if (manager->m_modeObject) env.ModeDraw(manager->m_modeObject);
    if (manager->m_mainItem) {
        env.SetActiveSurface(renderer, manager->m_barSurface);
        env.WidgetDraw(manager->m_mainItem);
    }

    for (Node* node = manager->m_dialogs->m_previous; node != manager->m_dialogs; node = node->m_previous) {
        auto* entry = node->m_entry;
        if (entry->m_state != kActiveDialog) continue;
        manager->m_variableProvider = entry->m_provider;
        if (env.IsSurfaceLost(renderer, entry->m_surface)) {
            env.RestoreSurface(renderer, entry->m_surface);
            env.ClearSurface(renderer, entry->m_surface);
            env.WidgetRedraw(entry->m_dialog);
        }
        env.SetActiveSurface(renderer, entry->m_surface);
        int x, y;
        env.DialogOrigin(entry->m_dialog, &x, &y);
        env.SetSurfaceOrigin(renderer, entry->m_surface, x, y);
        env.WidgetDraw(entry->m_dialog);
        if (!env.HasWorld()) {
            // Outside gameplay (the main menu) nothing else ticks the dialog, so it also gets its two refresh slots here.
            env.DialogRefresh(entry->m_dialog);
            env.WidgetRedraw(entry->m_dialog);
        }
    }

    if (!env.SystemCursorSetting() && !env.SystemCursorHidden()) {
        env.SetActiveSurface(renderer, manager->m_cursorSurface);
        env.RenderCursor();
    }
    env.SetMoving(renderer, false);
}

// GuiManager::Update, once per frame with the frame time: mode switch, the HUD bar, dialogs and mouse, the pending resolution
// change, the tooltip delay, the music player and the message list, the radar and progress widgets, the credits roll.
template<class Env> void Update(Env& env, View* manager, float dt)
{
    env.SwitchMode(manager);
    manager->m_variableProvider = nullptr;
    if (manager->m_mainItem) env.WidgetUpdate(manager->m_mainItem, dt);
    env.UpdateDialogs(manager, dt);
    env.UpdateMouse(manager);
    if (manager->m_modeObject) env.ModeUpdate(manager->m_modeObject, dt);

    if (manager->m_resolutionRequested) {
        manager->m_requestedMode = kDefaultMode;
        env.ChangeResolution(manager->m_requestedWidth, manager->m_requestedHeight);
        env.SetSettingsResolution(manager->m_requestedWidth, manager->m_requestedHeight);
        env.ClearDialogPositions();
        env.RequestGuiReload();
        env.RandomizeRainDrops();
        manager->m_resolutionRequested = 0;
        env.UpdateRadarViewRect(manager);
        env.RelayoutProgressBar(manager);
    }

    if (0.0f < manager->m_tooltipCooldown) {
        const double left = static_cast<double>(manager->m_tooltipCooldown) - static_cast<double>(dt);
        manager->m_tooltipCooldown = static_cast<float>(left);
        if (left < 0.0) manager->m_tooltipCooldown = 0.0f;
    }
    env.UpdateMusicPlayer(manager->m_musicPlayer);
    env.UpdateTickerList(manager->m_tickerList);
    if (manager->m_radarEnabled) {
        env.WidgetUpdate(manager->m_radarButton, dt);
        env.ShiftRadarViewRect(manager);
    }
    if (manager->m_progressVisible) env.WidgetUpdate(manager->m_progressButton, dt);

    if (manager->m_credits) {
        env.UpdateCredits(manager->m_credits, dt);
        if (env.CreditsFinished(manager->m_credits)) {
            DestroyCredits(env, manager);
            // The roll is over: every dialog closes, remembering where the player left it, and the extras screen opens.
            for (Node* node = manager->m_dialogs->m_next; node != manager->m_dialogs; node = node->m_next) {
                auto* entry = node->m_entry;
                entry->m_state = 2;
                const int key = entry->m_dialog->m_positionKey;
                int x, y;
                env.DialogOrigin(entry->m_dialog, &x, &y);
                if (manager->m_mode != kNoGuiMode && manager->m_requestedMode != kNoGuiMode) env.StoreDialogPosition(x, y, key);
                if (entry->m_provider) env.ProviderClosed(entry->m_provider);
            }
            env.ShowDialog(manager, kExtrasScreen, nullptr, false);
        }
    }
}

// ---- LoadScript ----

inline constexpr uint32_t kFontSlots = 10;
inline constexpr uint8_t kStaffClasses[] = {9, 0xb, 8, 7, 0xc, 0xa}; // order of the button refresh
inline constexpr uint32_t kSmallScreenWidth = 800;
inline constexpr uint8_t kButtonType = 0;
inline constexpr const char* kFerryPanelAction = "FerryPanel";

template<class Env> void ReleaseDialogEntries(Env& env, View* manager)
{
    Node* head = manager->m_dialogs;
    for (Node* node = head->m_next; node != head; node = node->m_next) {
        ui::GuiManagerDialogEntry* entry = node->m_entry;
        if (!entry) continue;
        env.SetDialogOpen(entry->m_dialog, false);
        env.ReleaseSurface(entry->m_surface);
        env.DeleteEntry(entry);
    }
    for (Node* node = head->m_next; node != head;) {
        Node* next = node->m_next;
        env.FreeNode(node);
        node = next;
    }
    head->m_next = head;
    head->m_previous = head;
}

// The script's widget list and its dialog definitions; detach the definitions' dialogs before destroying their owning entries.
template<class Env> void DestroyWidgetLists(Env& env, View* manager)
{
    while (manager->m_widgets) {
        auto* node = manager->m_widgets;
        auto* next = node->m_next;
        env.DestroyListNode(node);
        env.Delete(node);
        manager->m_widgets = next;
    }
    while (manager->m_definitions) {
        auto* node = manager->m_definitions;
        auto* next = node->m_next;
        node->m_dialog = nullptr;
        env.DestroyListNode(node);
        env.Delete(node);
        manager->m_definitions = next;
    }
}

// Every font slot in use: the font and its text renderer (only looked at when the slot has a font). LoadScript also empties the slots.
template<class Env> void ReleaseFonts(Env& env, View* manager, bool clearSlots)
{
    for (uint32_t i = 0; i < kFontSlots; ++i) {
        if (!manager->m_fonts[i]) continue;
        env.ReleaseFont(manager->m_fonts[i]);
        env.Delete(const_cast<void*>(manager->m_fonts[i]));

        if (manager->m_textRenderers[i]) env.Delete(manager->m_textRenderers[i]);
        if (clearSlots) {
            manager->m_fonts[i] = nullptr;
            manager->m_textRenderers[i] = nullptr;
        }
    }
}

template<class Env> void LoadScript(Env& env, View* manager, const char* path)
{
    ReleaseDialogEntries(env, manager);
    manager->m_variableProvider = nullptr;
    env.MusicPanelVisible(manager->m_musicPlayer, false);
    env.TickerVisible(manager->m_tickerList, false);
    DestroyWidgetLists(env, manager);
    ReleaseFonts(env, manager, true);
    if (manager->m_mainItem) env.DeleteWidget(manager->m_mainItem);
    manager->m_widgets = nullptr;
    manager->m_definitions = nullptr;
    manager->m_mainItem = nullptr;
    manager->m_context[0] = 0;
    manager->m_context[7] = 0;
    manager->m_context[11] = 0;
    manager->m_context[12] = -1;
    manager->m_context[1] = 1;
    manager->m_context[2] = 0x11;
    manager->m_context[10] = 2;

    uint32_t size = 0;
    void* text = env.ReadLayout(path, size);
    void* script = env.NewScript(manager);
    env.ParseScript(script, text, size);
    manager->m_definitions = static_cast<ui::GuiManagerDefinitionNode*>(env.ScriptDialogs(script));
    manager->m_widgets = static_cast<ui::GuiManagerWidgetNode*>(env.ScriptWidgets(script));
    if (script) env.Delete(script); // empty destructor first
    env.Delete(text);
    if (manager->m_mainItem) {
        env.SetDialogOpen(manager->m_mainItem, true);
        env.WidgetRedraw(manager->m_mainItem);
    }

    if (env.HasWorld()) {
        for (const uint8_t staffClass : kStaffClasses) env.UpdateStaffButtons(manager, staffClass, env.CountByClass(staffClass) != 0);
        if (env.HasWorld()) {
            const bool ship = env.CountBuildings(BuildingBehavior::ShipStop) != 0;
            const bool small = env.ScreenWidth() < kSmallScreenWidth;
            const char* sprite =
                    ship ? (small ? "UI2_bar_blob_ship" : "UI3_bar_blob_ship") : (small ? "UI2_bar_blob_coach" : "UI3_bar_blob_coach");
            for (auto* node = manager->m_widgets; node; node = node->m_next)
                if (node->m_widget->m_type == kButtonType) env.SetButtonGraphic(node->m_widget, kFerryPanelAction, sprite);
        }
    }
    env.SyncBarSurfaces(manager);
    env.ActivateProgressDialog(manager);
    if (manager->m_balloon) {
        env.DeleteWidget(manager->m_balloon);
        manager->m_balloon = nullptr;
        manager->m_balloonActive = 0;
    }
}

// ---- Construct / Destruct ----

inline constexpr int32_t kCursorSurfaceSize = 0x20;

// A new surface, cleared once (the bar, the cursor, the radar and progress panels are all created this way).
template<class Env> int32_t NewClearedSurface(Env& env, ui::Renderer* renderer, int width, int height)
{
    const int32_t surface = env.CreateSurface(renderer, width, height, 0);
    env.ClearSurface(renderer, surface);
    return surface;
}

// The size of the current frame of a panel sprite, which the panel's surface takes.
inline void SpriteFrameSize(const ui::SpriteInstance* sprite, int* width, int* height)
{
    const ui::SpriteAnim* anim = sprite->m_sprite->m_anims[sprite->m_animIndex];
    *width = anim->m_width;
    *height = anim->m_height;
}

// GuiManager::CreateBarSurfaces, once from Construct: the bottom bar (as wide as the screen, placed at its bottom edge), the 32x32
// software cursor and the radar and progress panels, each as big as its "panel down" sprite (a sprite instance made only to read the size).
template<class Env> bool CreateBarSurfaces(Env& env, View* manager)
{
    auto* renderer = static_cast<ui::Renderer*>(manager->m_renderer);
    SizeBar(env, manager, renderer);
    manager->m_barSurface = NewClearedSurface(env, renderer, renderer->m_width, manager->m_barHeight);
    env.SetSurfaceOrigin(renderer, manager->m_barSurface, 0, renderer->m_height - manager->m_barHeight);
    manager->m_cursorSurface = NewClearedSurface(env, renderer, kCursorSurfaceSize, kCursorSurfaceSize);
    int width, height;
    ui::SpriteInstance* sprite = env.CreateSprite(env.RadarPanelSpriteName());
    SpriteFrameSize(sprite, &width, &height);
    manager->m_radarSurface = NewClearedSurface(env, renderer, width, height);
    env.ReleaseSprite(sprite);
    sprite = env.CreateSprite(env.ProgressPanelSpriteName());
    SpriteFrameSize(sprite, &width, &height);
    manager->m_progressSurface = NewClearedSurface(env, renderer, width, height);
    env.ReleaseSprite(sprite);
    return true;
}

template<class Env> View* Construct(Env& env, View* manager)
{
    env.SetManager(manager);
    manager->m_dialogs = nullptr;
    auto* head = static_cast<Node*>(env.AllocateListHead());
    head->m_next = head;
    head->m_previous = head;
    manager->m_dialogs = head;
    manager->m_radarBig = 1;
    manager->m_radarDirty = 1;
    manager->m_radarMouseArmed = 1;
    manager->m_widgets = nullptr;
    manager->m_definitions = nullptr;
    manager->m_mainItem = nullptr;
    manager->m_radarEnabled = 0;
    manager->m_radarWidget = nullptr;
    manager->m_radarButton = nullptr;
    manager->m_radarSprite = nullptr;
    manager->m_renderer = static_cast<ui::Renderer*>(env.Renderer());
    manager->m_softwareCursor = 1;
    manager->m_modeObject = nullptr;
    manager->m_balloonActive = 0;
    manager->m_balloon = nullptr;
    manager->m_selection = -1;
    manager->m_resolutionRequested = 0;
    for (int32_t& slot : manager->m_context) slot = 0;
    manager->m_context[4] = -1;
    for (uint32_t i = 0; i < kFontSlots; ++i) {
        manager->m_fonts[i] = nullptr;
        manager->m_textRenderers[i] = nullptr;
    }
    manager->m_requestedMode = kNoGuiMode;
    manager->m_mode = kDefaultMode;
    env.CreateBarSurfaces(manager);
    manager->m_scriptTextKey = nullptr;
    manager->m_messageTextKey = nullptr;
    manager->m_variableProvider = nullptr;

    manager->m_tooltip = env.NewTooltip();
    manager->m_tooltipOwner = nullptr;
    manager->m_tooltipCooldown = 0.0f;

    manager->m_musicPlayer = env.NewMusicPlayer();
    manager->m_tickerList = env.NewTickerList();
    manager->m_cameraGrabRequested = 0;
    manager->m_cameraGrabSurface = -1;
    env.CreateProgressBar(manager);
    manager->m_credits = nullptr;
    return manager;
}

template<class Env> void Destruct(Env& env, View* manager)
{
    ReleaseDialogEntries(env, manager);
    manager->m_variableProvider = nullptr;
    if (manager->m_balloon) env.DeleteWidget(manager->m_balloon);
    DestroyWidgetLists(env, manager);
    ReleaseFonts(env, manager, false);
    if (manager->m_mainItem) env.DeleteWidget(manager->m_mainItem);
    if (manager->m_modeObject) {
        env.ModeExit(manager->m_modeObject);
        if (manager->m_modeObject) env.ModeDelete(manager->m_modeObject);
    }
    if (void* tooltip = manager->m_tooltip) {
        env.DestructTooltip(tooltip);
        env.Delete(tooltip);
    }
    if (void* player = manager->m_musicPlayer) {
        env.DestructMusicPlayer(player);
        env.Delete(player);
    }
    if (manager->m_tickerList) env.Delete(manager->m_tickerList); // empty destructor first
    if (manager->m_radarWidget) env.DeleteWidget(manager->m_radarWidget);
    if (manager->m_radarSprite) env.ReleaseSprite(manager->m_radarSprite);
    if (manager->m_radarButton) env.DeleteWidget(manager->m_radarButton);
    env.ReleaseSprite(manager->m_progressSprite);
    if (manager->m_progressButton) env.DeleteWidget(manager->m_progressButton);
    if (ui::GuiCredits* credits = manager->m_credits) {
        env.DestructCreditsObject(credits);
        env.Delete(credits);
    }
    // The list is empty after ReleaseDialogEntries, so only the sentinel is left to free.
    env.FreeListHead(manager->m_dialogs);
    env.SetManager(nullptr);
}

} // namespace obl::gui_manager_frame
