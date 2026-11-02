#pragma once

namespace obl::platform {

// Builds the extended Options > Video resolution list from the primary monitor's size (game/ui/resolutions.h).
bool InitializeResolutions();

// Applies [Display] Scale to the game window `window` (the game's opaque handle, sdl_present.h; the renderer size stays
// unchanged). Borderless when it covers the monitor, otherwise a resizable framed window fitting the work area.
void PlaceGameWindow(void* window, int width, int height);

} // namespace obl::platform
