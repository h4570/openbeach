#pragma once

#include "core/float_to_int.h"
#include "game/map/grid_math.h"
#include "game/messages/message_state.h"
#include "game/world/world_script_types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include "game/tables/world_params_tables.h"

namespace obl::gui_modes_place {

// The wheel magnitude and cursor position do not affect rotation. Building previews
// wrap by animation zero, then clamp to the current animation's direction count.
inline int Rotate(int direction, int firstDirections, int currentDirections, int delta, bool building)
{
    const int count = building ? firstDirections : currentDirections;
    int result = delta > 0 ? (direction + 1) % count : direction - 1;
    if (delta <= 0 && result < 0) result = count - 1;
    if (result >= currentDirections) result = currentDirections - 1;
    return result;
}

using grid_math::TileXY;

constexpr uint32_t kGrassFlags = 0x7000;
constexpr uint32_t kPavementFlags = 0x10000080;
constexpr int kFootprintMargin = 2; // the footprint layers cover (width + 2) x (height + 2) tiles
constexpr const char* kBinName = "Bin";
constexpr const char* kLitterName = "Litter";

// The footprint origin of the building and scenery place modes: the tile under the cursor becomes the footprint's corner. With an even rotation the point moves height/2 steps in direction 4, then width/2 steps in direction 2;
// an odd rotation (bit 0 of SpriteInstance::m_direction, so also -1) swaps the two dimensions. The division truncates toward zero, so a
// non-positive dimension moves nothing, and a tile that does not move is returned as given (not wrapped to the map).
inline TileXY FootprintOrigin(uint32_t mapWidth, uint32_t mapHeight, TileXY tile, int gridWidth, int gridHeight, int direction)
{
    int stepsDirection4 = gridHeight / 2;
    int stepsDirection2 = gridWidth / 2;
    if ((direction & 1) == 1) std::swap(stepsDirection4, stepsDirection2);
    for (int i = 0; i < stepsDirection4; ++i) tile = grid_math::Neighbour(mapWidth, mapHeight, tile.x, tile.y, 4);
    for (int i = 0; i < stepsDirection2; ++i) tile = grid_math::Neighbour(mapWidth, mapHeight, tile.x, tile.y, 2);
    return tile;
}

// What the building place mode's placement test reads of the building definition and of the mode.
struct BuildingPlacement {
    int gridWidth = 0, gridHeight = 0; // BuildingDef::m_GridWidth / m_GridHeight
    int rotation = 0; // SpriteInstance::m_direction of the preview
    const int32_t (*footprint)[tables::kFootprintCells] = nullptr; // BuildingFootprint::m_layers[0], by row, then column
    uint16_t validColor = 0, invalidColor = 0;
    bool preview = false;
};

template<class Env> bool CanPlaceBuilding(Env& env, const BuildingPlacement& building, TileXY tile)
{
    // The placement transform only supports quarter turns, even though traversal also handles mirrored directions.
    if (building.rotation < 0 || building.rotation > 3) return false;
    const uint32_t width = static_cast<uint32_t>(building.gridWidth) + kFootprintMargin;
    const uint32_t height = static_cast<uint32_t>(building.gridHeight) + kFootprintMargin;
    const bool turned = (building.rotation & 1) == 1;
    const uint32_t columns = turned ? height : width;
    const uint32_t rows = turned ? width : height;

    TileXY origin = tile;
    int columnDirection = 0, rowDirection = 0;
    env.Orient(&origin, rows, columns, &columnDirection, &rowDirection, building.rotation);
    TileXY cursor = origin;
    bool result = true;
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t column = 0; column < columns; ++column) {
            bool fits = true;
            const uint32_t flags = env.CellFlags(cursor);
            const uint32_t entry = static_cast<uint32_t>(turned ? building.footprint[column][row] : building.footprint[row][column]);
            if (entry != 0) {
                if (entry == 0x200) {
                    if (!env.EdgeOk(cursor)) fits = result = false;
                } else {
                    if ((flags & 0x50) != 0 || (flags & 0x8000) != 0) fits = result = false;
                    if ((flags & 0x100) != 0) {
                        if ((entry & 4) == 0) fits = result = false;
                    } else if ((entry & flags) == 0) {
                        fits = result = false;
                    }
                }
                if (env.HasBin(cursor)) fits = result = false;
                if (building.preview) env.Highlight(cursor, fits ? building.validColor : building.invalidColor);
            }
            cursor = env.Neighbour(cursor, columnDirection);
        }
        cursor = env.Neighbour(origin, rowDirection);
        origin = cursor;
    }
    return result;
}

