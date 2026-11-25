#pragma once

#include "game/mods/script_runtime.h"

#include <optional>
#include <span>
#include <string>
#include <utility>

namespace obl::ui {
struct Renderer;
}

// The overlay of the presentation scripts (obl.overlay): drawn onto the finished frame after the GUI, right before it is presented,
// so nothing in the game reads it. Fonts come from the pack, images from the mod's overlay/ directory; both are loaded on first use.
namespace obl::mods {

// Draws `ops` in order onto the renderer's back buffer.
void DrawOverlayOps(ui::Renderer& renderer, std::span<const OverlayOp> ops);

// The width and height of `text` (UTF-8) in the pack font `font`; nullopt when the pack has no such font.
std::optional<std::pair<int, int>> MeasureOverlayText(const std::string& text, const std::string& font);

// Frees the fonts and images (the scripts stopped).
void ReleaseOverlayResources();

} // namespace obl::mods
