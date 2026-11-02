#include "game/objects/scenery_names.h"
#include "core/key_code.h"
#include "core/ascii_case.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/cursor.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/sound_api.h"
#include "game/ui/renderer_api.h"
#include "game/map/grid_state.h"
#include "game/ui/sprite_instance_preview.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/map/path_queries.h"
#include "game/ui/renderer_types.h"
#include "game/ui/gui_modes_place.h"
#include "game/ui/gui_modes_tools.h"
#include "game/map/cell_objects.h"
#include "game/world/world_params.h"

#include "game/game/game_accessors.h"
#include "game/game/command_loop.h"
#include "game/map/grid_math.h"
#include "game/map/grid_cell.h"
#include "game/world/world_receive.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/gui_modes_types.h"
#include "game/tables/world_params_tables.h"
#include "game/objects/object_base.h"
#include "game/world/world_state.h"
#include "core/log.h"
#include "game/ui/input.h"
#include "game/ui/map_view_access.h"
#include "core/call_counter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "game/ui/map_view_math.h"
#include "game/ui/map_view.h"

namespace obl::gui {
namespace {
using namespace layout;
using messages::Message;
using ui::Sprite;
using ui::SpriteBank;
using ui::SpriteInstance;
using namespace ui;
using namespace tables;
using map::Grid;
using Base = GuiModePlaceBase;
using Scenery = GuiModePlaceScenery;
using Grass = GuiModePlaceGrass;
using Pavement = GuiModePlacePavement;
using Building = GuiModePlaceBuilding;
// Call counters (`call-report <name>`) and the live drivers the click tests run (`call-drive <name>`).
test::CallCounter g_counter("ui.gui-modes-place");
test::CallCounter g_live("GuiModesPlaceLive");
test::CallCounter g_liveWheel("GuiModesPlaceLiveWheel");

SpriteBank* Bank()
{
    return ui::GetSpriteBank();
}
path::PathTiles* PathTiles()
{
    return game::GetPathTiles();
}
SpriteInstance* Create(const char* name)
{
    return (static_cast<ui::SpriteBank*>(Bank()))->CreateObservedInstance(name);
}
void Release(SpriteInstance* sprite)
{
    (static_cast<ui::SpriteBank*>(Bank()))->ReleaseInstance(sprite);
}
uint16_t Color(unsigned r, unsigned g, unsigned b)
{
    return obl::ui::renderer::PackColor(game::GetRenderer(), r, g, b);
}
void Animate(SpriteInstance* sprite, const char* name)
{
    if (*name) (sprite)->SetAnimByName(name, 0.0f, nullptr);
}

void Enter(Scenery* scenery)
{
    scenery->m_definitionId = scenery->m_manager->m_context[5];
    scenery->m_definition = world::world_params::GetSceneryDef(game::GetWorldParams(), scenery->m_definitionId);
    scenery->m_preview = Create(scenery->m_definition->m_Sprite.c_str());
    Animate(scenery->m_preview, scenery->m_definition->m_Anim.c_str());
    scenery->m_validColor = Color(0, 250, 0);
    scenery->m_invalidColor = Color(250, 0, 0);
}
void Enter(Grass* grass)
{
    grass->m_definitionId = grass->m_manager->m_context[5];
    grass->m_definition = world::world_params::GetSceneryDef(game::GetWorldParams(), grass->m_definitionId);
    grass->m_preview = Create(PathTiles()->GetGrassSpriteName());
    grass->m_preview->m_sprite->m_type = 12;
    Animate(grass->m_preview, grass->m_definition->m_Anim.c_str());
    grass->m_preview->m_animType = 3;
}
void Enter(Pavement* pavement)
{
    pavement->m_definitionId = pavement->m_manager->m_context[5];
    pavement->m_definition = world::world_params::GetSceneryDef(game::GetWorldParams(), pavement->m_definitionId);
    pavement->m_preview = Create(PathTiles()->GetPavementSpriteName());
    pavement->m_preview->m_animType = 3;
    pavement->m_preview->m_frameIndex = PathTiles()->GetFullTileFrame();
}
void Enter(Building* building)
{
    building->m_definition = world::world_params::GetBuildingDef(game::GetWorldParams(), building->m_manager->m_context[0]);
    building->m_preview = Create(building->m_definition->m_Sprite.c_str());
    building->m_savedHotspotX = (building->m_preview)->GetDrawOffsetX();
    building->m_cancelHeld = 0;
    building->m_invalidColor = Color(255, 0, 0);
    building->m_neutralColor = Color(255, 255, 255);
    building->m_validColor = Color(0, 255, 0);
    building->m_canPlace = 1;
}
void Exit(Scenery* scenery)
{
    Release(scenery->m_preview);
}
void Exit(Grass* grass)
{
    Release(grass->m_preview);
}
void Exit(Pavement* pavement)
{
    Release(pavement->m_preview);
}
void Exit(Building* building)
{
    auto* sprite = building->m_preview;
    sprite->m_sprite->m_anims[sprite->m_animIndex]->m_hotspotX = static_cast<int16_t>(building->m_savedHotspotX);
    Release(sprite);
    static_cast<ui::GuiManager*>(building->m_manager)->CloseDialog("CancelBuildMode");
}
bool Wheel(SpriteInstance* sprite, int delta, bool building)
{
    const auto* data = sprite->m_sprite;
    sprite->m_direction = gui_modes_place::Rotate(
            sprite->m_direction, data->m_anims[0]->m_directions, data->m_anims[sprite->m_animIndex]->m_directions, delta, building);
    return true;
}

using gui_modes_place::TileXY;

// Thin wrappers over the game functions the placement code calls.
bool DrawHighlight(uint32_t x, uint32_t y, uint32_t color)
{
    return GetMapView()->DrawTileHighlight(x, y, color);
}
int PlaySound(const char* name, uint32_t flags, float fade)
{
    return audio::CurrentSound()->Play(name, static_cast<uint8_t>(flags), fade);
}
void RebuildPathTile(uint32_t x, uint32_t y, int removeNeighbours)
{
    PathTiles()->RebuildTile(x, y, removeNeighbours);
}

void SetRendererMoving(void* renderer, bool moving)
{
    static_cast<ui::Renderer*>(renderer)->m_directMode = moving ? 1 : 0;
}
void SetCursorShape(void* cursor, int shape)
{
    CursorSetShape(static_cast<ui::SoftwareCursor*>(cursor), shape);
}
void DrawSpriteInWorld(SpriteInstance* sprite, const float* world, float time)
{
    (sprite)->DrawInWorld(world, time);
}
void DrawBuildingGhost(SpriteInstance* sprite, const float* world, int tilesDir6, int tilesDir0, char ghost)
{
    (sprite)->DrawBuildingPreview(world, tilesDir6, tilesDir0, ghost);
}

void DrawTileOutline(GuiManager* manager, uint32_t x, uint32_t y, uint32_t color)
{
    manager->DrawTileOutline(x, y, color);
}

struct PlaceGridEnv {
    Grid* grid = game::GetGrid();
    void* definition{}; // BuildingDef (edge test) or SceneryDef (tile test)

