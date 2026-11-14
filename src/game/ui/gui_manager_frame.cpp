#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_modes_types.h"
#include "game/ui/gui_widgets_leaves.h"
#include "game/ui/gui_tooltip.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/renderer_api.h"
#include "game/ui/line_draw.h"
#include "game/ui/gui_widgets.h"
#include "game/game/game_api.h"
#include "game/ui/sprite.h"
#include "game/game/settings_store.h"
#include "game/ui/gui_controls.h"
#include "game/ui/own_controls.h"
#include "game/ui/gui_containers.h"
#include "game/ui/gui_tables.h"
#include "game/ui/gui_data.h"
#include "game/objects/object_registry_api.h"
#include "game/map/grid_state.h"
#include "game/weather/weather.h"
#include "game/ui/gui_manager_buttons.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_radar.h"
#include "game/ui/cursor.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/resolutions.h"
#include "game/ui/gui_modes_place.h"
#include "game/ui/gui_script_parse.h"
#include "game/ui/gui_script_state.h"
#include "game/ui/gui_modes_tools.h"
#include "game/ui/gui_action_entries.h"
#include "game/ui/music_player.h"
#include "game/ui/image.h"
#include "game/game/game_accessors.h"
#include "game/ui/map_view_access.h"
#include "game/ui/gui_content_runtime.h"
#include "game/ui/gui_layouts_runtime.h"
#include "game/ui/gui_definitions.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_manager_queries.h"
#include "game/ui/sound_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/gui_script_types.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/gui_controls_types.h"
#include "game/ui/input_types.h"
#include "game/ui/map_view.h"
#include "game/objects/object_base.h"
#include "game/ui/renderer_types.h"
#include "game/ui/cursor_types.h"
#include "game/world/world_state.h"
#include "core/file_io.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <array>
#include <bit>
#include <cstring>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>
#include "game/ui/map_view_math.h"

