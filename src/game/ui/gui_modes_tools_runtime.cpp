#include "game/objects/object_guest.h"
#include "core/key_code.h"
#include "core/char16.h"
#include "core/pointer_width.h"
#include "game/ui/gui_manager_buttons.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/cursor.h"
#include "game/objects/object_moving.h"
#include "game/objects/guest.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/sound_api.h"
#include "game/ui/renderer_api.h"
#include "game/ui/line_draw.h"
#include "game/tables/building_behavior.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/gui_modes_tools.h"
#include "game/map/grid_state.h"
#include "game/ui/gui_tooltip.h"
#include "game/map/path_queries.h"

#include "game/objects/object_scenery.h"
#include "game/ui/photo_file.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/map/cell_objects.h"
#include "game/game/command_loop.h"
#include "game/ui/gui_canvas_live.h"
#include "game/ui/input.h"
#include "game/map/grid_cell.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_modes_types.h"
#include "game/ui/map_view.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/messages/message_state.h"
#include "game/ui/renderer_types.h"
#include "core/rect.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "game/world/world_state.h"
#include "core/log.h"
#include "game/ui/map_view_access.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include "game/ui/text_table.h"
#include "game/ui/map_view_math.h"

namespace obl::gui {
namespace {

using Mode = ui::GuiModeCamera;
using Manager = ui::GuiManager;
using Node = ui::GuiModeCameraThumb;
// Call counters (`call-report <name>`).
test::CallCounter g_counter("ui.gui-mode-camera"); // the photo mode frame
test::CallCounter g_composeCounter("ui.gui-mode-album-compose"); // GuiModeCamera::ComposePhoto
test::CallCounter g_switch("GuiModeSwitch"); // driver: asks the real manager for a mode (the next update builds it)
test::CallCounter g_lifecycleCounter("ui.gui-mode-lifecycle"); // one per lifecycle call
test::CallCounter g_updateCounter("ui.gui-mode-update"); // one per Update call of a mode of this module

// The entry points of this module the mode code calls directly (they keep the counters and the compose probe).

// ---- the game ------------------------------------------------------------------------------------------------------------------

void* PoolAllocate(size_t bytes)
{
    return ::operator new(bytes);
}
void PoolFree(void* block, size_t bytes)
{
    ::operator delete(block);
}

constexpr size_t kListHeadBytes = sizeof(Node);
constexpr size_t kListNodeBytes = sizeof(Node);

ui::GuiManager* View(Manager* manager)
{
    return static_cast<ui::GuiManager*>(manager);
}

void ClearField15c(Manager* manager)
{
    manager->HideProgressBar();
}

// ---- C++ lifecycle ---------------------------------------------------------------------------------------------------------------

// Appends a node holding `surface` before the head, the way the list's insert at end() links it.
void LinkBack(Node* head, Node* node, int surface)
{
    node->m_surface = surface;
    node->m_next = head;
    node->m_prev = head->m_prev;
    head->m_prev->m_next = node;
    head->m_prev = node;
}

// Frees every node of the thumbnail list (the head stays) and links the head to itself.
void ClearThumbs(Mode* mode)
{
    Node* head = mode->m_thumbs;
    for (Node* node = head->m_next; node != head;) {
        Node* next = node->m_next;
        if (node) PoolFree(node, kListNodeBytes);
        node = next;
    }
    head->m_next = head;
    head->m_prev = head;
}

void EnterCamera(Mode* mode)
{
    ClearThumbs(mode);
    View(mode->m_manager)->m_radarEnabled = 0;
    ClearField15c(mode->m_manager);
}

void ExitCamera(Mode* mode)
{
    ui::Renderer* renderer = game::GetRenderer();
    Node* head = mode->m_thumbs;
    for (Node* node = head->m_next; node != head; node = node->m_next) obl::ui::renderer::ReleaseSurface(renderer, node->m_surface);
    ClearThumbs(mode);
}

void EnterDefault(ui::GuiModeDefault* mode)
{
    mode->m_dragging = 0;
    mode->m_pressed = 0;
    mode->m_selectedGuestCount = 0;
}

void ExitDefault(ui::GuiModeDefault* mode)
{
    View(mode->m_manager)->m_radarMouseArmed = 1;
}

void EnterSendStaff(ui::GuiModeSendStaff* mode)
{
    mode->m_target = object::object_manager::ObjectById(game::GetObjectManager(), mode->m_manager->m_context[9]);
}

void ExitDozer(ui::GuiModeDozer* mode)
{
    static_cast<ui::GuiManager*>(mode->m_manager)->FlashActionButtons("SceneryDozer", false);
}

} // namespace

namespace {

void TickerAnimation(Manager* manager, float dt)
{
    manager->UpdateTickerAnimation(dt);
}
void FollowCamera(Manager* manager)
{
    manager->UpdateFollowCamera();
}
int DialogCount(Manager* manager)
{
    return manager->GetDialogCount();
}
void CloseFirstDialog(Manager* manager)
{
    manager->CloseFirstDialog();
}
void ShowOptions(Manager* manager)
{
    manager->ShowDialog("InGameOptions", nullptr, false);
}
int LeftButton()
{
    return game::GetInput()->m_leftButton;
}
bool KeyPressed(int scancode)
{
    return InputKeyPressed(*game::GetInput(), scancode);
}
bool OverGui(Manager* manager)
{
    return manager->IsCursorOverGui(true);
}

void UpdateCommon(ui::GuiMode* mode, float dt)
{
    TickerAnimation(mode->m_manager, dt);
}

void UpdateCamera(Mode* mode, float dt)
{
    mode->m_dt = dt;
    FollowCamera(mode->m_manager);
    const int photo = View(mode->m_manager)->m_cameraGrabSurface;
    if (photo != -1) mode->ComposePhoto(photo);
}

// The debug paint tool: while the left button is pressed or held outside the panels, the flags of the tile under the cursor become the value
// the editor left in GUI context entry 3.
void UpdatePaint(ui::GuiModePaint* mode, float dt)
{
    Manager* manager = mode->m_manager;
    TickerAnimation(manager, dt);
    const int left = LeftButton();
    if (left != 1 && left != 2) return;
    if (OverGui(manager)) return;
    int cursor[2];
    CursorGetPos(game::GetCursor(), cursor);
    ui::MapView* view = ui::GetMapView();
    const float zoom = game::GetRenderer()->m_zoomScale;
    const int screen[2]{gui_modes_tools::PaintMapPixel(cursor[0], zoom, view->m_cameraX),
            gui_modes_tools::PaintMapPixel(cursor[1], zoom, view->m_cameraY)};
    float world[3];
    const float* point = view->ScreenToWorldPoint(world, screen);
    map::Grid* grid = game::GetGrid();
    const grid_math::TileXY tile = grid->WorldPointToTile(point[0], point[1]);
    grid->Cell(tile.x, tile.y).m_flags = static_cast<uint32_t>(manager->m_context[3]);
}

// Esc in the default mode drops the followed object and either opens the options (nothing else is open) or closes the first dialog.
void UpdateDefault(ui::GuiModeDefault* mode, float dt)
{
    Manager* manager = mode->m_manager;
    TickerAnimation(manager, dt);
    FollowCamera(manager);
    if (!KeyPressed(obl::input::KeyCode::Escape)) return;
    View(manager)->m_selection = -1;
    if (gui_modes_tools::EscapeOpensOptions(DialogCount(manager)))
        ShowOptions(manager);
    else
        CloseFirstDialog(manager);
}

constexpr int kModeDefault = 4;

bool OverGuiFlag(Manager* manager, bool includeMain)
{
    return manager->IsCursorOverGui(includeMain);
}
void SetMode(Manager* manager, int mode)
{
    manager->SetMode(mode);
}
void SetCursorShape(int shape)
{
    CursorSetShape(game::GetCursor(), shape);
}
void PlaySound(const char* name)
{
    audio::CurrentSound()->Play(name, 1, 0.0f);
}
void AnimateSceneryPanelOut(Manager* manager)
{
    char panel[] = "SceneryPanel";
    manager->AnimateDialog(panel, 2, 1.0f, 0);
}

// The map pixel under the handed cursor (screen pixels / zoom + camera corner).
void MapPixelUnder(int x, int y, int (&screen)[2])
{
    const ui::MapView* view = ui::GetMapView();
    const float zoom = game::GetRenderer()->m_zoomScale;
    screen[0] = gui_modes_tools::MouseMapPixel(x, zoom, view->m_cameraX);
    screen[1] = gui_modes_tools::MouseMapPixel(y, zoom, view->m_cameraY);
}

// The world point under a map pixel; copied out because the game returns a pointer into its own scratch.
void WorldUnder(const int (&screen)[2], float (&world)[3])
{
    float scratch[3];
    const float* point = ui::GetMapView()->ScreenToWorldPoint(scratch, screen);
    std::memcpy(world, point, sizeof(world));
}

void TileUnder(const float (&world)[3], uint32_t (&tile)[2])
{
    const grid_math::TileXY tileXY = game::GetGrid()->WorldPointToTile(world[0], world[1]);
    tile[0] = tileXY.x;
    tile[1] = tileXY.y;
}

// Esc leaves the photo mode: back to the default mode, the CameraControls window closed.
bool OnMouseCamera(Mode* mode)
{
    if (KeyPressed(obl::input::KeyCode::Escape)) {
        Manager* manager = mode->m_manager;
        SetMode(manager, kModeDefault);
        char name[] = "CameraControls";
        static_cast<ui::GuiManager*>(manager)->CloseDialog(name);
        View(manager)->m_radarEnabled = 1;
        SetProgressVisibleUnlessSandbox(manager);
    }
    return false;
}

bool OnMouseDozer(ui::GuiModeDozer* mode, int x, int y, int left)
{
    Manager* manager = mode->m_manager;
    int screen[2];
    float world[3];
    MapPixelUnder(x, y, screen);
    WorldUnder(screen, world);
    TileUnder(world, mode->m_tile);
    mode->m_canRemove = mode->CanRemove(mode->m_tile);
    const bool overGui = OverGui(manager);
    SetCursorShape(gui_modes_tools::DozerCursorShape(overGui));
    mode->m_overMap = !overGui;
    if (left == 1) {
        // A press on the map is a `dozer` command (a refused one is recorded as rejected); the removal and its income happen at the start
        // of the next tick. CanRemove predicts the outcome for the sound.
        if (mode->m_overMap)
            game::SubmitCommand(command::Dozer{static_cast<int32_t>(mode->m_tile[0]), static_cast<int32_t>(mode->m_tile[1])});
        PlaySound(gui_modes_tools::DozerClickSound(gui_modes_tools::DozerRemoves(mode->m_overMap != 0, mode->m_canRemove != 0)));
    }
    if (KeyPressed(obl::input::KeyCode::Escape)) {
        AnimateSceneryPanelOut(manager);
        SetMode(manager, kModeDefault);
    }
    return false;
}

bool OnMouseSendStaff(ui::GuiModeSendStaff* mode, int x, int y, int left)
{
    Manager* manager = mode->m_manager;
    int screen[2];
    float world[3];
    MapPixelUnder(x, y, screen);
    WorldUnder(screen, world);
    TileUnder(world, mode->m_tile);
    int targetId = -1; // CanDirect always writes it
    mode->m_canDirect = mode->CanDirect(mode->m_tile, &targetId);
    const bool overGui = OverGui(manager);
    SetCursorShape(gui_modes_tools::SendStaffCursorShape(overGui, mode->m_targetsObject != 0, mode->m_canDirect != 0));
    mode->m_overMap = !overGui;
    const bool sent = gui_modes_tools::SendStaffSends(left, mode->m_overMap != 0, mode->m_canDirect != 0);
    if (left == 1 && mode->m_overMap) {
        const uint32_t staffId = mode->m_target ? static_cast<object::Base*>(mode->m_target)->m_id : 0xffffffffu;
        if (targetId == -1)
            game::SubmitCommand(command::SendStaff{staffId, world[0], world[1], world[2]});
        else
            game::SubmitCommand(command::SendStaffTo{staffId, static_cast<uint32_t>(targetId)});
    }
    if (KeyPressed(obl::input::KeyCode::Escape) || sent) SetMode(manager, kModeDefault);
    return false;
}

bool OnMouseDefault(ui::GuiModeDefault* mode, int x, int y, int left, int right)
{
    Manager* manager = mode->m_manager;
    ui::GuiManager* view = View(manager);
    int screen[2];
    float world[3];
    MapPixelUnder(x, y, screen);
    if (right == 1 && !manager->IsMainItemHovered()) view->m_selection = -1;
    if (left == 1) {
        if (!OverGui(manager) && !mode->SelectUnderCursor()) {
            WorldUnder(screen, world);
            std::memcpy(mode->m_pressWorld, world, sizeof(world));
            mode->m_pressed = 1;
            object::object_manager::NotifyAllObjects(game::GetObjectManager());
            mode->m_selectedGuestCount = 0;
        }
    } else if (left == 2) {
        if (OverGuiFlag(manager, false)) {
            mode->m_dragging = 0;
            view->m_radarMouseArmed = 1;
        } else {
            if (manager->IsRadarHovered() && !mode->m_dragging) mode->m_pressed = 0;
            if (mode->m_pressed) {
                mode->m_dragging = 1;
                view->m_radarMouseArmed = 0;
            }
        }
    } else if (mode->m_dragging) {
        WorldUnder(screen, world);
        std::memcpy(mode->m_releaseWorld, world, sizeof(world));
        mode->m_dragging = 0;
        mode->m_pressed = 0;
        if (mode->m_selectionShown) mode->SelectGuestsInRectangle();
        view->m_radarMouseArmed = 1;
    } else if (right != 2) {
        const bool overGui = OverGui(manager);
        bool blocking = false, objectUnder = false;
        if (!overGui) {
            blocking = manager->IsBlockingDialogOpen();
            if (!blocking) {
                objectUnder = !object::object_manager::GetObjectsUnderCursor(game::GetObjectManager()).empty();
            }
        }
        SetCursorShape(gui_modes_tools::DefaultCursorShape(overGui, blocking, objectUnder));
    }
    if (DialogCount(manager) != 0) {
        mode->m_dragging = 0;
        view->m_radarMouseArmed = 1;
    }
    return false;
}

test::CallCounter g_mouseCounter("ui.gui-mode-mouse"); // one per OnMouse call of a mode of this module

// ---- GuiModeCamera::DrawScreen -------------------------------------------------------------------------------------------------

// Thumbnails of the photos taken so far, one blit per list node, left to right above the bottom edge.
void BlitThumbnails(const Mode* mode, int height)
{
    ui::Renderer* renderer = game::GetRenderer();
    const ui::GuiModeCameraThumb* head = mode->m_thumbs;
    int x = gui_modes_tools::kCameraThumbX;
    for (const ui::GuiModeCameraThumb* node = head->m_next; node != head; node = node->m_next) {
        obl::ui::renderer::BlitSurface(renderer, x, height - gui_modes_tools::kCameraThumbRaise, node->m_surface);
        x += gui_modes_tools::kCameraThumbStep;
    }
}

// The whole draw on the game's functions: frame sprites through LiveCanvas, then the thumbnails.
void RunDraw(Mode* mode)
{
    const ui::Renderer* renderer = game::GetRenderer();
    {
        LiveCanvas canvas;
        gui_modes_tools::DrawCameraFrame(canvas, renderer->m_width, renderer->m_height, mode->m_dt);
    }
    BlitThumbnails(mode, renderer->m_height);
}

// ---- GuiModeCamera::ComposePhoto: the album page -------------------------------------------------------------------------------

using Renderer = ui::Renderer;

int ThumbCount(const Mode* mode)
{
    int count = 0;
    const Node* head = mode->m_thumbs;
    for (const Node* node = head->m_next; node != head; node = node->m_next) ++count;
    return count;
}

// The comment line of the page: the game's rand() picks one of the CAMERA_COMMENT_<n> entries of the text table.
const Char16* RollComment()
{
    const std::string key = gui_modes_tools::AlbumCommentKey(game::Rand());
    return ui::text_table::Lookup(key.c_str());
}

int CreateSurface(Renderer* r, int width, int height)
{
    return obl::ui::renderer::CreateSurface(r, width, height, 1);
}
void ClearSurface(Renderer* r, int surface)
{
    obl::ui::renderer::ClearSurface(r, surface);
}
void SetActiveSurface(Renderer* r, int surface)
{
    obl::ui::renderer::SetActiveSurface(r, surface);
}
void ReleaseSurface(Renderer* r, int surface)
{
    obl::ui::renderer::ReleaseSurface(r, surface);
}

void Compose(Mode* mode, int photo)
{
    Manager* manager = mode->m_manager;
    Renderer* renderer = game::GetRenderer();
    renderer->m_directMode = 1;

    const int screenWidth = renderer->m_width;
    const int height = renderer->m_height;
    const int width = gui_modes_tools::AlbumSurfaceWidth(screenWidth);
    const int page = CreateSurface(renderer, width, height);
    obl::ui::renderer::SetSurfaceOrigin(renderer, page, 0, 0);
    ClearSurface(renderer, page);
    const screen::Rect area{0, 0, width, height};
    SetActiveSurface(renderer, page);
    const uint32_t white = obl::ui::renderer::PackColor(renderer, 0xff, 0xff, 0xff);
    const int fillRect[]{area.left, area.top, area.right, area.bottom};
    obl::ui::renderer::FillRect(renderer, fillRect, white);
    // (x, y, destination, source): also makes `page` the active surface.
    obl::ui::renderer::BlitSurfaceKeyed(renderer, gui_modes_tools::kAlbumPhotoOffset, gui_modes_tools::kAlbumPhotoOffset, page, photo);
    SetActiveSurface(renderer, page);
    {
        LiveCanvas canvas;
        gui_modes_tools::DrawAlbumOverlay(canvas, width, height, &RollComment);
    }
    ui::PhotoAlbumSavePhotoFile(page);

    Node* head = mode->m_thumbs;
    for (int drop = gui_modes_tools::AlbumDropCount(ThumbCount(mode), gui_modes_tools::AlbumPhotoLimit(screenWidth)); drop > 0; --drop) {
        Node* first = head->m_next;
        const int surface = first->m_surface;
        first->m_prev->m_next = first->m_next;
        first->m_next->m_prev = first->m_prev;
        PoolFree(first, kListNodeBytes);
        ReleaseSurface(renderer, surface);
    }
    const int thumb = CreateSurface(renderer, 0x41, 0x27);
    ClearSurface(renderer, thumb);
    SetActiveSurface(renderer, thumb);
    obl::ui::renderer::DownscaleSurface(renderer, photo, thumb);
    LinkBack(head, static_cast<Node*>(PoolAllocate(kListNodeBytes)), thumb);
    View(manager)->m_cameraGrabSurface = -1;
    ReleaseSurface(renderer, page);
    ReleaseSurface(renderer, photo);
    renderer->m_directMode = 0;
}

std::atomic<int> g_composed{0};

using GuiManager = ui::GuiManager;
using World = layout::World;
test::CallCounter g_clickEnter("GuiModeClickEnter"), g_clickAim("GuiModeClickAim"), g_clickCheck("GuiModeClickCheck");

struct ClickPlan {
    bool armed = false;
    int mode = 0, variant = 0;
    uint32_t tile[2]{};
    uint32_t flags = 0;
    int viewX = 0, viewY = 0;
    float income = 0, demolish = 0;
    float world[3]{};
    int followed = 0;
};
ClickPlan g_clickPlan;
float g_pressWorld[3];

GuiManager* ClickManager()
{
    return game::GetGuiManager();
}

bool TileListEmpty(uint32_t x, uint32_t y)
{
    return game::GetGrid()->Cell(x, y).m_objects.empty();
}

// A bare patch: the tile and its eight neighbours hold no object (so no sprite can be under a cursor parked there).
bool BarePatch(uint32_t x, uint32_t y)
{
    const map::Grid* grid = game::GetGrid();
    if (x == 0 || y == 0 || x + 1 >= grid->m_width || y + 1 >= grid->m_height) return false;
    for (uint32_t dy = y - 1; dy <= y + 1; ++dy)
        for (uint32_t dx = x - 1; dx <= x + 1; ++dx)
            if (!TileListEmpty(dx, dy)) return false;
    return true;
}

void TileOfView(int viewX, int viewY, uint32_t (&tile)[2], float (&world)[3])
{
    const int screen[2]{viewX + ui::GetMapView()->m_cameraX, viewY + ui::GetMapView()->m_cameraY};
    WorldUnder(screen, world);
    TileUnder(world, tile);
}

object::Base* FirstStaff()
{
    for (object::ObjectType type : {object::ObjectType::Builder, object::ObjectType::Lifeguard, object::ObjectType::Cleaner,
                 object::ObjectType::Mechanic, object::ObjectType::Rep, object::ObjectType::SecurityGuard}) {
        const auto members = object::object_manager::GetListByClass(game::GetObjectManager(), static_cast<int>(type));
        const auto count = static_cast<uint32_t>(members.size());
        if (count) return members[0];
    }
    return nullptr;
}

void DriveClickEnter(int mode)
{
    auto* gui = game::GetGuiManager();
    if (!gui || !game::GetWorld()) {
        OBL_LOG("autotest: FAILED GuiModeClickEnter needs gameplay");
        return;
    }
    if (mode == 9) {
        const object::Base* staff = FirstStaff();
        if (!staff) {
            OBL_LOG("autotest: FAILED GuiModeClickEnter found no staff member (spawn-builder first)");
            return;
        }
        gui->SetContext(9, static_cast<int>(staff->m_id));
    }
    SetMode(gui, mode);
    g_clickEnter.Count();
}

// Brings a bare walkable patch of land (the first the grid scan finds) to the middle of the view; the camera follows over the next frames.
test::CallCounter g_clickFocus("GuiModeClickFocus");
void DriveClickFocus(int)
{
    const map::Grid* grid = game::GetGrid();
    if (!grid || !game::GetWorld()) {
        OBL_LOG("autotest: FAILED GuiModeClickFocus needs gameplay");
        return;
    }
    for (uint32_t y = 2; y + 2 < grid->m_height; ++y) {
        for (uint32_t x = 2; x + 2 < grid->m_width; ++x) {
            const uint32_t flags = grid->Cell(x, y).m_flags;
            if ((flags & 1) == 0 || (flags & 0x10) != 0 || (flags & 0x80000000u) == 0 || (flags & 0x10007000) != 0 || !BarePatch(x, y))
                continue;
            const uint32_t tile[2]{x, y};
            int pixel[2];
            ui::GetMapView()->TileToScreen(pixel, tile);
            ui::GetMapView()->SetCameraPos(pixel[0] - 400, pixel[1] - 300);
            g_clickFocus.Count();
            return;
        }
    }
    g_clickFocus.Record(false, "no bare walkable patch on the map");
}

// arg = mode * 10 + variant. Dozer 100 grass on a bare tile, 101 bare tile; SendStaff 90 walkable, 91 sea; Default 40 press, 41 the release point
// (the cursor moves while the button is held), 42 right button; the cursor goes to the candidate nearest to the middle of the view.
void DriveClickAim(int arg)
{
    const int mode = arg / 10, variant = arg % 10;
    GuiManager* tree = ClickManager();
    World* world = game::GetWorld();
    if (!tree || !world || tree->m_mode != mode || !tree->m_modeObject || game::GetRenderer()->m_zoomScale != 1.0f) {
        OBL_LOG("autotest: FAILED GuiModeClickAim needs gameplay, the mode entered and zoom scale 1");
        return;
    }
    ClickPlan plan;
    plan.mode = mode;
    plan.variant = variant;
    int centerX = 400, centerY = 300;
    if (mode == 4 && variant == 1) centerX = 560; // the release point: well away from the press
    if (mode == 4 && variant == 2) { // the right button needs an object to follow; the cursor stays where it is
        const object::Manager* objects = game::GetObjectManager();
        if (!objects->m_guests.size()) {
            g_clickAim.Record(false, "no guest to follow");
            return;
        }
        plan.followed = static_cast<int>(objects->m_guests[0]->m_id);
        View(static_cast<Manager*>(game::GetGuiManager()))->m_selection = plan.followed;
        plan.armed = true;
        g_clickPlan = plan;
        platform::SetSyntheticCursor(400, 300);
        g_clickAim.Count();
        return;
    }
    bool found = false;
    int best = 0;
    for (int y = 120; y <= 420; y += 6) {
        for (int x = 200; x <= 600; x += 6) {
            uint32_t tile[2];
            float point[3];
            TileOfView(x, y, tile, point);
            const uint32_t flags = game::GetGrid()->Cell(tile[0], tile[1]).m_flags;
            const bool land = (flags & 1) != 0;
            bool fits = false;
            if (mode == 10) {
                fits = land && BarePatch(tile[0], tile[1]) && (flags & 0x10007000) == 0 &&
                        !static_cast<ui::GuiModeDozer*>(tree->m_modeObject)->CanRemove(tile);
            } else if (mode == 9) {
                const bool walkable = (flags & 0x10) == 0 && (flags & 0x80000000u) != 0;
                fits = variant == 0 ? walkable && BarePatch(tile[0], tile[1]) : (flags & 0x80000000u) == 0;
            } else {
                fits = land && BarePatch(tile[0], tile[1]);
            }
            const int distance = (x - centerX) * (x - centerX) + (y - centerY) * (y - centerY);
            if (fits && (!found || distance < best)) {
                plan.tile[0] = tile[0];
                plan.tile[1] = tile[1];
                plan.flags = flags;
                plan.viewX = x;
                plan.viewY = y;
                std::memcpy(plan.world, point, sizeof(point));
                found = true;
                best = distance;
            }
        }
    }
    if (!found) {
        g_clickAim.Record(false, "no tile for arg " + std::to_string(arg));
        return;
    }
    if (mode == 10 && variant == 0) game::GetGrid()->Cell(plan.tile[0], plan.tile[1]).m_flags = plan.flags | 0x1000; // grass to remove
    plan.income = world->m_incomeToday;
    plan.demolish = world->m_demolishIncomeToday;
    if (mode == 4 && variant == 0) std::memcpy(g_pressWorld, plan.world, sizeof(plan.world));
    plan.armed = true;
    g_clickPlan = plan;
    platform::SetSyntheticCursor(plan.viewX, plan.viewY);
    g_clickAim.Count();
}

bool SameFloats(const float* a, const float* b)
{
    return std::memcmp(a, b, 3 * sizeof(float)) == 0;
}

void DriveClickCheck(int arg)
{
    const int mode = arg / 10, variant = arg % 10;
    GuiManager* tree = ClickManager();
    World* world = game::GetWorld();
    const ClickPlan& plan = g_clickPlan;
    if (!tree || !world) {
        OBL_LOG("autotest: FAILED GuiModeClickCheck needs gameplay");
        return;
    }
    std::string why;
    const auto expect = [&](bool condition, const char* what) {
        if (!condition) why += std::string(what) + " ";
    };
    const auto bits = [](float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; };
    ui::GuiManager* view = View(game::GetGuiManager());
    if (mode == 2) { // Esc left the photo mode
        expect(tree->m_mode == kModeDefault, "mode");
        expect(view->m_radarEnabled == 1, "photo flag");
    } else {
        if (!plan.armed || plan.mode != mode || plan.variant != variant) {
            OBL_LOG("autotest: FAILED GuiModeClickCheck without a matching Aim");
            return;
        }
        if (mode == 10) {
            const uint32_t flags = game::GetGrid()->Cell(plan.tile[0], plan.tile[1]).m_flags;
            const bool removes = variant == 0;
            expect(tree->m_mode == 10, "mode");
            expect(flags == plan.flags, "tile flags"); // the grass painted by Aim is gone again, a bare tile is untouched
            expect(bits(world->m_incomeToday, plan.income), "income");
            expect(bits(world->m_demolishIncomeToday, removes ? plan.demolish + 5.0f : plan.demolish), "demolish income");
            const auto* dozer = static_cast<const ui::GuiModeDozer*>(tree->m_modeObject);
            expect(tree->m_mode == 10 && dozer->m_tile[0] == plan.tile[0] && dozer->m_tile[1] == plan.tile[1], "tile under the cursor");
            expect(tree->m_mode == 10 && dozer->m_overMap == 1, "over the map");
            expect(tree->m_mode == 10 && dozer->m_canRemove == 0, "removable after the click"); // grass removed, or nothing to remove
        } else if (mode == 9) {
            if (variant == 0) {
                expect(tree->m_mode == kModeDefault, "mode (order sent)");
            } else {
                expect(tree->m_mode == 9, "mode (order refused)");
                const auto* staff = static_cast<const ui::GuiModeSendStaff*>(tree->m_modeObject);
                expect(tree->m_mode == 9 && staff->m_tile[0] == plan.tile[0] && staff->m_tile[1] == plan.tile[1], "tile under the cursor");
                expect(tree->m_mode == 9 && staff->m_canDirect == 0, "cannot direct");
                expect(tree->m_mode == 9 && staff->m_overMap == 1, "over the map");
            }
        } else if (mode == 4 && variant == 0) { // the button is down on bare ground: armed, then dragging
            const auto* mode = static_cast<const ui::GuiModeDefault*>(tree->m_modeObject);
            expect(tree->m_mode == 4, "mode");
            expect(mode->m_pressed == 1, "pressed");
            expect(mode->m_dragging == 1, "dragging");
            expect(SameFloats(mode->m_pressWorld, plan.world), "press point");
            expect(view->m_radarMouseArmed == 0, "bar flag during the drag");
        } else if (mode == 4 && variant == 1) { // released elsewhere: the rectangle ends there
            const auto* mode = static_cast<const ui::GuiModeDefault*>(tree->m_modeObject);
            expect(tree->m_mode == 4, "mode");
            expect(mode->m_pressed == 0 && mode->m_dragging == 0, "drag ended");
            expect(SameFloats(mode->m_releaseWorld, plan.world), "release point");
            expect(SameFloats(mode->m_pressWorld, g_pressWorld), "press point kept");
            expect(view->m_radarMouseArmed == 1, "bar flag after the release");
        } else if (mode == 4 && variant == 2) {
            expect(view->m_selection == -1, "followed object dropped");
        }
    }
    g_clickPlan.armed = false;
    g_clickCheck.Record(why.empty(), "arg=" + std::to_string(arg) + " failed: " + why + "gui mode " + std::to_string(tree->m_mode));
}

void DrawHighlight(uint32_t x, uint32_t y, uint32_t color)
{
    ui::GetMapView()->DrawTileHighlight(x, y, color);
}
void DrawFrameLine(void* renderer, int x1, int y1, int x2, int y2, uint32_t color)
{
    ui::pixel_primitives::DrawLine(renderer, x1, y1, x2, y2, color);
}

uint32_t PackColor(gui_modes_tools::TileHighlightRgb rgb)
{
    return obl::ui::renderer::PackColor(
            game::GetRenderer(), static_cast<uint32_t>(rgb.r), static_cast<uint32_t>(rgb.g), static_cast<uint32_t>(rgb.b));
}

void DrawDozer(ui::GuiModeDozer* mode)
{
    if (!mode->m_overMap) return;
    DrawHighlight(mode->m_tile[0], mode->m_tile[1], PackColor(gui_modes_tools::TileHighlightColor(mode->m_canRemove != 0)));
}

void DrawStaff(ui::GuiModeSendStaff* mode)
{
    if (!mode->m_overMap) return;
    DrawHighlight(mode->m_tile[0], mode->m_tile[1], PackColor(gui_modes_tools::TileHighlightColor(mode->m_canDirect != 0)));
}

void DrawDefault(ui::GuiModeDefault* mode)
{
    if (!mode->m_dragging) return;
    ui::Renderer* renderer = game::GetRenderer();
    const uint32_t color = PackColor({255, 255, 255});
    float press[3];
    std::memcpy(press, mode->m_pressWorld, sizeof(press));
    int pixel[2];
    const int* result = ui::GetMapView()->ProjectWorldPoint(pixel, press);
    const int x = gui_modes_tools::SelectionScreenPoint(result[0], renderer->m_zoomScale);
    const int y = gui_modes_tools::SelectionScreenPoint(result[1], renderer->m_zoomScale);
    const auto* cursor = game::GetCursor();
    const gui_modes_tools::SelectionFrame frame =
            gui_modes_tools::ComputeSelectionFrame(x, y, cursor->m_x, cursor->m_y, renderer->m_width, renderer->m_height);
    mode->m_selectionShown = frame.shown;
    if (!frame.shown) return;
    DrawFrameLine(renderer, frame.x1, frame.y1, frame.x2, frame.y1, color);
    DrawFrameLine(renderer, frame.x2, frame.y1, frame.x2, frame.y2, color);
    DrawFrameLine(renderer, frame.x2, frame.y2, frame.x1, frame.y2, color);
    DrawFrameLine(renderer, frame.x1, frame.y2, frame.x1, frame.y1, color);
}

test::CallCounter g_toolsDrawCounter("ui.gui-mode-tools-draw"); // one per draw of these three

using Object = object::Base;

const map::GridCell& TileObjects(uint32_t x, uint32_t y)
{
    return game::GetGrid()->Cell(x, y);
}
const tables::SceneryDef* DefinitionOf(const Object* scenery)
{
    return static_cast<const object::Scenery*>(scenery)->m_definition;
}
bool IsType(const Object* object, object::ObjectType type)
{
    return object->m_type == type;
}

void NeighbourCells(uint32_t x, uint32_t y, map::GridCell* (&cells)[8])
{
    game::GetGrid()->GetNeighbourCells(x, y, cells);
}

bool NeighboursFree(uint32_t x, uint32_t y)
{
    map::GridCell* cells[8];
    NeighbourCells(x, y, cells);
    for (map::GridCell* cell : cells)
        if ((cell->m_flags & gui_modes_tools::kTileBuildingBit) != 0) return false;
    return true;
}

// ---- the effects ----
void RequestRemoval(Object* scenery)
{
    static_cast<object::Scenery*>(scenery)->RequestRemoval();
}

void RebuildTile(uint32_t x, uint32_t y, int kind)
{
    game::GetPathTiles()->RebuildTile(x, y, kind);
}

bool SelectGuestAt(Object* guest, int* screenPoint)
{
    return object::GuestTrySelectAtScreenPoint(*static_cast<object::Guest*>(guest), screenPoint);
}

bool SelectStaffAt(Object* staff, int* screenPoint)
{
    return static_cast<object::Person*>(staff)->TrySelectAtScreenPoint(screenPoint);
}

void PostMessage(messages::Message* message)
{
    messages::message_facade::Post(messages::message_facade::Current(), message);
}

void ClearSelectionOfClass(uint32_t objectClass)
{
    object::object_manager::ClearSelectionByClass(game::GetObjectManager(), objectClass);
}

std::vector<Object*> ObjectsUnderCursor()
{
    return object::object_manager::GetObjectsUnderCursor(game::GetObjectManager());
}
std::vector<Object*> ObjectsUnderCursorUnselectable()
{
    return object::object_manager::GetObjectsUnderCursorUnselectable(game::GetObjectManager());
}

std::vector<Object*> ObjectsInRectangle(uint32_t objectClass, float* low, float* high)
{
    return object::object_manager::GetObjectsInRectangle(game::GetObjectManager(), objectClass, low, high);
}

bool VisibleAgainst(Object* object, int buildingId)
{
    return static_cast<object::Mover*>(object)->IsVisibleAgainstBuilding(buildingId);
}

// ---- the bulldozer ----
void CreditIncome(double amount, bool cash)
{
    layout::World* world = game::GetWorld();
    // Selling assets is not operating income: otherwise repeated purchases satisfy scenario 8's income goal.
    world->m_demolishIncomeToday = static_cast<float>(amount + world->m_demolishIncomeToday);
    if (cash) world->m_player->m_cash = static_cast<float>(world->m_player->m_cash + gui_modes_tools::kDozerFlagIncome);
}

bool CanRemoveScenery(const Object* object)
{
    // Removal enters state 3 and marks death synchronously, but the object stays on the grid until cleanup.
    return IsType(object, object::ObjectType::Scenery) && DefinitionOf(object)->m_userRemove != 0 && !object->m_waitingForDeath &&
            static_cast<uint32_t>(object->m_state) != gui_modes_tools::kSceneryStateBeingRemoved;
}

bool RunCanRemove(const uint32_t* tile)
{
    bool any = false, scenery = false;
    cell_objects::ForEach(*game::GetObjectManager(), TileObjects(tile[0], tile[1]), [&](const Object* object) {
        any = true;
        if (CanRemoveScenery(object)) scenery = true;
    });
    const uint32_t flags = game::GetGrid()->Cell(tile[0], tile[1]).m_flags;
    return gui_modes_tools::DozerCanRemove(any, scenery, flags, [&] { return NeighboursFree(tile[0], tile[1]); });
}

bool RemoveAt(const uint32_t* tile)
{
    const bool removed = cell_objects::ForEach(*game::GetObjectManager(), TileObjects(tile[0], tile[1]), [&](Object* object) {
        if (!CanRemoveScenery(object)) return false;
        RequestRemoval(object);
        CreditIncome(gui_modes_tools::SceneryRemovalIncome(static_cast<uint32_t>(DefinitionOf(object)->m_Cost2Build)), false);
        return true;
    });
    if (removed) return true;
    map::GridCell& cell = game::GetGrid()->Cell(tile[0], tile[1]);
    const uint32_t flags = cell.m_flags;
    if (gui_modes_tools::IsBarePathTile(flags) && NeighboursFree(tile[0], tile[1])) {
        cell.m_flags = gui_modes_tools::ClearBarePath(cell.m_flags);
        RebuildTile(tile[0], tile[1], 1);
        CreditIncome(gui_modes_tools::kDozerFlagIncome, true);
        return true;
    }
    if ((flags & gui_modes_tools::kTileSurfaceMask) == 0) return false;
    cell.m_flags = gui_modes_tools::ClearSurface(cell.m_flags);
    RebuildTile(tile[0], tile[1], 0);
    CreditIncome(gui_modes_tools::kDozerFlagIncome, true);
    return true;
}

// ---- the send-staff target ----
bool RunCanDirect(ui::GuiModeSendStaff* mode, const uint32_t* tile, int* targetId)
{
    using gui_modes_tools::StaffRule;
    mode->m_targetsObject = 0;
    *targetId = -1;
    bool allowed = gui_modes_tools::StaffTileAllowed(game::GetGrid()->Cell(mode->m_tile[0], mode->m_tile[1]).m_flags);
    auto pick = [&](const Object* object) {
        mode->m_targetsObject = 1;
        allowed = true;
        *targetId = static_cast<int>(object->m_id);
    };
    // The building under the cursor, when it has no job of its own (the builder wants one that does).
    auto hasJob = [](Object* building) {
        const auto* job = static_cast<const object::Building*>(building);
        return object::building_api::HasBuildJob(job) || object::building_api::HasDemolishJob(job);
    };
    auto forEachOnTile = [&](auto&& wanted) {
        cell_objects::ForEach(*game::GetObjectManager(), TileObjects(tile[0], tile[1]), [&](Object* object) {
            if (wanted(object)) pick(object);
        });
    };
    switch (gui_modes_tools::ClassifyStaffTarget(static_cast<const Object*>(mode->m_target)->m_Name)) {
    case StaffRule::Builder: {
        const auto objects = ObjectsUnderCursor();
        if (!objects.empty() && IsType(objects[0], object::ObjectType::Building) && hasJob(objects[0])) pick(objects[0]);
        return allowed;
    }
    case StaffRule::Cleaner: {
        const auto objects = ObjectsUnderCursor();
        if (!objects.empty() && IsType(objects[0], object::ObjectType::Building) && !hasJob(objects[0]) &&
                static_cast<const object::Building*>(objects[0])->m_definition->m_Behavior == BuildingBehavior::Toilet)
            pick(objects[0]);
        forEachOnTile([](const Object* o) {
            return IsType(o, object::ObjectType::Scenery) && DefinitionOf(o)->m_clean != 0 &&
                    static_cast<const object::Scenery*>(o)->m_vandalised == 0;
        });
        return allowed;
    }
    case StaffRule::SecurityGuard:
    case StaffRule::Guests: forEachOnTile([](const Object* o) { return IsType(o, object::ObjectType::Guest); }); return allowed;
    case StaffRule::Mechanic: {
        const auto objects = ObjectsUnderCursorUnselectable();
        if (!objects.empty() && IsType(objects[0], object::ObjectType::Building) && !hasJob(objects[0])) pick(objects[0]);
        forEachOnTile([](const Object* o) {
            return IsType(o, object::ObjectType::Scenery) && (DefinitionOf(o)->m_repair != 0 || DefinitionOf(o)->m_water != 0);
        });
        return allowed;
    }
    case StaffRule::None: break;
    }
    return allowed;
}

// ---- the default mode's selection ----
// A guest or staff member is tried at the screen point (relative to the view): the tile under it, then its eight neighbours. `preferSelected`
// re-picks a selected object (levels 1 and 2), otherwise a free one that the occluding building (-1: none) does not hide.
bool WantsSelection(Object* object, bool preferSelected, int occludingBuildingId)
{
    if (!gui_modes_tools::SelectionLevelWanted(preferSelected, object->m_SelectLevel)) return false;
    return preferSelected || occludingBuildingId == -1 || VisibleAgainst(object, occludingBuildingId);
}

void TileOfScreenPoint(const int* screenPoint, uint32_t (&tile)[2])
{
    const ui::MapView* view = ui::GetMapView();
    const int screen[2]{screenPoint[0] + view->m_cameraX, screenPoint[1] + view->m_cameraY};
    float world[3];
    WorldUnder(screen, world);
    TileUnder(world, tile);
}

template<class Visit> bool VisitNeighbours(const uint32_t (&tile)[2], Visit&& visit)
{
    map::GridCell* cells[8];
    NeighbourCells(tile[0], tile[1], cells);
    bool picked = false;
    for (int i = 0; i < 8 && !picked; ++i) {
        picked = cell_objects::ForEach(
                *game::GetObjectManager(), *cells[i], [&](Object* object) { return static_cast<bool>(visit(object)); });
    }
    return picked;
}

bool TrySelectGuest(bool preferSelected, int* screenPoint, int occludingBuildingId)
{
    uint32_t tile[2];
    TileOfScreenPoint(screenPoint, tile);
    auto tryGuest = [&](Object* o) {
        return IsType(o, object::ObjectType::Guest) && WantsSelection(o, preferSelected, occludingBuildingId) &&
                SelectGuestAt(o, screenPoint);
    };
    bool picked = false;
    if (!preferSelected) { // the tile itself is walked only on the free pass, and a later guest's answer replaces an earlier one
        cell_objects::ForEach(*game::GetObjectManager(), TileObjects(tile[0], tile[1]), [&](Object* o) {
            if (IsType(o, object::ObjectType::Guest) && WantsSelection(o, false, occludingBuildingId))
                picked = SelectGuestAt(o, screenPoint);
        });
    }
    return picked || VisitNeighbours(tile, tryGuest);
}

bool TrySelectStaff(bool preferSelected, int* screenPoint, int occludingBuildingId)
{
    uint32_t tile[2];
    TileOfScreenPoint(screenPoint, tile);
    auto tryStaff = [&](Object* o) {
        return gui_modes_tools::IsStaffType(static_cast<int>(o->m_type)) && WantsSelection(o, preferSelected, occludingBuildingId) &&
                SelectStaffAt(o, screenPoint);
    };
    const bool picked = cell_objects::ForEach(
            *game::GetObjectManager(), TileObjects(tile[0], tile[1]), [&](Object* o) { return static_cast<bool>(tryStaff(o)); });
    return picked || VisitNeighbours(tile, tryStaff);
}

bool RunSelectUnderCursor(ui::GuiModeDefault* mode)
{
    int cursor[2];
    CursorGetPos(game::GetCursor(), cursor);
    int occludingBuildingId = -1;
    const auto objects = ObjectsUnderCursor();
    for (size_t i = 0; i < objects.size() && occludingBuildingId == -1; ++i)
        if (IsType(objects[i], object::ObjectType::Building)) occludingBuildingId = static_cast<int>(objects[i]->m_id);
    if (TrySelectGuest(true, cursor, occludingBuildingId) || TrySelectStaff(true, cursor, occludingBuildingId) ||
            TrySelectGuest(false, cursor, occludingBuildingId) || TrySelectStaff(false, cursor, occludingBuildingId))
        return true;
    if (objects.empty()) return false;
    Object* first = objects[0];
    first->OnSelect(1, 0); // OnSelect(clicked, centerCamera)
    messages::Message message{};
    message.sender = message.receiver = static_cast<int>(first->m_id);
    message.type = 0x65;
    PostMessage(&message);
    return true;
}

void RunSelectGuestsInRectangle(ui::GuiModeDefault* mode)
{
    ClearSelectionOfClass(static_cast<uint32_t>(object::ObjectType::Guest));
    float low[2], high[2];
    gui_modes_tools::OrderSelectionAxis(mode->m_pressWorld[0], mode->m_releaseWorld[0], low[0], high[0]);
    gui_modes_tools::OrderSelectionAxis(mode->m_pressWorld[1], mode->m_releaseWorld[1], low[1], high[1]);
    const auto guests = ObjectsInRectangle(static_cast<uint32_t>(object::ObjectType::Guest), low, high);
    mode->m_selectedGuestCount = static_cast<int32_t>(guests.size());
    for (Object* guest : guests) guest->OnSelect(0, 0);
}

test::CallCounter g_queryCounter("ui.gui-mode-query"); // one per call of these seven

} // namespace

// call-drive GuiModeSwitch <mode>: GuiManager::SetMode on the real manager, so the next GuiManager::Update runs SwitchMode - the
// current mode's Exit and deleting destructor, the new class's Construct and Enter - and the mode's own Update / draws follow.
void RunSwitch(int mode)
{
    auto* gui = game::GetGuiManager();
    if (!gui) {
        OBL_LOG("autotest: FAILED gui-modes-switch needs gameplay (GuiManager)");
        return;
    }
    gui->SetMode(mode);
    g_switch.Count();
}

int AlbumComposeCount()
{
    return g_composed;
}

bool DozerCanRemoveAt(uint32_t x, uint32_t y)
{
    const uint32_t tile[2]{x, y};
    return RunCanRemove(tile);
}
bool DozerRemoveAt(uint32_t x, uint32_t y)
{
    const uint32_t tile[2]{x, y};
    return RemoveAt(tile);
}

bool RegisterToolModeDrivers()
{
    g_switch.SetDriver(&RunSwitch);
    g_clickEnter.SetDriver(&DriveClickEnter);
    g_clickFocus.SetDriver(&DriveClickFocus);
    g_clickAim.SetDriver(&DriveClickAim);
    g_clickCheck.SetDriver(&DriveClickCheck);
    return true;
}

namespace gui_modes {
ui::GuiMode* ConstructTool(int mode, ui::GuiManager* manager)
{
    switch (mode) {
    case 0: return new ui::GuiModePaint(manager);
    case 2: return new ui::GuiModeCamera(manager);
    case 4: return new ui::GuiModeDefault(manager);
    case 6: return new ui::GuiModeNone(manager);
    case 9: return new ui::GuiModeSendStaff(manager);
    case 10: return new ui::GuiModeDozer(manager);
    default: return nullptr;
    }
}
} // namespace gui_modes

} // namespace obl::gui

