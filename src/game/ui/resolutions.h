#pragma once

#include "core/char16.h"
#include <span>

namespace obl::ui {

// Extends Options > Video > resolution with widescreen/native sizes (windowed mode). The platform measures the monitor
// (platform/window_placement.h) and places the window; this list only knows sizes.
void BuildResolutionTable(int monitorWidth, int monitorHeight);

// The label of an extended resolution when `key` is one of the list's own key pointers (compared by address, as the GUI
// passes them back), else null. ui::text_table::Lookup asks this before the language table.
const Char16* ResolutionText(const char* key);

struct ResolutionListEntry {
    int modeIndex;
    const char* textKey;
    int height;
};
// The table behind the resolution list.
std::span<const ResolutionListEntry> ResolutionListEntries();
// The width of the extended entry selected by `height`; false when the list has none (the game modes handle it).
bool FindExtendedResolution(int height, int* width);
// ui::renderer::IsModeSupported accepts every width of the extended list.
bool IsListedResolutionWidth(int width);

// GuiManager::SyncBarSurfaces runs this first (see the .cpp for the height-only resolution change it repairs).
void PrepareBarSurfaceSync(void* gui);

} // namespace obl::ui