namespace obl::gui {
namespace {
using namespace gui_manager_frame;
using ui::GuiCredits;
using ui::GuiCreditsNode;
using Entry = ui::GuiManagerDialogEntry;

// Call counters (`call-report ui.gui-manager-frame`).
test::CallCounter g_counter("ui.gui-manager-frame");

View* Gui()
{
    return game::GetGuiManager();
}
ui::MapView* MapView()
{
    return ui::GetMapView();
}

// ---- Thin wrappers over the game calls the frame code makes ----
void DrawLine(void* renderer, int x0, int y0, int x1, int y1, uint32_t color)
{
    ui::pixel_primitives::DrawLine(renderer, x0, y0, x1, y1, color);
}

void SetMoving(void* renderer, bool moving)
{
    static_cast<ui::Renderer*>(renderer)->m_directMode = moving ? 1 : 0;
}
void Animate(Dialog* dialog, int type, uint32_t speedBits, int delay)
{
    container::DialogStartAnimation(dialog, type, speedBits, delay);
}
void SetButtonGraphic(Widget* button, const char* action, const char* sprite)
{
    button_action_facade::SetGraphicByAction(static_cast<ui::GuiButton*>(button), action, sprite);
}
void SetGuiBarHeight(void* renderer, int height)
{
    obl::ui::renderer::SetGuiBarHeight(renderer, height);
}
void SyncBarSurfaces(View* manager)
{
    manager->SyncBarSurfaces();
}
void RadarMouse(View* manager, int x, int y, int state)
{
    manager->UpdateRadarMouse(x, y, state);
}
void ProgressBarMouse(View* manager, int x, int y, int state)
{
    manager->UpdateProgressBarMouse(x, y, state);
}
bool ShowDialog(View* manager, const char* name, void* provider, bool modal)
{
    return manager->ShowDialog(name, provider, modal);
}
void SetCursorHidden(void* cursor, bool hide)
{
    CursorSetSystemHidden(static_cast<ui::SoftwareCursor*>(cursor), hide);
}

// The ten mode constructors of SwitchMode by mode number (none for 3): the place modes and the tool modes.
ui::GuiMode* ConstructMode(int mode, View* manager)
{
    if (mode == 1 || mode == 5 || mode == 7 || mode == 8) return ConstructPlaceMode(mode, manager);
    return gui_modes::ConstructTool(mode, manager);
}

// ---- Wrappers of the surface, drawing and update calls (DrawSurfaces, SyncBarSurfaces, Update) ----
bool IsSurfaceLost(void* renderer, int surface)
{
    return obl::ui::renderer::IsSurfaceLost(renderer, surface);
}
void RestoreSurface(void* renderer, int surface)
{
    obl::ui::renderer::RestoreSurface(renderer, surface);
}
void ClearSurface(void* renderer, int surface)
{
    obl::ui::renderer::ClearSurface(renderer, surface);
}
void RecreateSurface(void* renderer, int surface)
{
    obl::ui::renderer::RecreateSurface(renderer, surface);
}
void SetActiveSurface(void* renderer, int surface)
{
    obl::ui::renderer::SetActiveSurface(renderer, surface);
}
void SetSurfaceOrigin(void* renderer, int surface, int x, int y)
{
    obl::ui::renderer::SetSurfaceOrigin(renderer, surface, x, y);
}
bool ResizeSurface(void* renderer, int surface, int width, int height)
{
    return obl::ui::renderer::ResizeSurface(renderer, surface, width, height);
}
void DrawSprite(void* sprite, int x, int y, uint32_t depth, uint32_t timeBits)
{
    (static_cast<ui::SpriteInstance*>(sprite))->DrawClipped(x, y, depth, std::bit_cast<float>(timeBits));
}
void DrawProgressBar(View* manager)
{
    manager->DrawProgressBar();
}
void RenderCursor()
{
    CursorRender(game::GetCursor());
}
void SwitchModeCall(View* manager)
{
    manager->SwitchMode();
}
void UpdateDialogsCall(View* manager, uint32_t dtBits)
{
    static_cast<ui::GuiManager*>(manager)->UpdateDialogs(std::bit_cast<float>(dtBits));
}
void UpdateMouseCall(View* manager)
{
    manager->UpdateMouse();
}
void RandomizeRainDropsCall()
{
    weather::runtime::RandomizeRainDrops();
}
void UpdateRadarViewRectCall(View* manager)
{
    manager->UpdateRadarViewRect();
}
void RelayoutProgressBarCall(View* manager)
{
    manager->RelayoutProgressBar();
}
void UpdateMusicPlayerCall(void* player)
{
    static_cast<audio::MusicPlayer*>(player)->Update();
}
void UpdateTickerListCall(void* ticker)
{
    static_cast<ui::TickerList*>(ticker)->Update();
}
void ShiftRadarViewRectCall(View* manager)
{
    manager->ShiftRadarViewRect();
}
void UpdateCreditsCall(void* credits, uint32_t dtBits)
{
    (static_cast<GuiCredits*>(credits))->Update(std::bit_cast<float>(dtBits));
}
bool CreditsFinishedCall(void* credits)
{
    return (static_cast<GuiCredits*>(credits))->IsFinished();
}

// ---- Wrappers of the lifetime calls (Construct, CreateBarSurfaces, Destruct) ----
void* AllocateCall(size_t size)
{
    return ::operator new(size);
}
void CreateBarSurfacesCall(View* manager)
{
    manager->CreateBarSurfaces();
}
// The records the manager owns: `new T()` value-initializes them before the old in-place constructor fills them.
static_assert(std::is_trivially_destructible_v<ui::GuiTooltip>);
static_assert(std::is_trivially_destructible_v<audio::MusicPlayer>);
static_assert(std::is_trivially_destructible_v<ui::TickerList>);
void* NewTooltipCall()
{
    return (new ui::GuiTooltip())->Construct(0);
}
void* NewMusicPlayerCall()
{
    return (new audio::MusicPlayer())->Construct();
}
void* NewTickerListCall()
{
    return TickerListConstruct(new ui::TickerList());
}
void CreateProgressBarCall(View* manager)
{
    manager->CreateProgressBar();
}
int32_t CreateSurfaceCall(void* renderer, int width, int height, int flags)
{
    return obl::ui::renderer::CreateSurface(renderer, width, height, flags);
}
ui::SpriteInstance* CreateSpriteCall(void* bank, const char* name)
{
    return (static_cast<ui::SpriteBank*>(bank))->CreateInstance(name);
}
void ReleaseSpriteCall(void* bank, void* sprite)
{
    (static_cast<ui::SpriteBank*>(bank))->ReleaseInstance(sprite);
}
void ReleaseSurfaceCall(void* renderer, int surface)
{
    obl::ui::renderer::ReleaseSurface(renderer, surface);
}
void DeleteCall(void* memory)
{
    ::operator delete(memory);
}
void DestroyListNodeCall(void* node)
{
    static_cast<ui::GuiWidgetListNode*>(node)->DestroyWidget();
}
void ReleaseFontCall(const void* font)
{
    ui::images::ReleaseSlot(static_cast<void**>(const_cast<void*>(font)));
}
void DestructTooltipCall(void* tooltip)
{
    static_cast<ui::GuiTooltip*>(tooltip)->Destruct();
}
void DestructMusicPlayerCall(void* player)
{
    static_cast<audio::MusicPlayer*>(player)->Destruct();
}
void DestructCreditsCall(void* credits)
{
    (static_cast<GuiCredits*>(credits))->Destruct();
}
// The list sentinel is a real node also in the driver's runs, so it is always freed.
void FreeNodesCall(void* node, int)
{
    ::operator delete(node);
}
// ---- The game side of gui_manager_frame.h ----
struct GameEnv {
    bool DialogHovered(Dialog* d) { return (static_cast<ui::GuiWidget*>(d))->IsHovered(); }
    bool DialogWheel(Dialog* d, int x, int y, int wheel) { return (static_cast<ui::GuiWidget*>(d))->MouseWheel(x, y, wheel); }
    bool DialogMouse(Dialog* d, int x, int y, int state) { return (static_cast<ui::GuiWidget*>(d))->MouseLeft(x, y, state); }
    void SelectTab(Dialog* d, int tab) { (static_cast<ui::GuiWidget*>(d))->SelectTab(tab); }
    bool WidgetHovered(Widget* w) { return (static_cast<ui::GuiWidget*>(w))->IsHovered(); }
    bool WidgetMouseLeft(Widget* w, int x, int y, int state) { return (static_cast<ui::GuiWidget*>(w))->MouseLeft(x, y, state); }
    void WidgetMouseRight(Widget* w, int x, int y, int state) { (static_cast<ui::GuiWidget*>(w))->MouseRight(x, y, state); }
    void WidgetMouse(Widget* w, int x, int y, int state) { (static_cast<ui::GuiWidget*>(w))->MouseLeft(x, y, state); }
    bool CursorInMapView()
    {
        const ui::MapView* map = MapView();
        return CursorIsInRect(game::GetCursor(), map->m_viewLeft, map->m_viewTop, map->m_viewWidth, map->m_viewHeight);
    }
    void SetSystemCursorHidden(bool hide) { SetCursorHidden(game::GetCursor(), hide); }
    const object::Base* GetObject(int id) { return object::object_manager::ObjectById(game::GetObjectManager(), id); }
    void WorldToInt(float* world, int32_t* x, int32_t* y) { ui::MapView::WorldToInt(world, x, y); }
    void SetCameraPos(int x, int y) { MapView()->SetCameraPos(x, y); }
    void Camera(int32_t* x, int32_t* y)
    {
        *x = MapView()->m_cameraX;
        *y = MapView()->m_cameraY;
    }
    void Neighbour(uint32_t x, uint32_t y, int direction, uint32_t* out)
    {
        const grid_math::TileXY next = game::GetGrid()->GetNeighbour(x, y, direction);
        out[0] = next.x;
        out[1] = next.y;
    }
    void TileToScreen(const uint32_t* tile, int32_t* out) { MapView()->TileToScreen(out, tile); }
    void SetMoving(void* renderer, bool moving) { gui::SetMoving(renderer, moving); }
    void DrawLine(void* renderer, int x0, int y0, int x1, int y1, uint32_t color) { gui::DrawLine(renderer, x0, y0, x1, y1, color); }
    void AnimateDialog(Dialog* d, int type, float speed, int delay) { Animate(d, type, std::bit_cast<uint32_t>(speed), delay); }
    void SetButtonGraphic(Widget* w, const char* action, const char* sprite) { gui::SetButtonGraphic(w, action, sprite); }
    void SetGuiBarHeight(void* renderer, int height) { gui::SetGuiBarHeight(renderer, height); }
    void ModeExit(ui::GuiMode* mode) { mode->Exit(); }
    void ModeDelete(ui::GuiMode* mode) { delete mode; }
    void ModeEnter(ui::GuiMode* mode) { mode->Enter(); }
    void ModeWheel(ui::GuiMode* mode, int x, int y, int wheel) { mode->OnMouseWheel(x, y, wheel); }
    void ModeMouse(ui::GuiMode* mode, int x, int y, int left, int right) { mode->OnMouse(x, y, left, right); }
    ui::GuiMode* NewMode(View* manager, int mode) { return ConstructMode(mode, manager); }
    void SyncBarSurfaces(View* manager) { gui::SyncBarSurfaces(manager); }
    int CursorX() { return game::GetInput()->m_x; }
    int CursorY() { return game::GetInput()->m_y; }
    int Wheel() { return game::GetInput()->m_wheel; }
    int LeftButton() { return game::GetInput()->m_leftButton; }
    int RightButton() { return game::GetInput()->m_rightButton; }
    void RadarMouse(View* manager, int x, int y, int state) { gui::RadarMouse(manager, x, y, state); }
    void ProgressBarMouse(View* manager, int x, int y, int state) { gui::ProgressBarMouse(manager, x, y, state); }