template<class Env> bool CanPlaceScenery(Env& env, int gridWidth, int gridHeight, int rotation, TileXY tile)
{
    if (env.CrossesBoundary(tile, gridWidth, gridHeight)) return false;
    const bool turned = (rotation & 1) == 1;
    const uint32_t columns = static_cast<uint32_t>(turned ? gridWidth : gridHeight);
    const uint32_t rows = static_cast<uint32_t>(turned ? gridHeight : gridWidth);
    TileXY origin = tile;
    for (uint32_t row = 0; row < rows; ++row) {
        TileXY cursor = origin;
        for (uint32_t column = 0; column < columns; ++column) {
            if (!env.SceneryTileOk(cursor) || !env.OnlyLitter(cursor)) return false;
            cursor = env.Neighbour(cursor, 0);
        }
        origin = env.Neighbour(origin, 6);
    }
    return true;
}

inline float CashAfterPlacing(float cash, int32_t cost)
{
    return static_cast<float>(static_cast<double>(cash) - static_cast<double>(cost));
}
inline float ExpensesAfterPlacing(float expenses, int32_t cost)
{
    return static_cast<float>(static_cast<double>(static_cast<uint32_t>(cost)) + static_cast<double>(expenses));
}

inline int ScaledCursor(int pixels, float zoomScale)
{
    return FloatToIntWrap(static_cast<double>(pixels) / static_cast<double>(zoomScale));
}

inline bool GrassTileUsable(uint32_t flags)
{
    bool usable = ((flags & 1) != 0 && (flags & 0x80000000u) != 0) || (flags & 0x40) != 0;
    if ((flags & 0x10007000u) != 0) usable = false;
    return usable;
}

inline bool PavementTileUsable(uint32_t flags)
{
    return (flags & 1) != 0 && (flags & 0x80000000u) != 0 && (flags & 0x10000000u) == 0;
}

inline bool CashCoversTile(float cash, int32_t cost)
{
    return static_cast<double>(cash) - static_cast<double>(cost) >= 0.0;
}
inline bool CashCoversBuilding(float cash, int32_t cost)
{
    return static_cast<double>(cash) - static_cast<double>(cost) > 0.0;
}

// The cursor of the two tile modes: normal when the tile can be paid for, was just placed or the cursor is over a panel, otherwise the
// "refused" shape. The panel test runs last, only when the first two did not decide (it is a query, but the live test counts the calls).
constexpr int kCursorNormal = 0, kCursorRefused = 5;
template<class OverGui> int PlaceCursorShape(bool canPlace, bool justPlaced, OverGui&& overGui)
{
    return canPlace || justPlaced || overGui() ? kCursorNormal : kCursorRefused;
}

struct PreviewTiles {
    int first, second;
};
inline PreviewTiles BuildingPreviewTiles(int gridWidth, int gridHeight, int rotation)
{
    return (rotation & 1) == 1 ? PreviewTiles{gridHeight, gridWidth} : PreviewTiles{gridWidth, gridHeight};
}

// GuiModePlaceScenery::DrawWorld outlines every tile of the footprint in `color` before it draws the sprite: the same walk as
// CanPlaceScenery (rows in direction 6, the cells of a row in direction 0, `gridWidth` rows with an even rotation), but without the early
// exit. A non-positive count draws nothing, and an empty row still moves to the next one.
// Env: TileXY Neighbour(TileXY tile, int direction)         Grid::GetNeighbour
//      void Outline(TileXY tile, uint16_t color)             GuiManager::DrawTileOutline
template<class Env> void OutlineScenery(const Env& env, int gridWidth, int gridHeight, int rotation, TileXY origin, uint16_t color)
{
    const bool turned = (rotation & 1) == 1;
    const int rows = turned ? gridHeight : gridWidth;
    const int columns = turned ? gridWidth : gridHeight;
    for (int row = 0; row < rows; ++row) {
        TileXY cursor = origin;
        for (int column = 0; column < columns; ++column) {
            env.Outline(cursor, color);
            cursor = env.Neighbour(cursor, 0);
        }
        origin = env.Neighbour(origin, 6);
    }
}

