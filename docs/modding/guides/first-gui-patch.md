# Add a menu button

Start with [l0_gui_patch](../../../tests/mods/l0_gui_patch/mod.json). Its
[main_menu.patch.json](../../../tests/mods/l0_gui_patch/gui/main_menu.patch.json) targets the `main_menu` layout on L0.

1. Use `add_slot` to put a named row on `MainMenu` and give it bounds in the reference layout.
2. Use `add_control` with the slot name, a unique control id and a `Button(...)` definition. This example uses an L0 button
   sprite and names the action `Mod:l0_gui_patch.ping`.
3. Open the main menu with the mod active and click the new button. To run Lua in response, add an API 1 presentation
   script with `on_action(id)` and handle the id `ping`.

See [GUI patch operations](../reference/gui-patches.md) for layout anchors, existing control ids and dialog patches.