    void MoveDialogToFront(View* manager, Node* node)
    {
        Entry* entry = node->m_entry;
        node->m_previous->m_next = node->m_next;
        node->m_next->m_previous = node->m_previous;
        ::operator delete(node);
        auto* fresh = GuiAllocDialogNode(&entry);
        Node* head = manager->m_dialogs;
        Node* first = head->m_next;
        fresh->m_next = first;
        fresh->m_previous = head;
        first->m_previous = fresh;
        head->m_next = fresh;
    }
    void ShowDialog(View* manager, const char* name, void* provider, bool modal) { gui::ShowDialog(manager, name, provider, modal); }
    GuiCredits* NewCredits()
    {
        static_assert(std::is_trivially_destructible_v<GuiCredits>);
        return (new GuiCredits())->Construct();
    }
    void ResetCredits(GuiCredits* credits) { credits->Reset(); }
    void DestructCredits(GuiCredits* credits) { credits->Destruct(); }
    void FreeCredits(GuiCredits* credits) { delete credits; }
    void* AllocateNode() { return ::operator new(sizeof(GuiCreditsNode)); }
    void FreeNode(void* node) { ::operator delete(node); }

    void DeleteLine(void* line)
    {
        auto* credits = static_cast<ui::GuiCreditsLine*>(line);
        if (!credits) return;
        if (credits->m_text) DeleteWidget(credits->m_text);
        delete credits;
    }
    std::span<const ui::GuiCreditEntry> CreditEntries() { return gui_content::Credits(); }
    void* NewLine() { return new ui::GuiCreditsLine(); }
    void* NewText(const char* key, int font, uint32_t color)
    {
        return TextConstruct(new ui::GuiText(), 0, 0x262, 800, 0x271, key, font, color, 4);
    }
    void WidgetBounds(void* widget, int32_t* out) { (static_cast<ui::GuiWidget*>(widget))->GetBounds(out); }
    void WidgetSetPosition(void* widget, int x, int y) { (static_cast<ui::GuiWidget*>(widget))->SetPosition(x, y); }
    void DeleteWidget(void* widget) { gui::DestroyWidget(*static_cast<ui::GuiWidget*>(widget)); }