constexpr int32_t kCreateBuildingClass = 2;
inline messages::Message BuildingCommandMessage()
{
    messages::Message message{};
    message.sender = -2;
    message.receiver = -2;
    message.data = kCreateBuildingClass;
    return message;
}
// `key` is BuildingDef::m_key, strcpy'd (terminator included) to the front of the placement; the footprint's origin tile, the preview's
// direction as the layer, no prices (-1.0 x5), no beer (-1, price 0.5), age 0, not broken.
inline world_script::ScriptPlacement BuildingCommandPlacement(const char* key, TileXY origin, int32_t direction)
{
    world_script::ScriptPlacement placement{};
    const size_t size = std::min(std::strlen(key) + 1, sizeof(placement.m_name) + sizeof(placement.m_sprite));
    const size_t nameSize = std::min(size, sizeof(placement.m_name));
    std::memcpy(placement.m_name, key, nameSize);
    if (size > nameSize) std::memcpy(placement.m_sprite, key + nameSize, size - nameSize);
    placement.m_x = static_cast<int32_t>(origin.x);
    placement.m_y = static_cast<int32_t>(origin.y);
    placement.m_layer = direction;
    for (float& price : placement.m_prices) price = -1.0f;
    placement.m_beer = -1;
    placement.m_beerPrice = 0.5f;
    return placement;
}

constexpr int32_t kCreateSceneryClass = 1;
inline messages::Message SceneryCommandMessage()
{
    messages::Message message{};
    message.sender = -2;
    message.receiver = -2;
    message.data = kCreateSceneryClass;
    return message;
}
inline world_script::ScriptPlacement SceneryCommandPlacement(const char* key, const char* name, TileXY origin, int32_t direction)
{
    world_script::ScriptPlacement placement{};
    const int32_t marker = -2;
    std::memcpy(&placement.m_prices[0], &marker, sizeof(marker));
    placement.m_x = static_cast<int32_t>(origin.x);
    placement.m_y = static_cast<int32_t>(origin.y);
    // Scenery layout of m_layer: byte 0 is 1, byte 1 the direction, the upper bytes stay zero.
    placement.m_layer = 1 | (static_cast<int32_t>(static_cast<uint8_t>(direction)) << 8);
    std::strncpy(placement.m_name, key, sizeof(placement.m_name) - 1);
    std::strncpy(placement.m_sprite, name, sizeof(placement.m_sprite) - 1);
    return placement;
}

} // namespace obl::gui_modes_place

namespace obl::ui {
struct GuiManager;
struct GuiMode;
} // namespace obl::ui

namespace obl::gui {
bool RegisterPlaceModeDrivers();
// A new place mode `mode` (1 scenery, 5 building, 7 paving, 8 grass), as GuiManager::SwitchMode builds it; null for any other mode.
ui::GuiMode* ConstructPlaceMode(int mode, ui::GuiManager* manager);

// The fit checks and the purchase of the building and scenery place modes, shared by the GUI click and the command executor
// (command_executor.cpp). x/y is the footprint origin, direction the preview's. Buy* pays and posts the create command when the cash
// covers the price (a building needs cash left over, scenery may spend all) and returns false otherwise, changing nothing.
bool BuildingFits(const tables::BuildingDef& definition, uint32_t x, uint32_t y, int direction);
bool SceneryFits(const tables::SceneryDef& definition, uint32_t x, uint32_t y, int direction);
bool BuyBuilding(const tables::BuildingDef& definition, uint32_t x, uint32_t y, int direction);
bool BuyScenery(const tables::SceneryDef& definition, uint32_t x, uint32_t y, int direction);

// The surface modes (grass, paths) and the `paint-surface` command. The definition is scenery of category 1; its m_PathFlag picks grass
// (non-zero) or path (zero), as the scenery panel does when it enters the mode. SurfaceFits reads only the tile's flags; PaintSurfaceTile
// pays without a cash check (the caller checks CashCoversTile), writes the surface bits and rebuilds the path tiles.
bool IsGrass(const tables::SceneryDef& definition);
bool SurfaceFits(const tables::SceneryDef& definition, uint32_t x, uint32_t y);
void PaintSurfaceTile(const tables::SceneryDef& definition, uint32_t x, uint32_t y);
} // namespace obl::gui