namespace obl::ui {

GuiModeCommon::GuiModeCommon(GuiManager* manager) : GuiMode(manager)
{
    gui::g_lifecycleCounter.Count();
}

GuiModeCommon::~GuiModeCommon()
{
    gui::g_lifecycleCounter.Count();
}

GuiModeCamera::GuiModeCamera(GuiManager* manager) : GuiModeCommon(manager)
{
    // The thumbnail list's sentinel, a block linked to itself.
    auto* head = static_cast<gui::Node*>(gui::PoolAllocate(gui::kListHeadBytes));
    head->m_next = head;
    head->m_prev = head;
    m_thumbs = head;
}

GuiModeCamera::~GuiModeCamera()
{
    gui::ClearThumbs(this);
    gui::PoolFree(m_thumbs, gui::kListHeadBytes);
}

void GuiModeCamera::Enter()
{
    gui::g_lifecycleCounter.Count();
    gui::EnterCamera(this);
}

void GuiModeDefault::Enter()
{
    gui::g_lifecycleCounter.Count();
    gui::EnterDefault(this);
}

void GuiModeSendStaff::Enter()
{
    gui::g_lifecycleCounter.Count();
    gui::EnterSendStaff(this);
}

void GuiModeCamera::Exit()
{
    gui::g_lifecycleCounter.Count();
    gui::ExitCamera(this);
}

void GuiModeDefault::Exit()
{
    gui::g_lifecycleCounter.Count();
    gui::ExitDefault(this);
}

void GuiModeDozer::Exit()
{
    gui::g_lifecycleCounter.Count();
    gui::ExitDozer(this);
}

void GuiModeCamera::Update(float dt)
{
    gui::g_updateCounter.Count();
    gui::UpdateCamera(this, dt);
}

void GuiModeDefault::Update(float dt)
{
    gui::g_updateCounter.Count();
    gui::UpdateDefault(this, dt);
}

void GuiModeCommon::Update(float dt)
{
    gui::g_updateCounter.Count();
    gui::UpdateCommon(this, dt);
}

bool GuiModeCommon::OnMouseWheel(int x, int y, int delta)
{
    return gui::GuiWidgetOnMouseWheel(nullptr, x, y, delta);
}

void GuiModePaint::Update(float dt)
{
    gui::g_updateCounter.Count();
    gui::UpdatePaint(this, dt);
}

void GuiModeCamera::DrawScreen()
{
    gui::RunDraw(this);
    gui::g_counter.Count();
}

bool GuiModeCommon::OnMouse(int x, int y, int left, int right)
{
    gui::g_mouseCounter.Count();
    return false;
}

void GuiModeCamera::ComposePhoto(int photo)
{
    gui::Compose(this, photo);
    gui::g_composeCounter.Count();
    ++gui::g_composed;
}

bool GuiModeDozer::CanRemove(uint32_t* tile)
{
    gui::g_queryCounter.Count();
    return gui::RunCanRemove(tile);
}

bool GuiModeSendStaff::CanDirect(uint32_t* tile, int* targetId)
{
    gui::g_queryCounter.Count();
    return gui::RunCanDirect(this, tile, targetId);
}

bool GuiModeDefault::SelectUnderCursor()
{
    gui::g_queryCounter.Count();
    return gui::RunSelectUnderCursor(this);
}

void GuiModeDefault::SelectGuestsInRectangle()
{
    gui::g_queryCounter.Count();
    gui::RunSelectGuestsInRectangle(this);
}

bool GuiModeCamera::OnMouse(int x, int y, int left, int right)
{
    gui::g_mouseCounter.Count();
    return gui::OnMouseCamera(this);
}

bool GuiModeDozer::OnMouse(int x, int y, int left, int right)
{
    gui::g_mouseCounter.Count();
    return gui::OnMouseDozer(this, x, y, left);
}

bool GuiModeDefault::OnMouse(int x, int y, int left, int right)
{
    gui::g_mouseCounter.Count();
    return gui::OnMouseDefault(this, x, y, left, right);
}

bool GuiModeSendStaff::OnMouse(int x, int y, int left, int right)
{
    gui::g_mouseCounter.Count();
    return gui::OnMouseSendStaff(this, x, y, left);
}

void GuiModeDozer::DrawWorld()
{
    gui::g_toolsDrawCounter.Count();
    gui::DrawDozer(this);
}

void GuiModeDefault::DrawScreen()
{
    gui::g_toolsDrawCounter.Count();
    gui::DrawDefault(this);
}

void GuiModeSendStaff::DrawWorld()
{
    gui::g_toolsDrawCounter.Count();
    gui::DrawStaff(this);
}

} // namespace obl::ui
