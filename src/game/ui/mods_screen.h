#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace obl::ui {
struct GuiWidget;
}

// The mods screen of the main menu (docs/modding/players/mods-screen.md): the installed mods, switched on and off and put in order,
// why one cannot start, each mod's settings and the conflicts of the list. The list it edits is mods::ModList; a change that can
// start is kept at once: presentation mods start or stop at once (the layers, scripts, texts and menus reload), world mods on the
// next start, and `[Mods] Active` in openbeach.ini gets the new list (scripted test runs leave the player's file alone).
namespace obl::gui::mods_screen {

// Widget types of the own controls (GuiWidget::m_type), apart from every type of the pack's controls.
inline constexpr int32_t kOwnButtonType = 40;
inline constexpr int32_t kModsPanelType = 41;

// Start-up wiring: the game directory, whose mods/ the screen lists and whose openbeach.ini it writes.
void SetGameDirectory(const std::filesystem::path& gameDirectory);

// The ModsPanel control of the own layer.
ui::GuiWidget* CreatePanel(int x0, int y0, int x1, int y1);

// A desktop drop is accepted only while the Mods screen is open.
bool QueueArchive(std::string_view path);

// Test support (`gui-click ModsScreen target <name>`): the centre of a tap target of the open screen, false when it shows none of
// that name. Names: `toggle.<mod>`, `select.<mod>`, `up.<mod>`, `down.<mod>`, `setting.<id>.prev`, `setting.<id>.next` (of the
// selected mod), `page.prev`, `page.next`, `help`, `back`.
bool FindTarget(std::string_view name, int& x, int& y);

} // namespace obl::gui::mods_screen
