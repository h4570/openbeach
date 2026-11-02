#include "game/ui/gui_widget_virtual.h"
#include "gui_widgets_image_bar.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/renderer_api.h"
#include "game/ui/line_draw.h"
#include "gui_widgets.h"
#include "game/ui/sprite.h"
#include "gui_controls.h"
#include "game/ui/gui_data.h"
#include "gui_radar.h"
#include "game/ui/gui_definitions.h"
#include "game/game/game_accessors.h"
#include "game/ui/gui_radar.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/gui_controls_types.h"
#include "game/ui/map_view.h"
#include "game/objects/object_base.h"
#include "game/ui/renderer_types.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <cstring>
#include "game/ui/map_view_access.h"

namespace obl::gui {
namespace {
using ui::GuiManager;
using Manager = GuiManager;
using Progress = ui::GuiManager;
using namespace gui_radar;

// Call counters (`call-report ui.gui-radar`).
test::CallCounter g_counter("ui.gui-radar");
void CountCall()
{
    g_counter.Count();
}

Manager* Gui()
{
    return game::GetGuiManager();
}

void DestroyWidget(ui::GuiWidget* widget)
{
    gui::DestroyWidget(*widget);
}

void SetWidgetPosition(ui::GuiWidget* widget, int x, int y)
{
    widget->SetPosition(x, y);
}

Rect Bounds(ui::GuiWidget* widget)
{
    int out[4];
    widget->GetBounds(out);
    return {out[0], out[1], out[2], out[3]};
}

int SpriteHeight(ui::SpriteInstance* sprite)
{
    return sprite->m_sprite->m_anims[sprite->m_animIndex]->m_height;
}
int SpriteWidth(ui::SpriteInstance* sprite)
{
    return sprite->m_sprite->m_anims[sprite->m_animIndex]->m_width;
}

// The part of CreateRadar and UpdateRadarViewRect that reads the widget, the renderer and the map.
void WriteViewRect(Manager* gui)
{
    const Rect widget = Bounds(gui->m_radarWidget);
    const auto* renderer = static_cast<ui::Renderer*>(gui->m_renderer);
    const auto* map = ui::GetMapView();
    const ViewRect view = CalcViewRect(
            widget, static_cast<uint32_t>(renderer->m_width), static_cast<uint32_t>(renderer->m_height), map->m_mapWidth, map->m_mapHeight);
    gui->m_viewX0 = view.x0;
    gui->m_viewY0 = view.y0;
    gui->m_viewX1 = view.x1;
    gui->m_viewY1 = view.y1;
}

uint16_t Pack(int r, int g, int b)
{
    return obl::ui::renderer::PackColor(game::GetRenderer(), r, g, b);
}

void CreateRadar(Manager* gui, int x, int y, const char* image)
{
    void* const bank = ui::GetSpriteBank();
    if (gui->m_radarWidget) {
        DestroyWidget(gui->m_radarWidget);
        if (gui->m_radarButton) DestroyWidget(gui->m_radarButton);
        (static_cast<ui::SpriteBank*>(bank))->ReleaseInstance(gui->m_radarSprite);
    }
    auto* sprite = static_cast<ui::SpriteInstance*>((static_cast<ui::SpriteBank*>(bank))->CreateInstance(gui_data::g_RadarPanelDownSprite));
    gui->m_radarSprite = sprite;
    gui->m_panelX0 = x;
    gui->m_panelY0 = y;
    gui->m_panelX1 = SpriteWidth(sprite) + x;
    gui->m_panelY1 = SpriteHeight(sprite) + y;
    gui->m_radarEnabled = 1;
    gui->m_radarBig = 1;
    gui->m_radarDirty = 1;

    gui->m_radarWidget = static_cast<ui::GuiWidget*>(
            ImageConstruct(new ui::GuiImage(), x + 5, y + 0x13, gui->m_panelX1 + 5, gui->m_panelY1 + 0x13, image));
    obl::ui::renderer::SetSurfaceOrigin(gui->m_renderer, gui->m_radarSurface, x, y);
    WriteViewRect(gui);
    gui->m_colorViewRect = Pack(0, 0xfa, 0);
    gui->m_colorMarkerBlack = Pack(0, 0, 0);
    gui->m_colorMarkerRed = Pack(0xff, 0, 0);
    gui->m_colorMarkerTeal = Pack(0x54, 0x94, 0xac);
    gui->m_colorMarkerGrey = Pack(200, 200, 200);

    auto* button = button_facade::Construct(new ui::GuiButton(), gui->m_panelX0 + 4, gui->m_panelY0, gui->m_panelX1, gui->m_panelY1 + 4,
            gui_data::g_RadarUpButtonGraphic, gui_data::kButtonNoText, gui_data::kToggleRadarSizeAction, gui_data::kButtonNoText);
    gui->m_radarButton = button;
    button->SetTooltip(gui_data::g_RadarTooltipKey);
    static_cast<ui::GuiButton*>(button)->m_repeat = 0;
    button->SetVisible(1);
    gui->m_radarHovered = 0;
}

void UpdateRadarMouse(Manager* gui, int x, int y, int state)
{
    gui->m_radarHovered = 0;
    if (!gui->m_radarEnabled) return;
    gui->m_radarHovered =
            PanelHovered({gui->m_panelX0, gui->m_panelY0, gui->m_panelX1, gui->m_panelY1}, SpriteHeight(gui->m_radarSprite), x, y);
    auto* button = gui->m_radarButton;
    button->MouseLeft(x, y, state);
    if (gui->m_radarBig && gui->m_radarWidget->HitTestBase(x, y) && gui->m_radarMouseArmed) {
        gui->ClickMoveRadarCamera(x, y, state);
        gui->m_selection = -1;
    }
}

void ClickMoveCamera(Manager* gui, int x, int y, int state)
{
    if (state != 1 && state != 2) return;
    const auto* renderer = static_cast<ui::Renderer*>(gui->m_renderer);
    const auto* map = ui::GetMapView();
    const Point target = ClickCameraTarget(Bounds(gui->m_radarWidget), {gui->m_viewX0, gui->m_viewY0, gui->m_viewX1, gui->m_viewY1}, x, y,
            map->m_mapWidth, map->m_mapHeight, renderer->m_zoomLevel, static_cast<uint32_t>(renderer->m_width),
            static_cast<uint32_t>(renderer->m_height));
    ui::GetMapView()->SetCameraPos(target.x, target.y);
}

void ShiftViewRect(Manager* gui)
{
    if (!gui->m_radarEnabled || !gui->m_radarBig) return;
    const auto* map = ui::GetMapView();
    const ViewRect view = ShiftedViewRect(Bounds(gui->m_radarWidget), {gui->m_viewX0, gui->m_viewY0, gui->m_viewX1, gui->m_viewY1},
            map->m_cameraPositionX, map->m_cameraPositionY, map->m_mapWidth, map->m_mapHeight);
    gui->m_viewX0 = view.x0;
    gui->m_viewY0 = view.y0;
    gui->m_viewX1 = view.x1;
    gui->m_viewY1 = view.y1;
}

void FillRect(Manager* gui, const Rect& rect, uint32_t color)
{
    const int32_t packed[4] = {rect.x0, rect.y0, rect.x1, rect.y1};
    obl::ui::renderer::FillRect(gui->m_renderer, packed, color);
}

void PutPixel(Manager* gui, Point at, uint32_t color)
{
    obl::ui::renderer::PutPixel(gui->m_renderer, at.x, at.y, color);
}

void DrawMarkers(Manager* gui)
{
    const auto* objects = game::GetObjectManager();
    const Rect widget = Bounds(gui->m_radarWidget);
    const auto* map = ui::GetMapView();
    const MarkerFrame frame{static_cast<float>(widget.x1 - widget.x0), static_cast<float>(widget.y1 - widget.y0),
            static_cast<float>(widget.x0), static_cast<float>(widget.y0), static_cast<float>(map->m_mapWidth),
            static_cast<float>(map->m_mapHeight)};
    for (uint32_t i = 0; i < objects->m_buildings.size(); ++i) {
        const auto* building = static_cast<const object::Building*>(objects->m_buildings[i]);

        const auto* sprite = static_cast<const ui::SpriteInstance*>(building->m_sprite);
        const auto* def = building->m_definition;
        const bool mirrored = (sprite->m_direction & 1) != 0;
        const uint32_t width = mirrored ? def->m_GridHeight : def->m_GridWidth;
        const uint32_t depth = mirrored ? def->m_GridWidth : def->m_GridHeight;
        FillRect(gui, MarkerFootprint(frame, building->m_WorldXYZ[0], building->m_WorldXYZ[1], width, depth), gui->m_colorMarkerTeal);
    }
    for (uint32_t i = 0; i < objects->m_others.size(); ++i) {
        const object::Base* object = objects->m_others[i];
        const auto type = static_cast<uint8_t>(object->m_type);
        if (type < 7) continue;
        if (type <= 0xc)
            FillRect(gui, MarkerSquare(frame, object->m_WorldXYZ[0], object->m_WorldXYZ[1]), gui->m_colorMarkerRed);
        else if (type == 0xd)
            PutPixel(gui, MarkerPixel(frame, object->m_WorldXYZ[0], object->m_WorldXYZ[1]), gui->m_colorMarkerGrey);
    }
    for (uint32_t i = 0; i < objects->m_guests.size(); ++i) {
        const object::Base* guest = objects->m_guests[i];
        PutPixel(gui, MarkerPixel(frame, guest->m_WorldXYZ[0], guest->m_WorldXYZ[1]), gui->m_colorMarkerBlack);
    }
}

void DrawRadar(Manager* gui)
{
    if (!gui->m_radarBig) return;
    DrawMarkers(gui);
    Line lines[4];
    const int count = CameraRectLines(
            {gui->m_viewX0, gui->m_viewY0, gui->m_viewX1, gui->m_viewY1}, static_cast<ui::Renderer*>(gui->m_renderer)->m_zoomLevel, lines);
    for (int i = 0; i < count; ++i)
        ui::pixel_primitives::DrawLine(gui->m_renderer, lines[i].x0, lines[i].y0, lines[i].x1, lines[i].y1, gui->m_colorViewRect);
}

// ---- progress bar (below the HUD, holds the scenario goal dialog) ----
Progress* ProgressGui()
{
    return game::GetGuiManager();
}

void SetProgressOrigin(Progress* gui, int y)
{
    obl::ui::renderer::SetSurfaceOrigin(gui->m_renderer, gui->m_progressSurface, 9, y);
}

int ProgressY(const Progress* gui)
{
    const auto* renderer = static_cast<const ui::Renderer*>(gui->m_renderer);
    return ProgressBarY(static_cast<uint32_t>(renderer->m_width), static_cast<uint32_t>(renderer->m_height));
}

void CreateProgressBar(Progress* gui)
{
    const int y = ProgressY(gui);
    gui->m_progressDialog = nullptr;
    gui->m_progressVisible = 0;
    gui->m_progressBig = 1;
    gui->m_progressDirty = 1;
    auto* sprite = static_cast<ui::SpriteInstance*>(
            (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateInstance(gui_data::g_ProgressPanelDownSprite));
    gui->m_progressSprite = sprite;
    gui->m_progressX0 = 9;
    gui->m_progressY0 = y;

    gui->m_progressX1 = SpriteHeight(sprite) + 9;
    gui->m_progressY1 = SpriteWidth(sprite) + y;
    SetProgressOrigin(gui, y);
    auto* button = button_facade::Construct(new ui::GuiButton(), gui->m_progressX0 + 4, gui->m_progressY0 + 0x45, gui->m_progressX1,
            gui->m_progressY1, gui_data::g_ProgressDownButtonGraphic, gui_data::kButtonNoText, gui_data::kToggleProgressSizeAction,
            gui_data::kButtonNoText);
    gui->m_progressButton = button;
    button->SetTooltip(gui_data::g_ProgressTooltipKey);
    static_cast<ui::GuiButton*>(button)->m_repeat = 0;
    button->SetVisible(1);
    gui->m_progressHovered = 0;
    gui->m_progressDialogName[0] = 0;
}

void RelayoutProgressBar(Progress* gui)
{
    const int y = ProgressY(gui);
    gui->m_progressY0 = y;
    gui->m_progressY1 = SpriteWidth(gui->m_progressSprite) + y;
    SetWidgetPosition(gui->m_progressButton, Bounds(gui->m_progressButton).x0, y + 0x45);
    SetProgressOrigin(gui, y);
}

void SetProgressDialog(Progress* gui, const char* name)
{
    std::memcpy(gui->m_progressDialogName, name, std::strlen(name) + 1);
}

void ActivateProgressDialog(Progress* gui)
{
    RelayoutProgressBar(gui);
    gui->m_progressDialog = nullptr;
    for (auto* node = gui->m_definitions; node; node = node->m_next) {
        if (std::strcmp(static_cast<ui::GuiDialogData*>(node->m_dialog)->m_name, gui->m_progressDialogName) == 0) {
            gui->m_progressDialog = node->m_dialog;
            break;
        }
    }
    ui::GuiWidget* dialog = gui->m_progressDialog;
    if (!dialog) return;
    const Rect bounds = Bounds(dialog);
    dialog->Translate(gui->m_progressX0 - bounds.x0 + 5, gui->m_progressY0 - bounds.y0 + 6);
    dialog->Invalidate();
    dialog->SetOrigin(0, 0);
    gui->m_progressDirty = 1;
    auto* button = gui->m_progressButton;
    button->Invalidate();
}

void DrawProgressBar(Progress* gui)
{
    if (!gui->m_progressVisible) return;
    obl::ui::renderer::SetActiveSurface(gui->m_renderer, gui->m_progressSurface);
    if (gui->m_progressDirty) {
        obl::ui::renderer::ClearSurface(gui->m_renderer, gui->m_progressSurface);
        gui->m_progressDirty = 0;
        const Point at = ProgressSpritePos(gui->m_progressX0, gui->m_progressY0, gui->m_progressBig != 0);
        (static_cast<ui::SpriteInstance*>(gui->m_progressSprite))->DrawClipped(at.x, at.y, 0xffff, 99999.0f);
    }
    auto* button = gui->m_progressButton;
    button->Draw();
    if (gui->m_progressDialog && gui->m_progressBig) {
        auto* dialog = gui->m_progressDialog;
        dialog->Draw();
    }
}

} // namespace

void UpdateRadarViewRectDirect(void* gui)
{
    static_cast<ui::GuiManager*>(gui)->UpdateRadarViewRect();
}
} // namespace obl::gui

namespace obl::ui {
void GuiManager::CreateRadar(int x, int y, const char* image)
{
    gui::CountCall();
    gui::CreateRadar(this, x, y, image);
}
void GuiManager::SetProgressDialog(const char* name)
{
    gui::CountCall();
    gui::SetProgressDialog(this, name);
}
void GuiManager::UpdateRadarViewRect()
{
    gui::CountCall();
    gui::WriteViewRect(this);
}
void GuiManager::UpdateRadarMouse(int x, int y, int state)
{
    gui::CountCall();
    gui::UpdateRadarMouse(this, x, y, state);
}
void GuiManager::ClickMoveRadarCamera(int x, int y, int state)
{
    gui::CountCall();
    gui::ClickMoveCamera(this, x, y, state);
}
void GuiManager::ShiftRadarViewRect()
{
    gui::CountCall();
    gui::ShiftViewRect(this);
}
void GuiManager::DrawRadar()
{
    gui::CountCall();
    gui::DrawRadar(this);
}
void GuiManager::CreateProgressBar()
{
    gui::CountCall();
    gui::CreateProgressBar(this);
}
void GuiManager::RelayoutProgressBar()
{
    gui::CountCall();
    gui::RelayoutProgressBar(this);
}
void GuiManager::ActivateProgressDialog()
{
    gui::CountCall();
    gui::ActivateProgressDialog(this);
}
void GuiManager::DrawProgressBar()
{
    gui::CountCall();
    gui::DrawProgressBar(this);
}
} // namespace obl::ui
