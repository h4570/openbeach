#pragma once

#include <string>
#include <vector>

namespace obl::test {
// gui-open / gui-close / gui-move / gui-state: dialogs and widget positions through the GuiManager methods and the
// ui:: object graph (game/ui/gui_types.h) (docs/architecture/data-formats/converter/gui-runtime.md). `gui-click` is expanded by autotest.cpp into gui-move + the usual
// press/release of `click`.
bool ExecuteGuiControlCommand(const std::string& name, const std::vector<std::string>& args);
// Live probes gui-entries, gui-open-dialogs, gui-animating (dialogs still sliding in: wait for 0 before a gui-click), gui-mode, gui-mode-requested, gui-selection, gui-context-<0..12>, gui-hash.
bool ReadGuiProbe(const std::string& name, double& out);
} // namespace obl::test