    // LoadScript.
    void SetDialogOpen(void* widget, bool open) { (static_cast<ui::GuiWidget*>(widget))->SetVisible(open); }
    void ReleaseSurface(int surface) { ReleaseSurfaceCall(game::GetRenderer(), surface); }
    void DeleteEntry(void* entry) { DeleteCall(entry); }
    void Delete(void* object) { DeleteCall(object); }
    void MusicPanelVisible(void* player, bool visible)
    {
        static_cast<audio::MusicPlayer*>(player)->OnPanelVisibilityChanged(visible ? 1 : 0);
    }
    void TickerVisible(void* ticker, bool visible) { static_cast<ui::TickerList*>(ticker)->SetVisible(visible ? 1 : 0); }
    void DestroyListNode(void* node) { DestroyListNodeCall(node); }
    void ReleaseFont(const void* font) { ReleaseFontCall(font); }

    void* ReadLayout(const char* file, uint32_t& size)
    {
        size = 0;
        if (!file) return nullptr;
        const std::string bytes = own_controls::Apply(gui_layouts::ReadScript(file));
        auto* text = static_cast<uint8_t*>(::operator new(bytes.size() + 1));
        std::memcpy(text, bytes.data(), bytes.size());
        text[bytes.size()] = file_io::kTerminator;
        size = static_cast<uint32_t>(bytes.size());
        return text;
    }
    void* NewScript(View* manager)
    {
        static_assert(std::is_trivially_destructible_v<ui::GuiScript>);
        return GuiScriptConstruct(new ui::GuiScript(), manager);
    }
    void ParseScript(void* script, void* text, uint32_t size)
    {
        GuiScriptParse(*static_cast<ui::GuiScript*>(script), static_cast<const char*>(text), size);
    }
    void* ScriptDialogs(void* script) { return GuiScriptDialogs(*static_cast<ui::GuiScript*>(script)); }
    void* ScriptWidgets(void* script) { return GuiScriptWidgets(*static_cast<ui::GuiScript*>(script)); }
    int CountByClass(uint8_t objectClass)
    {
        return object::object_manager::CountByClass(game::GetObjectManager(), static_cast<char>(objectClass));
    }
    int CountBuildings(BuildingBehavior behavior)
    {
        return static_cast<int>(obl::object_manager::CountBuildingsByBehavior(*game::GetObjectManager(), behavior));
    }
    void UpdateStaffButtons(View* manager, uint8_t staffType, bool hasStaff)
    {
        static_cast<ui::GuiManager*>(manager)->UpdateStaffButtons(staffType, hasStaff);
    }
    uint32_t ScreenWidth() { return static_cast<uint32_t>(game::GetRenderer()->m_width); }
    void SetButtonGraphic(void* button, const char* action, const char* sprite)
    {
        button_action_facade::SetGraphicByAction(static_cast<ui::GuiButton*>(button), action, sprite);
    }
    void ActivateProgressDialog(View* manager) { manager->ActivateProgressDialog(); }

