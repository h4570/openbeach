#pragma once
// The converted GUI layouts (packs/base/gui/, gui_layouts.h) as the GUI loader reads them. Loaded once at start-up; a GUI that needs
// a layout the pack lacks stops the game with the reason, as the object browser and credits do (gui_content_runtime.h).

#include "core/char16.h"
#include <string>

namespace obl::gui::gui_layouts {

bool Load(const Text16& gameDirectory);

// The file of a layout key (game_states.h kMainMenuLayout, ...) for a language number. nullptr for a null key (a state without a
// converted layout). The name stays valid until the game ends.
const char* FileFor(const char* layout, int language);

std::string ReadScript(const char* file);

} // namespace obl::gui::gui_layouts
