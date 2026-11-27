#pragma once

#include <cstdint>
#include <string_view>

namespace obl::ui {
struct GuiWidget;
}

// The ExtraScenarios screen of the scenario menu (docs/modding/players/scenarios.md): lists mod scenario sets in load order for
// the current mode (campaign or sandbox), shows each scenario's picture, status, lock reason and descriptions, and starts the
// selected entry.
namespace obl::gui::extra_scenarios_screen {

// Widget type of the ExtraScenariosPanel control (GuiWidget::m_type), apart from pack controls and ModsScreen.
inline constexpr int32_t kExtraScenariosPanelType = 42;

// True when the loaded scenario registry has any extra (mod scenario set) entries in either mode.
bool HasExtraScenarios();

// True when the loaded scenario registry has at least one extra entry for the current mode (campaign or sandbox).
bool HasEntriesForCurrentMode();

// The ExtraScenariosPanel control of the own layer.
ui::GuiWidget* CreatePanel(int x0, int y0, int x1, int y1);

// Keyboard navigation while ExtraScenarios is open (Up/Down/Enter/Escape).
bool IsOpen();
bool HandleKeyDown(int scancode);

// Test support (`gui-click ExtraScenarios target <name>`): the centre of a tap target of the open screen, false when it shows none
// of that name. Names: `back`, `play`, `page.prev`, `page.next`, `select.<scenario_id>`.
bool FindTarget(std::string_view name, int& x, int& y);

} // namespace obl::gui::extra_scenarios_screen