    // Surfaces and drawing: the renderer calls go through the wrappers above; the widgets' virtual slots are called directly.
    bool HasWorld() { return game::GetWorld() != nullptr; }
    bool IsSurfaceLost(void* renderer, int surface) { return gui::IsSurfaceLost(renderer, surface); }
    void RestoreSurface(void* renderer, int surface) { gui::RestoreSurface(renderer, surface); }
    void ClearSurface(void* renderer, int surface) { gui::ClearSurface(renderer, surface); }
    void RecreateSurface(void* renderer, int surface) { gui::RecreateSurface(renderer, surface); }
    void SetActiveSurface(void* renderer, int surface) { gui::SetActiveSurface(renderer, surface); }
    void SetSurfaceOrigin(void* renderer, int surface, int x, int y) { gui::SetSurfaceOrigin(renderer, surface, x, y); }
    bool ResizeSurface(void* renderer, int surface, int width, int height) { return gui::ResizeSurface(renderer, surface, width, height); }
    void DrawSprite(void* sprite, int x, int y, uint32_t depth, float time)
    {
        gui::DrawSprite(sprite, x, y, depth, std::bit_cast<uint32_t>(time));
    }
    void DrawProgressBar(View* manager) { gui::DrawProgressBar(manager); }
    void RenderCursor() { gui::RenderCursor(); }
    bool SystemCursorSetting() { return game::settings_store::Current().m_systemCursor != 0; }
    bool SystemCursorHidden() { return game::GetCursor()->m_systemCursorHidden != 0; }
    void WidgetRedraw(void* w) { (static_cast<ui::GuiWidget*>(w))->Invalidate(); }
    void WidgetDraw(void* w) { (static_cast<ui::GuiWidget*>(w))->Draw(); }
    void WidgetUpdate(void* w, float dt) { (static_cast<ui::GuiWidget*>(w))->Update(dt); }
    void DialogRefresh(void* d) { (static_cast<ui::GuiWidget*>(d))->DrawOverlay(); }
    void ModeDraw(ui::GuiMode* mode) { mode->DrawWorld(); }
    void ModeUpdate(ui::GuiMode* mode, float dt) { mode->Update(dt); }
    void ProviderClosed(void* provider) { provider_virtual::Closed(provider); }
    void DialogOrigin(void* dialog, int* x, int* y)
    {
        int32_t bounds[4];
        const int32_t* origin = (static_cast<ui::GuiWidget*>(dialog))->GetBounds(bounds);
        *x = origin[0];
        *y = origin[1];
    }

