#pragma once

namespace obl::ui {
struct RendererLoadingGradient;
} // namespace obl::ui

namespace obl::game {

// The fixed tables the start-up fills in: game speeds, position offsets, the loading-bar gradient and the combo-box
// sprite names. `InitStartupTables` writes them once, before the first world.
void InitStartupTables();

// The game speed multipliers by level.
float* GameSpeedTable();

// The position offset pair: `variant` 1 gives (15, 0), any other (-15, 0).
const float* PositionOffset(int variant);

ui::RendererLoadingGradient& LoadingGradient();

// The three combo-box sprite names of the list and main-item widgets.
const char* const* GuiListSpriteNames();
const char* const* GuiMainItemSpriteNames();

} // namespace obl::game
