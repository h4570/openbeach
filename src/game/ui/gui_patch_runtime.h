#pragma once

#include "game/ui/gui_patch.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace obl::ui {
struct GuiManager;
struct GuiWidget;
} // namespace obl::ui

// Runtime bridge for GUI layout patches:
// - Extension point for mod button actions (`Mod:<id>.<event>`), so presentation scripts (#114/#115) can register a handler.
// - Runtime lookup and manipulation of dialogs and controls by string ID (show, hide, move).
// Kept separate from the base GUI path: when no patches are active, all tables stay empty.
namespace obl::gui_patch_runtime {

using ModActionHandler = std::function<void(std::string_view modId, std::string_view event)>;

// Registers or clears the callback invoked when a button with a `Mod:<id>.<event>` action is clicked.
void SetModActionHandler(ModActionHandler handler);

// Dispatches a `Mod:<id>.<event>` button action: logs it, records it for test probes, and forwards it to the registered handler.
void DispatchModAction(std::string_view action);

// Last dispatched `Mod:<id>.<event>` action and total dispatch count (for tests / diagnostics).
const std::string& LastModAction();
uint32_t ModActionCount();
void ResetModActionStats();

// Binds the emitted control IDs from the most recent patched LoadScript to the live GuiManager widget tree.
void BindLayout(ui::GuiManager* manager, std::span<const gui_patch::ControlBinding> bindings);
void ClearBindings();

// Script / test API: find, show, hide, and move dialogs and controls by ID.
ui::GuiWidget* FindControl(std::string_view id);
bool SetControlVisible(ui::GuiManager* manager, std::string_view id, bool visible);
bool MoveControl(ui::GuiManager* manager, std::string_view id, int x, int y);
bool ShowDialog(ui::GuiManager* manager, std::string_view name, bool modal = false);
bool HideDialog(ui::GuiManager* manager, std::string_view name);
bool MoveDialog(ui::GuiManager* manager, std::string_view name, int x, int y);

} // namespace obl::gui_patch_runtime