    void Orient(TileXY* origin, uint32_t rows, uint32_t columns, int* columnDirection, int* rowDirection, int rotation) const
    {
        grid->OrientFootprintTraversal(
                &origin->x, static_cast<int>(rows), static_cast<int>(columns), columnDirection, rowDirection, rotation);
    }
    TileXY Neighbour(TileXY tile, int direction) const
    {
        return grid_math::Neighbour(grid->m_width, grid->m_height, tile.x, tile.y, direction);
    }
    uint32_t CellFlags(TileXY tile) const { return grid->Cell(tile.x, tile.y).m_flags; }
    bool EdgeOk(TileXY tile) const
    {
        return grid->CanPlaceBuildingEdge(tile.x, tile.y, *static_cast<const tables::BuildingDef*>(definition));
    }
    bool HasBin(TileXY tile) const
    {
        bool found = false;
        cell_objects::ForEach(*game::GetObjectManager(), grid->Cell(tile.x, tile.y),
                [&](const object::Base* object) { found |= scenery_names::IsBin(object->m_Name); });
        return found;
    }
    void Highlight(TileXY tile, uint16_t color) const { DrawHighlight(tile.x, tile.y, color); }
    bool CrossesBoundary(TileXY tile, int width, int height) const
    {
        return grid->IsFootprintCrossingMapBoundary(tile.x, tile.y, width, height);
    }
    bool SceneryTileOk(TileXY tile) const
    {
        return grid->CanPlaceSceneryTile(tile.x, tile.y, *static_cast<const tables::SceneryDef*>(definition));
    }
    bool OnlyLitter(TileXY tile) const
    {
        bool only = true;
        cell_objects::ForEach(*game::GetObjectManager(), grid->Cell(tile.x, tile.y),
                [&](const object::Base* base) { only &= static_cast<int>(base->m_type) == 1 && scenery_names::IsLitter(base->m_Name); });
        return only;
    }
};

uint32_t* RunGetOrigin(Building* building, uint32_t* out, uint32_t tileX, uint32_t tileY)
{
    const Grid* grid = game::GetGrid();
    const TileXY origin = gui_modes_place::FootprintOrigin(grid->m_width, grid->m_height, {tileX, tileY},
            building->m_definition->m_GridWidth, building->m_definition->m_GridHeight, building->m_preview->m_direction);
    out[0] = origin.x;
    out[1] = origin.y;
    return out;
}
void RunAdjustOrigin(Scenery* scenery, uint32_t* tile)
{
    const Grid* grid = game::GetGrid();
    const TileXY origin = gui_modes_place::FootprintOrigin(grid->m_width, grid->m_height, {tile[0], tile[1]},
            scenery->m_definition->m_GridWidth, scenery->m_definition->m_GridHeight, scenery->m_preview->m_direction);
    tile[0] = origin.x;
    tile[1] = origin.y;
}
bool RunBuildingCanPlace(Building* building, uint32_t tileX, uint32_t tileY, bool preview)
{
    gui_modes_place::BuildingPlacement placement;
    placement.gridWidth = building->m_definition->m_GridWidth;
    placement.gridHeight = building->m_definition->m_GridHeight;
    placement.rotation = building->m_preview->m_direction;
    placement.footprint = building->m_definition->m_footprint->m_layers[0];
    placement.validColor = building->m_validColor;
    placement.invalidColor = building->m_invalidColor;
    placement.preview = preview;
    PlaceGridEnv env;
    env.definition = building->m_definition;
    return gui_modes_place::CanPlaceBuilding(env, placement, {tileX, tileY});
}
bool RunSceneryCanPlace(Scenery* scenery, TileXY tile)
{
    PlaceGridEnv env;
    env.definition = scenery->m_definition;
    return gui_modes_place::CanPlaceScenery(
            env, scenery->m_definition->m_GridWidth, scenery->m_definition->m_GridHeight, scenery->m_preview->m_direction, tile);
}
// The surface modes' tile: submitted as `paint-surface`, which pays and paints at the start of the next tick (docs/architecture/commands.md).
// OnMouse's `m_canPlace` (the same tile and cash checks) predicts the outcome for the sound and the cursor.
template<class T> void SubmitPaint(T* mode, const uint32_t* tile)
{
    game::SubmitCommand(command::PaintSurface{mode->m_definition->m_key, static_cast<int32_t>(tile[0]), static_cast<int32_t>(tile[1])});
}
template<class T> void RunPlaceTile(T* mode, uint32_t* tile, bool playSound)
{
    if (!mode->m_canPlace) {
        if (playSound) PlaySound("click_bad_01", 1, 0.0f);
        return;
    }
    SubmitPaint(mode, tile);
    mode->m_lastX = static_cast<int32_t>(tile[0]);
    mode->m_lastY = static_cast<int32_t>(tile[1]);
    if (playSound) PlaySound("Cash", 1, 0.0f);
}

// The entries count as `call-report ui.gui-modes-place` like the entry points that forward to them.
uint32_t* GetOrigin(Building* building, uint32_t* out, uint32_t tileX, uint32_t tileY)
{
    g_counter.Count();
    return RunGetOrigin(building, out, tileX, tileY);
}
void AdjustOrigin(Scenery* scenery, uint32_t* tile)
{
    g_counter.Count();
    RunAdjustOrigin(scenery, tile);
}
bool BuildingCanPlace(Building* building, uint32_t tileX, uint32_t tileY, bool preview)
{
    g_counter.Count();
    return RunBuildingCanPlace(building, tileX, tileY, preview);
}
bool SceneryCanPlace(Scenery* scenery, TileXY tile)
{
    g_counter.Count();
    return RunSceneryCanPlace(scenery, tile);
}
template<class T> void PlaceTile(T* mode, uint32_t* tile, bool playSound)
{
    g_counter.Count();
    RunPlaceTile(mode, tile, playSound);
}

bool KeyPressed(int scancode)
{
    return InputKeyPressed(*game::GetInput(), scancode);
}
bool KeyHeld(int scancode)
{
    return InputKeyHeld(*game::GetInput(), scancode);
}
int LeftButton()
{
    return game::GetInput()->m_leftButton;
}
bool OverGui(GuiManager* manager)
{
    return manager->IsCursorOverGui(true);
}
void SetMode(GuiManager* manager, int mode)
{
    manager->SetMode(mode);
}
constexpr int kModeDefault = 4;

TileXY TileUnderView(int viewX, int viewY)
{
    MapView* view = GetMapView();
    const int screen[2]{viewX + view->m_cameraX, viewY + view->m_cameraY};
    float world[3];
    const float* point = view->ScreenToWorldPoint(world, screen);
    const grid_math::TileXY tile = game::GetGrid()->WorldPointToTile(point[0], point[1]);
    return {tile.x, tile.y};
}

void PlaceTileByGame(Grass* grass, uint32_t* tile, bool playSound)
{
    PlaceTile(grass, tile, playSound);
}
void PlaceTileByGame(Pavement* pavement, uint32_t* tile, bool playSound)
{
    PlaceTile(pavement, tile, playSound);
}

// Grass and paving: the tile under the cursor decides `m_canPlace`, a press (or a drag) places it, Esc returns to the default mode.
template<class T> bool TileMouse(T* mode, int x, int y, int left)
{
    constexpr bool grass = std::is_same_v<T, Grass>;
    World* world = game::GetWorld();
    const float scale = game::GetRenderer()->m_zoomScale;
    const TileXY under = TileUnderView(gui_modes_place::ScaledCursor(x, scale), gui_modes_place::ScaledCursor(y, scale));
    mode->m_tileX = under.x;
    mode->m_tileY = under.y;
    if (static_cast<uint32_t>(mode->m_lastX) != under.x || static_cast<uint32_t>(mode->m_lastY) != under.y)
        mode->m_lastX = mode->m_lastY = -1;

    const uint32_t flags = game::GetGrid()->Cell(under.x, under.y).m_flags;
    bool usable = grass ? gui_modes_place::GrassTileUsable(flags) : gui_modes_place::PavementTileUsable(flags);
    // The definition read here is the one of the previous frame: the id is refreshed below, after the check.
    if (!gui_modes_place::CashCoversTile(world->m_player->m_cash, mode->m_definition->m_Cost2Build)) usable = false;
    mode->m_canPlace = usable;
    if constexpr (grass) {
        const int picked = mode->m_manager->m_context[5];
        if (picked != mode->m_definitionId) {
            mode->m_definitionId = picked;
            mode->m_definition = world::world_params::GetSceneryDef(game::GetWorldParams(), picked);
            Animate(mode->m_preview, mode->m_definition->m_Anim.c_str());
            mode->m_preview->m_animType = 3;
        }
    }
    GuiManager* manager = mode->m_manager;
    if (OverGui(manager)) {
        mode->m_canPlace = 0;
    } else if (left == 1 || left == 2) {
        // A press on a refused tile is recorded as a rejected command; a drag submits only the tiles it paints.
        if (left == 1 && !mode->m_canPlace) SubmitPaint(mode, &mode->m_tileX);
        PlaceTileByGame(mode, &mode->m_tileX, false);
    }

    const bool leave = KeyPressed(obl::input::KeyCode::Escape);
    const bool justPlaced = mode->m_tileX == static_cast<uint32_t>(mode->m_lastX) && mode->m_tileY == static_cast<uint32_t>(mode->m_lastY);
    const int shape = gui_modes_place::PlaceCursorShape(mode->m_canPlace != 0, justPlaced, [&] { return OverGui(manager); });
    CursorSetShape(game::GetCursor(), shape);
    if (leave) {
        char panel[] = "SceneryPanel";
        manager->AnimateDialog(panel, 2, 1.0f, 0);
        SetMode(manager, kModeDefault);
    }
    return false;
}

// Building: a press on a free spot pays the price, posts the "create building" command and leaves the mode unless Shift is held; a press
// on a blocked spot (or without enough cash) only plays the refusal. Esc leaves, Space turns the preview.
void BuildingUpdate(Building* building, float dt)
{
    GuiManager* manager = building->m_manager;
    bool leave = false;
    manager->UpdateTickerAnimation(dt);
    if (LeftButton() == 1 && !OverGui(manager)) {
        int cursor[2];
        CursorGetPos(game::GetCursor(), cursor);
        const TileXY under = TileUnderView(cursor[0], cursor[1]);
        uint32_t scratch[2];
        const uint32_t* start = GetOrigin(building, scratch, under.x, under.y);
        const TileXY origin{start[0], start[1]};
        const bool fits = BuildingCanPlace(building, origin.x, origin.y, false);
        building->m_canPlace = fits;
        // Every press on the map is a `place-building` (a refused one is recorded as rejected); the executor pays and posts at the start
        // of the next tick. The mode's own checks only pick the sound and whether it leaves.
        game::SubmitCommand(command::PlaceBuilding{building->m_definition->m_key, static_cast<int32_t>(origin.x),
                static_cast<int32_t>(origin.y), building->m_preview->m_direction});
        if (fits) {
            const bool affordable =
                    gui_modes_place::CashCoversBuilding(game::GetWorld()->m_player->m_cash, building->m_definition->m_Cost2Build);
            PlaySound(affordable ? "Cash" : "click_bad_01", 1, 0.0f);
            if (!KeyHeld(obl::input::KeyCode::LeftShift) && !KeyHeld(obl::input::KeyCode::RightShift)) leave = true;
        } else {
            PlaySound("click_bad_01", 1, 0.0f);
        }
    }
    if (KeyPressed(obl::input::KeyCode::Escape)) leave = true;
    if (KeyPressed(obl::input::KeyCode::Space)) Wheel(building->m_preview, 1, true);
    if (leave) SetMode(manager, kModeDefault);
}

void SceneryUpdate(Scenery* scenery, float dt)
{
    GuiManager* manager = scenery->m_manager;
    manager->UpdateTickerAnimation(dt);
    const int picked = manager->m_context[5];
    if (picked == scenery->m_definitionId) return;
    scenery->m_definitionId = picked;
    scenery->m_definition = world::world_params::GetSceneryDef(game::GetWorldParams(), picked);
    if (scenery->m_preview) Release(scenery->m_preview);
    scenery->m_preview = Create(scenery->m_definition->m_Sprite.c_str());
    Animate(scenery->m_preview, scenery->m_definition->m_Anim.c_str());
}

// A press (exactly `left == 1`, not a held button) outside the panels turns the tile into the footprint's origin and places the object
// there when it fits and the cash covers the price (cash - price >= 0): the "create scenery" command, the price off the cash and onto the
// two expense counters, the Cash sound; otherwise only the refusal. The mode stays; Esc leaves it, Space turns the preview.
bool SceneryMouse(Scenery* scenery, int x, int y, int left)
{
    const float scale = game::GetRenderer()->m_zoomScale;
    const TileXY under = TileUnderView(gui_modes_place::ScaledCursor(x, scale), gui_modes_place::ScaledCursor(y, scale));
    scenery->m_tileX = under.x;
    scenery->m_tileY = under.y;
    GuiManager* manager = scenery->m_manager;
    if (left == 1 && !OverGui(manager)) {
        AdjustOrigin(scenery, &scenery->m_tileX);
        const bool fits = SceneryCanPlace(scenery, TileXY{scenery->m_tileX, scenery->m_tileY});
        // As in the building mode: every press is a `place-scenery`; the prediction picks the sound and the last placement.
        game::SubmitCommand(command::PlaceScenery{scenery->m_definition->m_key, static_cast<int32_t>(scenery->m_tileX),
                static_cast<int32_t>(scenery->m_tileY), scenery->m_preview->m_direction});
        if (fits && gui_modes_place::CashCoversTile(game::GetWorld()->m_player->m_cash, scenery->m_definition->m_Cost2Build)) {
            PlaySound("Cash", 1, 0.0f);
            scenery->m_lastX = static_cast<int32_t>(scenery->m_tileX);
            scenery->m_lastY = static_cast<int32_t>(scenery->m_tileY);
        } else {
            PlaySound("click_bad_01", 1, 0.0f);
        }
    }
    const bool leave = KeyPressed(obl::input::KeyCode::Escape);
    if (KeyPressed(obl::input::KeyCode::Space)) Wheel(scenery->m_preview, 1, false);
    if (leave) {
        char panel[] = "SceneryPanel";
        manager->AnimateDialog(panel, 2, 1.0f, 0);
        SetMode(manager, kModeDefault);
    }
    return false;
}

constexpr int kBuildingWorldKind = 1, kSceneryWorldKind = 4, kGrassWorldKind = 4, kPavementWorldKind = 1; // Grid::TileToWorldPoint `kind`

void* RendererObject()
{
    return game::GetRenderer();
}
void* CursorHandle()
{
    return game::GetCursor();
}
void CursorPixels(int* out)
{
    CursorGetPos(game::GetCursor(), out);
}
void TileToWorld(float* out, TileXY tile, int kind)
{
    const grid_math::WorldPoint point = game::GetGrid()->TileToWorldPoint(tile.x, tile.y, kind);
    out[0] = point.x;
    out[1] = point.y;
    out[2] = point.z;
}
void DrawPreviewAt(SpriteInstance* sprite, TileXY tile, int kind)
{
    float world[3];
    TileToWorld(world, tile, kind);
    SetRendererMoving(RendererObject(), false);
    DrawSpriteInWorld(sprite, world, game::GetWorld()->m_gameTime);
    SetRendererMoving(RendererObject(), true);
}

// Grass and paving: the sprite stands on the tile OnMouse stored, but only while that tile can be paid for.
template<class T> void TileDraw(T* mode)
{
    if (!mode->m_canPlace) return;
    DrawPreviewAt(mode->m_preview, {mode->m_tileX, mode->m_tileY}, std::is_same_v<T, Grass> ? kGrassWorldKind : kPavementWorldKind);
}

// Building: outside the panels the tile under the cursor gives the footprint's origin and `m_canPlace` (with the green / red tile highlights
// CanPlace paints), the ghost is red when it does not fit; the cursor shows the refusal shape then.
void BuildingDraw(Building* building)
{
    int cursor[2];
    CursorPixels(cursor);
    if (!OverGui(building->m_manager)) {
        const TileXY under = TileUnderView(cursor[0], cursor[1]);
        uint32_t scratch[2];
        const uint32_t* start = GetOrigin(building, scratch, under.x, under.y);
        const TileXY origin{start[0], start[1]};
        building->m_canPlace = BuildingCanPlace(building, origin.x, origin.y, true);
        const auto tiles = gui_modes_place::BuildingPreviewTiles(
                building->m_definition->m_GridWidth, building->m_definition->m_GridHeight, building->m_preview->m_direction);
        float world[3];
        TileToWorld(world, origin, kBuildingWorldKind);
        SetRendererMoving(RendererObject(), false);
        DrawBuildingGhost(building->m_preview, world, tiles.first, tiles.second, !building->m_canPlace);
        SetRendererMoving(RendererObject(), true);
        if (!building->m_canPlace) {
            SetCursorShape(CursorHandle(), gui_modes_place::kCursorRefused);
            return;
        }
    }
    SetCursorShape(CursorHandle(), gui_modes_place::kCursorNormal);
}

struct OutlineEnv {
    PlaceGridEnv grid;
    GuiManager* manager;
    TileXY Neighbour(TileXY tile, int direction) const { return grid.Neighbour(tile, direction); }
    void Outline(TileXY tile, uint16_t color) const { DrawTileOutline(manager, tile.x, tile.y, color); }
};

// Scenery: the footprint's tiles are outlined green when it fits and the cash covers the price (cash - price >= 0), red otherwise, and the
// cursor follows; over a panel only the normal cursor is set.
void SceneryDraw(Scenery* scenery)
{
    if (OverGui(scenery->m_manager)) {
        SetCursorShape(CursorHandle(), gui_modes_place::kCursorNormal);
        return;
    }
    int cursor[2];
    CursorPixels(cursor);
    const TileXY under = TileUnderView(cursor[0], cursor[1]);
    uint32_t tile[2]{under.x, under.y};
    AdjustOrigin(scenery, tile);
    const bool fits = SceneryCanPlace(scenery, {tile[0], tile[1]});
    const bool valid = fits && gui_modes_place::CashCoversTile(game::GetWorld()->m_player->m_cash, scenery->m_definition->m_Cost2Build);
    SetCursorShape(CursorHandle(), valid ? gui_modes_place::kCursorNormal : gui_modes_place::kCursorRefused);
    const OutlineEnv env{{}, scenery->m_manager};
    gui_modes_place::OutlineScenery(env, scenery->m_definition->m_GridWidth, scenery->m_definition->m_GridHeight,
            scenery->m_preview->m_direction, {tile[0], tile[1]}, valid ? scenery->m_validColor : scenery->m_invalidColor);
    DrawPreviewAt(scenery->m_preview, {tile[0], tile[1]}, kSceneryWorldKind);
}

template<class T> void EnterCounted(T* mode)
{
    g_counter.Count();
    Enter(mode);
}
} // namespace

namespace {
template<bool BuildingMode> bool HandleWheel(Base* mode, int x, int y, int delta)
{
    g_counter.Count();
    auto* sprite = BuildingMode ? static_cast<Building*>(mode)->m_preview : static_cast<Scenery*>(mode)->m_preview;
    return Wheel(sprite, delta, BuildingMode);
}

template<class T> bool HandleTileMouse(T* mode, int x, int y, int left, int right)
{
    g_counter.Count();
    return TileMouse(mode, x, y, left);
}

void RunDraw(Grass* grass)
{
    TileDraw(grass);
}
void RunDraw(Pavement* pavement)
{
    TileDraw(pavement);
}
void RunDraw(Building* building)
{
    BuildingDraw(building);
}
void RunDraw(Scenery* scenery)
{
    SceneryDraw(scenery);
}
template<class T> void DrawWorldCounted(T* mode)
{
    g_counter.Count();
    RunDraw(mode);
}

SceneryDef* SceneryDefinition(WorldParams* params, int index)
{
    return world::world_params::GetSceneryDef(params, index);
}

void DriveLive(int mode)
{
    auto* manager = game::GetGuiManager();
    if (!manager || !game::GetWorld() || (mode != 1 && mode != 4 && mode != 5 && mode != 7 && mode != 8)) {
        OBL_LOG("autotest: FAILED GuiModesPlaceLive invalid mode/gameplay");
        return;
    }
    manager->SetContext(0, 0);
    manager->SetContext(5, 0);
    manager->SetMode(mode);
    g_live.Count();
}
void DriveLiveWheel(int which)
{
    auto* manager = game::GetGuiManager();
    if (!manager || (manager->m_mode != 1 && manager->m_mode != 5) || which < 1 || which > 3) {
        OBL_LOG("autotest: FAILED GuiModesPlaceLiveWheel invalid mode/input");
        return;
    }
    auto* base = static_cast<Base*>(manager->m_modeObject);
    const bool building = manager->m_mode == 5;
    auto* sprite = building ? static_cast<Building*>(base)->m_preview : static_cast<Scenery*>(base)->m_preview;
    const int delta = which == 1 ? 240 : which == 2 ? -240 : 0;
    const int expected = gui_modes_place::Rotate(sprite->m_direction, sprite->m_sprite->m_anims[0]->m_directions,
            sprite->m_sprite->m_anims[sprite->m_animIndex]->m_directions, delta, building);
    const bool result = base->OnMouseWheel(-31, 4096, delta);
    g_liveWheel.Record(result && sprite->m_direction == expected, "live wheel=" + std::to_string(delta));
}

struct ClickPlan {
    bool armed{};
    int mode{}, variant{};
    TileXY tile{};
    int viewX{}, viewY{};
    uint32_t flags{};
    float cash{}, costs{}, scenery{}, construct{}, savedCash{};
    int built{};
    uint32_t buildings{};
    int32_t cost{};
    int direction{};
    TileXY origin{}; // scenery: the footprint's origin of `tile`
    int32_t lastX{}, lastY{}; // scenery: the last placement before the click
    uint32_t others{};
};
ClickPlan g_plan;
test::CallCounter g_clickEnter("GuiModesPlaceClickEnter"), g_clickAim("GuiModesPlaceClickAim"), g_clickCheck("GuiModesPlaceClickCheck");

void DriveClickEnter(int mode)
{
    auto* manager = game::GetGuiManager();
    auto* params = game::GetWorldParams();
    if (!manager || !game::GetWorld() || !params || (mode != 1 && mode != 5 && mode != 7 && mode != 8)) {
        OBL_LOG("autotest: FAILED GuiModesPlaceClickEnter invalid mode/gameplay");
        return;
    }
    // The scenery mode places objects (not category 1); the surface modes paint category 1, grass or path as the panel picks the mode.
    const auto suits = [&](const SceneryDef* def) {
        if (mode == 1 || mode == 5) return def->m_Category != 1;
        return def->m_Category == 1 && IsGrass(*def) == (mode == 8);
    };
    int scenery = -1, building = -1;
    for (int i = 0; i < params->m_numScenery; ++i) {
        const SceneryDef* def = SceneryDefinition(params, i);
        if (def->m_Cost2Build > 0 && suits(def) && (scenery < 0 || def->m_Cost2Build < SceneryDefinition(params, scenery)->m_Cost2Build))
            scenery = i;
    }
    long bestArea = 0;
    for (int i = 0; i < params->m_numBuildings; ++i) {
        const auto* def = world::world_params::GetBuildingDef(params, i);
        const long area = static_cast<long>(def->m_GridWidth) * def->m_GridHeight;
        if (def->m_Cost2Build > 0 && def->m_footprint && area > 0 && (building < 0 || area < bestArea)) {
            building = i;
            bestArea = area;
        }
    }
    if (scenery < 0 || building < 0) {
        OBL_LOG("autotest: FAILED GuiModesPlaceClickEnter no priced definitions");
        return;
    }
    manager->SetContext(0, building);
    manager->SetContext(5, scenery);
    SetMode(manager, mode);
    g_clickEnter.Count();
}

// The entered mode object, whichever of the three classes it is.
struct ModeView {
    GuiManager* tree;
    Base* base;
    int32_t cost;
    SpriteInstance* preview;
    bool Accepts(TileXY tile, uint32_t* flags) const
    {
        *flags = game::GetGrid()->Cell(tile.x, tile.y).m_flags;
        if (tree->m_mode == 1) {
            auto* mode = static_cast<Scenery*>(base);
            uint32_t pair[2]{tile.x, tile.y};
            AdjustOrigin(mode, pair);
            return SceneryCanPlace(mode, {pair[0], pair[1]});
        }
        if (tree->m_mode == 8) return gui_modes_place::GrassTileUsable(*flags);
        if (tree->m_mode == 7) return gui_modes_place::PavementTileUsable(*flags);
        auto* mode = static_cast<Building*>(base);
        uint32_t scratch[2];
        const uint32_t* origin = GetOrigin(mode, scratch, tile.x, tile.y);
        return BuildingCanPlace(mode, origin[0], origin[1], false);
    }
};
bool ViewOfMode(int mode, ModeView* out)
{
    auto* tree = game::GetGuiManager();
    if (!tree || tree->m_mode != mode || !tree->m_modeObject) return false;
    auto* base = static_cast<Base*>(tree->m_modeObject);
    if (mode == 1) {
        auto* scenery = static_cast<Scenery*>(base);
        *out = {tree, base, scenery->m_definition->m_Cost2Build, scenery->m_preview};
    } else if (mode == 5) {
        auto* building = static_cast<Building*>(base);
        *out = {tree, base, building->m_definition->m_Cost2Build, building->m_preview};
    } else if (mode == 8) {
        auto* grass = static_cast<Grass*>(base);
        *out = {tree, base, grass->m_definition->m_Cost2Build, grass->m_preview};
    } else {
        auto* pavement = static_cast<Pavement*>(base);
        *out = {tree, base, pavement->m_definition->m_Cost2Build, pavement->m_preview};
    }
    return true;
}

// Focus: the first tile of the whole map the mode accepts, brought to the middle of the view (the camera follows on the next frames).
test::CallCounter g_clickFocus("GuiModesPlaceClickFocus");
void DriveClickFocus(int mode)
{
    ModeView view;
    if (!game::GetWorld() || !ViewOfMode(mode, &view)) {
        OBL_LOG("autotest: FAILED GuiModesPlaceClickFocus needs gameplay and the mode entered");
        return;
    }
    const Grid* grid = game::GetGrid();
    int accepted = 0;
    TileXY target{};
    for (uint32_t y = 0; y < grid->m_height; ++y) {
        for (uint32_t x = 0; x < grid->m_width; ++x) {
            uint32_t flags;
            if (view.Accepts({x, y}, &flags)) {
                if (!accepted++) target = {x, y};
            }
        }
    }
    if (!accepted) {
        g_clickFocus.Record(false, "no accepting tile on the map for mode=" + std::to_string(mode));
        return;
    }
    MapView* map = GetMapView();
    const uint32_t tile[2]{target.x, target.y};
    int pixel[2];
    map->TileToScreen(pixel, tile);
    map->SetCameraPos(pixel[0] - 400, pixel[1] - 300);
    g_clickFocus.Count();
}

void DriveClickAim(int arg)
{
    const int mode = arg % 10, variant = arg / 10;
    ModeView view;
    World* world = game::GetWorld();
    if (!world || variant > 4 || !ViewOfMode(mode, &view) || game::GetRenderer()->m_zoomScale != 1.0f) {
        OBL_LOG("autotest: FAILED GuiModesPlaceClickAim needs gameplay, the mode entered and zoom scale 1");
        return;
    }
    ClickPlan plan;
    plan.mode = mode;
    plan.variant = variant;
    // The accepting tile nearest to the middle of the view (the panels are at its edges).
    bool found = false;
    int best = 0;
    for (int y = 120; y <= 420; y += 6) {
        for (int x = 200; x <= 600; x += 6) {
            const TileXY tile = TileUnderView(x, y);
            uint32_t flags;
            const int distance = (x - 400) * (x - 400) + (y - 300) * (y - 300);
            if (view.Accepts(tile, &flags) == (variant != 1) && (!found || distance < best)) {
                plan.tile = tile;
                plan.flags = flags;
                plan.viewX = x;
                plan.viewY = y;
                found = true;
                best = distance;
            }
        }
    }
    if (!found) {
        g_clickAim.Record(false, "no tile for mode=" + std::to_string(mode) + " variant=" + std::to_string(variant));
        return;
    }
    Player* player = world->m_player;
    plan.cost = view.cost;
    plan.direction = view.preview->m_direction;
    plan.savedCash = player->m_cash;
    if (variant == 2) player->m_cash = 0.0f;
    plan.cash = player->m_cash;
    plan.costs = world->m_costsToday;
    plan.scenery = world->m_sceneryCostToday;
    plan.construct = world->m_constructCostToday;
    plan.built = world->m_buildingsConstructed;
    plan.buildings = game::GetObjectManager()->m_buildings.size();
    plan.others = game::GetObjectManager()->m_others.size();
    if (mode == 1) {
        auto* scenery = static_cast<Scenery*>(view.base);
        uint32_t pair[2]{plan.tile.x, plan.tile.y};
        AdjustOrigin(scenery, pair);
        plan.origin = {pair[0], pair[1]};
        plan.lastX = scenery->m_lastX;
        plan.lastY = scenery->m_lastY;
    }
    plan.armed = true;
    g_plan = plan;
    platform::SetSyntheticCursor(plan.viewX, plan.viewY);
    g_clickAim.Count();
}

template<class T> bool SameBits(T a, T b)
{
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

void DriveClickCheck(int arg)
{
    const int mode = arg % 10, variant = arg / 10;
    World* world = game::GetWorld();
    auto* tree = game::GetGuiManager();
    const ClickPlan& plan = g_plan;
    if (!world || !tree || !plan.armed || plan.mode != mode || plan.variant != variant) {
        OBL_LOG("autotest: FAILED GuiModesPlaceClickCheck without a matching Aim");
        return;
    }
    Player* player = world->m_player;
    const bool building = mode == 5, scenery = mode == 1;

    const bool pays = (variant == 0 || variant == 4) && plan.cost > 0;
    std::string why;
    const auto expect = [&](bool condition, const char* what) {
        if (!condition) why += std::string(what) + " ";
    };
    const uint32_t bits = mode == 8 ? gui_modes_place::kGrassFlags : gui_modes_place::kPavementFlags;
    const uint32_t flags = game::GetGrid()->Cell(plan.tile.x, plan.tile.y).m_flags;
    if (mode == 7 || mode == 8) expect(flags == (pays ? (plan.flags | bits) : plan.flags), "tile flags");
    expect(SameBits(player->m_cash, pays ? gui_modes_place::CashAfterPlacing(plan.cash, plan.cost) : plan.cash), "cash");
    expect(SameBits(world->m_costsToday, pays ? gui_modes_place::ExpensesAfterPlacing(plan.costs, plan.cost) : plan.costs), "costs today");
    expect(SameBits(world->m_sceneryCostToday,
                   pays && !building ? gui_modes_place::ExpensesAfterPlacing(plan.scenery, plan.cost) : plan.scenery),
            "scenery costs");
    expect(SameBits(world->m_constructCostToday,
                   pays && building ? gui_modes_place::ExpensesAfterPlacing(plan.construct, plan.cost) : plan.construct),
            "construct costs");
    expect(world->m_buildingsConstructed == plan.built + (pays && building ? 1 : 0), "buildings constructed");
    if (building) expect(game::GetObjectManager()->m_buildings.size() == plan.buildings + (pays ? 1 : 0), "building objects");
    // Tile modes stay; the building mode leaves after a purchase or a refusal for lack of cash unless Shift is held.
    const bool leaves = building && (variant == 0 || variant == 2);
    expect(tree->m_mode == (leaves ? kModeDefault : mode), "mode");
    if (scenery) {
        // Scenery is placed by the message the click posts, so the object appears a few frames later (the script steps before this check).
        expect(game::GetObjectManager()->m_others.size() == plan.others + (pays ? 1 : 0), "scenery objects");
        if (tree->m_mode == 1) {
            const auto* current = static_cast<Scenery*>(tree->m_modeObject);
            // OnMouse stores the tile under the cursor every frame; only a press turns it into the origin, and the last placement keeps it.
            expect(current->m_tileX == plan.tile.x && current->m_tileY == plan.tile.y, "tile under the cursor");
            expect(current->m_lastX == (pays ? static_cast<int32_t>(plan.origin.x) : plan.lastX) &&
                            current->m_lastY == (pays ? static_cast<int32_t>(plan.origin.y) : plan.lastY),
                    "last placement");
            if (variant == 3) {
                const auto* preview = current->m_preview;
                const auto* data = preview->m_sprite;
                expect(preview->m_direction ==
                                gui_modes_place::Rotate(plan.direction, data->m_anims[0]->m_directions,
                                        data->m_anims[preview->m_animIndex]->m_directions, 1, false),
                        "direction");
            }
        }
    }
    if (building && variant == 3 && tree->m_mode == 5) {
        const auto* preview = static_cast<Building*>(tree->m_modeObject)->m_preview;
        const auto* data = preview->m_sprite;
        expect(preview->m_direction ==
                        gui_modes_place::Rotate(
                                plan.direction, data->m_anims[0]->m_directions, data->m_anims[preview->m_animIndex]->m_directions, 1, true),
                "direction");
    }
    if (variant == 2) player->m_cash = plan.savedCash; // the test took the cash away; give it back
    if (!why.empty()) {
        char buffer[400];
        std::snprintf(buffer, sizeof(buffer),
                "click check mode=%d variant=%d tile=%u,%u view=%d,%d cost=%d flags %08x -> %08x cash %g -> %g built %d -> %d buildings %u "
                "-> %zu gui mode %d",
                mode, variant, plan.tile.x, plan.tile.y, plan.viewX, plan.viewY, plan.cost, plan.flags, flags, plan.cash, player->m_cash,
                plan.built, world->m_buildingsConstructed, plan.buildings, game::GetObjectManager()->m_buildings.size(), tree->m_mode);
        OBL_LOG("autotest: %s", buffer);
    }
    g_plan.armed = false;
    g_clickCheck.Record(why.empty(), "mode=" + std::to_string(mode) + " variant=" + std::to_string(variant) + " failed: " + why);
}

// Update of the scenery mode: Pick selects another scenery in the panel's context (one with a different sprite) and Picked, a few frames later,
// expects the mode to have followed it: id, definition, a fresh unrotated preview of the new sprite.
test::CallCounter g_sceneryPick("GuiModesPlaceSceneryPick"), g_sceneryPicked("GuiModesPlaceSceneryPicked");
int g_pickedId = -1;
const Sprite* g_pickedFrom{};
void DriveSceneryPick(int step)
{
    auto* tree = game::GetGuiManager();
    auto* params = game::GetWorldParams();
    if (!tree || !params || tree->m_mode != 1 || !tree->m_modeObject || step <= 0) {
        OBL_LOG("autotest: FAILED GuiModesPlaceSceneryPick needs the scenery mode and a step");
        return;
    }
    auto* scenery = static_cast<Scenery*>(tree->m_modeObject);
    for (int i = 1; i <= params->m_numScenery; ++i) {
        const int id = (scenery->m_definitionId + i * step) % params->m_numScenery;
        if (std::strcmp(SceneryDefinition(params, id)->m_Sprite.c_str(), scenery->m_definition->m_Sprite.c_str()) == 0) continue;
        g_pickedId = id;
        g_pickedFrom = scenery->m_preview->m_sprite;
        scenery->m_preview->m_direction = 1; // a fresh preview starts at direction 0
        game::GetGuiManager()->SetContext(5, id);
        g_sceneryPick.Count();
        return;
    }
    g_sceneryPick.Record(false, "no scenery with another sprite");
}
void DrivePickedCheck(int)
{
    auto* tree = game::GetGuiManager();
    auto* params = game::GetWorldParams();
    if (!tree || !params || tree->m_mode != 1 || !tree->m_modeObject || g_pickedId < 0) {
        OBL_LOG("autotest: FAILED GuiModesPlaceSceneryPicked needs the scenery mode after Pick");
        return;
    }
    const auto* scenery = static_cast<const Scenery*>(tree->m_modeObject);
    const SceneryDef* definition = SceneryDefinition(params, g_pickedId);
    std::string why;
    if (scenery->m_definitionId != g_pickedId) why += "id ";
    if (scenery->m_definition != definition) why += "definition ";
    if (!scenery->m_preview || scenery->m_preview->m_direction != 0) why += "direction ";
    // A few scenery sprites (the paving and grass pseudo-items) are not in the bank, the game then shows its "dummy"; the old preview is gone either way.
    if (!scenery->m_preview || scenery->m_preview->m_sprite == g_pickedFrom) why += "sprite ";
    g_sceneryPicked.Record(why.empty(),
            "id=" + std::to_string(g_pickedId) + " failed: " + why + " sprite=" + std::string(definition->m_Sprite) +
                    " preview=" + std::string(scenery->m_preview && scenery->m_preview->m_name ? scenery->m_preview->m_name : "(null)"));
}

} // namespace

bool BuildingFits(const BuildingDef& definition, uint32_t x, uint32_t y, int direction)
{
    gui_modes_place::BuildingPlacement placement;
    placement.gridWidth = definition.m_GridWidth;
    placement.gridHeight = definition.m_GridHeight;
    placement.rotation = direction;
    placement.footprint = definition.m_footprint->m_layers[0];
    PlaceGridEnv env;
    env.definition = const_cast<BuildingDef*>(&definition);
    return gui_modes_place::CanPlaceBuilding(env, placement, {x, y});
}

bool SceneryFits(const SceneryDef& definition, uint32_t x, uint32_t y, int direction)
{
    PlaceGridEnv env;
    env.definition = const_cast<SceneryDef*>(&definition);
    return gui_modes_place::CanPlaceScenery(env, definition.m_GridWidth, definition.m_GridHeight, direction, {x, y});
}

bool BuyBuilding(const BuildingDef& definition, uint32_t x, uint32_t y, int direction)
{
    World* world = game::GetWorld();
    Player* player = world->m_player;
    const int32_t cost = definition.m_Cost2Build;
    if (!gui_modes_place::CashCoversBuilding(player->m_cash, cost)) return false;
    player->m_cash = gui_modes_place::CashAfterPlacing(player->m_cash, cost);
    world->m_costsToday = gui_modes_place::ExpensesAfterPlacing(world->m_costsToday, cost);
    world->m_constructCostToday = gui_modes_place::ExpensesAfterPlacing(world->m_constructCostToday, cost);
    ++world->m_buildingsConstructed;
    Message message = gui_modes_place::BuildingCommandMessage();
    world_script::ScriptPlacement placement = gui_modes_place::BuildingCommandPlacement(definition.m_key.c_str(), {x, y}, direction);
    world::PostPlaceCommand(message, &placement);
    return true;
}

// The scenery mode posts first and pays after; the order is kept although nothing between the two reads the cash.
bool BuyScenery(const SceneryDef& definition, uint32_t x, uint32_t y, int direction)
{
    World* world = game::GetWorld();
    Player* player = world->m_player;
    const int32_t cost = definition.m_Cost2Build;
    if (!gui_modes_place::CashCoversTile(player->m_cash, cost)) return false;
    Message message = gui_modes_place::SceneryCommandMessage();
    world_script::ScriptPlacement placement =
            gui_modes_place::SceneryCommandPlacement(definition.m_key.c_str(), definition.m_Name.c_str(), {x, y}, direction);
    world::PostPlaceCommand(message, &placement);
    player->m_cash = gui_modes_place::CashAfterPlacing(player->m_cash, cost);
    world->m_costsToday = gui_modes_place::ExpensesAfterPlacing(world->m_costsToday, cost);
    world->m_sceneryCostToday = gui_modes_place::ExpensesAfterPlacing(world->m_sceneryCostToday, cost);
    return true;
}

bool IsGrass(const SceneryDef& definition)
{
    return definition.m_PathFlag != 0;
}

bool SurfaceFits(const SceneryDef& definition, uint32_t x, uint32_t y)
{
    const uint32_t flags = game::GetGrid()->Cell(x, y).m_flags;
    return IsGrass(definition) ? gui_modes_place::GrassTileUsable(flags) : gui_modes_place::PavementTileUsable(flags);
}

void PaintSurfaceTile(const SceneryDef& definition, uint32_t x, uint32_t y)
{
    World* world = game::GetWorld();
    const int32_t cost = definition.m_Cost2Build;
    world->m_player->m_cash = gui_modes_place::CashAfterPlacing(world->m_player->m_cash, cost);
    world->m_costsToday = gui_modes_place::ExpensesAfterPlacing(world->m_costsToday, cost);
    world->m_sceneryCostToday = gui_modes_place::ExpensesAfterPlacing(world->m_sceneryCostToday, cost);
    game::GetGrid()->Cell(x, y).m_flags |= IsGrass(definition) ? gui_modes_place::kGrassFlags : gui_modes_place::kPavementFlags;
    RebuildPathTile(x, y, 0);
}

ui::GuiMode* ConstructPlaceMode(int mode, ui::GuiManager* manager)
{
    switch (mode) {
    case 1: return new Scenery(manager);
    case 5: return new Building(manager);
    case 7: return new Pavement(manager);
    case 8: return new Grass(manager);
    default: return nullptr;
    }
}

bool RegisterPlaceModeDrivers()
{
    g_live.SetDriver(DriveLive);
    g_liveWheel.SetDriver(DriveLiveWheel);
    g_clickEnter.SetDriver(DriveClickEnter);
    g_clickFocus.SetDriver(DriveClickFocus);
    g_clickAim.SetDriver(DriveClickAim);
    g_clickCheck.SetDriver(DriveClickCheck);
    g_sceneryPick.SetDriver(DriveSceneryPick);
    g_sceneryPicked.SetDriver(DrivePickedCheck);
    return true;
}
} // namespace obl::gui

namespace obl::ui {

GuiModePlaceBase::GuiModePlaceBase(GuiManager* manager) : GuiModeCommon(manager)
{
    gui::g_counter.Count();
}

GuiModePlaceBase::~GuiModePlaceBase()
{
    gui::g_counter.Count();
}

void GuiModePlaceGrass::Exit()
{
    gui::g_counter.Count();
    gui::Exit(this);
}

void GuiModePlaceScenery::Exit()
{
    gui::g_counter.Count();
    gui::Exit(this);
}

void GuiModePlacePavement::Exit()
{
    gui::g_counter.Count();
    gui::Exit(static_cast<GuiModePlacePavement*>(this));
}

void GuiModePlaceBuilding::Exit()
{
    gui::g_counter.Count();
    gui::Exit(static_cast<GuiModePlaceBuilding*>(this));
}

void GuiModePlaceBuilding::Update(float dt)
{
    gui::g_counter.Count();
    gui::BuildingUpdate(this, dt);
}

void GuiModePlaceScenery::Update(float dt)
{
    gui::g_counter.Count();
    gui::SceneryUpdate(this, dt);
}

bool GuiModePlaceScenery::OnMouse(int x, int y, int left, int right)
{
    gui::g_counter.Count();
    return gui::SceneryMouse(this, x, y, left);
}

void GuiModePlaceScenery::Enter()
{
    gui::EnterCounted<GuiModePlaceScenery>(static_cast<GuiModePlaceScenery*>(this));
}

void GuiModePlaceScenery::DrawWorld()
{
    gui::DrawWorldCounted<GuiModePlaceScenery>(static_cast<GuiModePlaceScenery*>(this));
}

void GuiModePlaceGrass::Enter()
{
    gui::EnterCounted<GuiModePlaceGrass>(static_cast<GuiModePlaceGrass*>(this));
}

void GuiModePlaceGrass::DrawWorld()
{
    gui::DrawWorldCounted<GuiModePlaceGrass>(static_cast<GuiModePlaceGrass*>(this));
}

void GuiModePlacePavement::Enter()
{
    gui::EnterCounted<GuiModePlacePavement>(static_cast<GuiModePlacePavement*>(this));
}

void GuiModePlacePavement::DrawWorld()
{
    gui::DrawWorldCounted<GuiModePlacePavement>(static_cast<GuiModePlacePavement*>(this));
}

void GuiModePlaceBuilding::Enter()
{
    gui::EnterCounted<GuiModePlaceBuilding>(static_cast<GuiModePlaceBuilding*>(this));
}

void GuiModePlaceBuilding::DrawWorld()
{
    gui::DrawWorldCounted<GuiModePlaceBuilding>(static_cast<GuiModePlaceBuilding*>(this));
}

bool GuiModePlaceScenery::OnMouseWheel(int x, int y, int delta)
{
    return gui::HandleWheel<false>(this, x, y, delta);
}

bool GuiModePlaceBuilding::OnMouseWheel(int x, int y, int delta)
{
    return gui::HandleWheel<true>(this, x, y, delta);
}

bool GuiModePlaceGrass::OnMouse(int x, int y, int left, int right)
{
    return gui::HandleTileMouse<GuiModePlaceGrass>(this, x, y, left, right);
}

bool GuiModePlacePavement::OnMouse(int x, int y, int left, int right)
{
    return gui::HandleTileMouse<GuiModePlacePavement>(this, x, y, left, right);
}

} // namespace obl::ui
