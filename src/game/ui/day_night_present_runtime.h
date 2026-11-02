#pragma once

namespace obl::ui {
struct Renderer;
}

namespace obl::ui {

// Presents the scene through the weather tone tables into the back buffer: gathers the
// buffers from the renderer, the weather and the settings and runs the pixel loops of src/game/ui/day_night_present.
void PresentDayNight(ui::Renderer* renderer);
} // namespace obl::ui