    // Update.
    void SwitchMode(View* manager) { SwitchModeCall(manager); }
    void UpdateDialogs(View* manager, float dt) { UpdateDialogsCall(manager, std::bit_cast<uint32_t>(dt)); }
    void UpdateMouse(View* manager) { UpdateMouseCall(manager); }
    void ChangeResolution(int width, int height) { game::game_api::ChangeResolution(width, height); }
    void SetSettingsResolution(int width, int height)
    {
        game::settings_store::Current().m_resolutionWidth = width;
        game::settings_store::Current().m_resolutionHeight = height;
    }
    void ClearDialogPositions() { game::settings_store::ClearDialogPositions(game::settings_store::Current()); }

    void RequestGuiReload() { game::GetGame()->m_guiReloadPending = 1; }
    void RandomizeRainDrops() { RandomizeRainDropsCall(); }
    void UpdateRadarViewRect(View* manager) { UpdateRadarViewRectCall(manager); }
    void RelayoutProgressBar(View* manager) { RelayoutProgressBarCall(manager); }
    void UpdateMusicPlayer(void* player) { UpdateMusicPlayerCall(player); }
    void UpdateTickerList(void* ticker) { UpdateTickerListCall(ticker); }
    void ShiftRadarViewRect(View* manager) { ShiftRadarViewRectCall(manager); }
    void UpdateCredits(GuiCredits* credits, float dt) { UpdateCreditsCall(credits, std::bit_cast<uint32_t>(dt)); }
    bool CreditsFinished(GuiCredits* credits) { return CreditsFinishedCall(credits); }
    void StoreDialogPosition(int x, int y, int key)
    {
        game::settings_store::StoreDialogPosition(game::settings_store::Current(), x, y, key);
    }

    // Construct, CreateBarSurfaces, Destruct.
    // There is no global manager pointer to set: GetGuiManager() reads the Game's member, which owns the object (the Game
    // creates and destroys it) and stays set while the destructor runs.
    void SetManager(View*) {}
    void* AllocateListHead() { return AllocateCall(sizeof(Node)); }
    void FreeListHead(Node* head) { FreeNodesCall(head, 1); }
    void* Renderer() { return game::GetRenderer(); }
    void CreateBarSurfaces(View* manager) { CreateBarSurfacesCall(manager); }
    void* NewTooltip() { return NewTooltipCall(); }
    void* NewMusicPlayer() { return NewMusicPlayerCall(); }
    void* NewTickerList() { return NewTickerListCall(); }
    void CreateProgressBar(View* manager) { CreateProgressBarCall(manager); }
    int32_t CreateSurface(void* renderer, int width, int height, int flags) { return CreateSurfaceCall(renderer, width, height, flags); }
    const char* RadarPanelSpriteName() { return gui_data::g_RadarPanelDownSprite; }
    const char* ProgressPanelSpriteName() { return gui_data::g_ProgressPanelDownSprite; }
    ui::SpriteInstance* CreateSprite(const char* name) { return CreateSpriteCall(ui::GetSpriteBank(), name); }
    void ReleaseSprite(void* sprite) { ReleaseSpriteCall(ui::GetSpriteBank(), sprite); }
    void DestructTooltip(void* tooltip) { DestructTooltipCall(tooltip); }
    void DestructMusicPlayer(void* player) { DestructMusicPlayerCall(player); }
    void DestructCreditsObject(GuiCredits* credits) { DestructCreditsCall(credits); }
};

void SyncBarSurfacesBody(View* manager)
{
    GameEnv env;
    gui_manager_frame::SyncBarSurfaces(env, manager);
}

} // namespace
} // namespace obl::gui
namespace obl::ui {
void GuiManager::SetFont(int index, const void* font)
{
    gui::g_counter.Count();
    gui_manager_frame::SetFont(this, index, font);
}
void GuiManager::SetTextRenderer(int index, void* renderer)
{
    gui::g_counter.Count();
    gui_manager_frame::SetTextRenderer(this, index, renderer);
}
void GuiManager::RequestResolution(int width, int height)
{
    gui::g_counter.Count();
    gui_manager_frame::RequestResolution(this, width, height);
}
void GuiManager::SetSoftwareCursorEnabled(bool enabled)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::SetSoftwareCursorEnabled(env, this, enabled);
}
void GuiManager::CreateCredits()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::CreateCredits(env, this);
}
void GuiManager::DestroyCredits()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::DestroyCredits(env, this);
}
void GuiManager::OpenVideoOptions()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::OpenVideoOptions(env, this);
}
void GuiManager::DrawSurfaces()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::DrawSurfaces(env, this);
}
void GuiManager::LoadScript(const char* path)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::LoadScript(env, this, path);
}
void GuiManager::Update(float dt)
{
    gui::RefreshCommandDialogs();
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::Update(env, this, dt);
}
GuiManager* GuiManager::Construct()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui_manager_frame::Construct(env, this);
}
void GuiManager::Destruct()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::Destruct(env, this);
}
bool GuiManager::IsMainItemHovered()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui_manager_frame::IsMainItemHovered(env, this);
}
bool GuiManager::IsRadarHovered()
{
    gui::g_counter.Count();
    return gui_manager_frame::IsRadarHovered(this);
}
void GuiManager::HideProgressBar()
{
    gui::g_counter.Count();
    gui_manager_frame::HideProgressBar(this);
}
void GuiManager::UpdateFollowCamera()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::UpdateFollowCamera(env, this);
}
bool GuiManager::IsCursorOverGui(bool includeRadar)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui_manager_frame::IsCursorOverGui(env, this, includeRadar);
}
void GuiManager::DrawTileOutline(uint32_t x, uint32_t y, uint32_t color)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::DrawTileOutline(env, this, x, y, color);
}
GuiCredits* GuiCredits::Construct()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui_manager_frame::ConstructCredits(env, this);
}
void GuiCredits::Destruct()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::DestructCredits(env, this);
}
void GuiCredits::Reset()
{
    gui::g_counter.Count();
    gui_manager_frame::ResetCredits(this);
}
void GuiCredits::Update(float dt)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::UpdateCredits(env, this, dt);
}
bool GuiCredits::IsFinished()
{
    gui::g_counter.Count();
    return gui_manager_frame::IsCreditsFinished(this);
}
void GuiManager::SetMode(int mode)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::SetMode(env, this, mode);
}
void GuiManager::SwitchMode()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::SwitchMode(env, this);
}
void GuiManager::UpdateMouse()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::UpdateMouse(env, this);
}
void GuiManager::UpdateProgressBarMouse(int x, int y, int state)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_manager_frame::UpdateProgressBarMouse(env, this, x, y, state);
}
void GuiManager::SyncBarSurfaces()
{
    gui::g_counter.Count();
    ui::PrepareBarSurfaceSync(this);
    gui::SyncBarSurfacesBody(this);
}
bool GuiManager::CanUseWorldCameraInput()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui_manager_frame::CanUseWorldCameraInput(env, this);
}
bool GuiManager::CreateBarSurfaces()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    return gui_manager_frame::CreateBarSurfaces(env, this);
}
} // namespace obl::ui
