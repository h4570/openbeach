# GUI patches

A mod can modify `.gui` layouts without copying whole layout files by placing `gui/<layout>.patch.json` in its directory, where `<layout>` is a layout key from `gui/layouts.json` (`main_menu`, `in_game`, `intro`, `game_won`, `reset_menu`).

When no active mod provides a patch for a layout, the engine parses the base `.gui` file directly and leaves layouts, goldens, world hashes and replays unchanged.

## Control IDs

Every control in a patched layout has a string ID so patches and scripts can address it:

- **Explicit ID in `.gui`:** append `id=<identifier>` after the control's closing parenthesis, for example:
  ```
  Button(310, 314, 490, 354, l0_menu_start, None, ShowScenSel, None, L0_MENU_TIP_START) id=start_button
  ```
- **Explicit ID in a patch:** set `"id": "<identifier>"` on an `add_control` operation.
- **Derived ID:** controls without an explicit ID receive a deterministic ID of the form `<Scope>.<Kind>.<Key>` (for example `MainMenu.Button.ShowScenSel`, `MainMenu.TGA.l0_welcome.tga`, `MainItem.Variable.PlayerMoney$`).
  - `Button`: left action (if not `None`), else right action, else text key, else graphic.
  - `CheckBox`: variable name.
  - `Text`, `Variable`, `VariableImage`, `Slider`, `ComboBox`, `Image`, `TGA`, `ListBox`, `RadioGroup`, `Table`: key/name argument.
  - `Icon`: `<name>.<frame>`.
  - `Bar`, `ImageList`, `NumberAdjust`: key/list argument.
  - `RadioButton`: integer value.
- If two controls in a layout resolve to the same ID, loading is refused with an error naming the colliding ID.

## Slots

Slots are named rectangular regions that lay out added controls automatically in mod load order so mods do not fight over coordinates.

- **In `.gui`:** `Slot(name, x0, y0, x1, y1, direction[, spacing])` inside a `DialogItem`, `MainItem`, or `MainItem640`. `direction` is `column` (default) or `row`; `spacing` is the gap in pixels between items (default `0`).
- **In a patch:** use the `add_slot` operation to declare a slot on a base-pack dialog whose `.gui` file cannot be edited directly.
- Each control added into a slot keeps its own width and height (falling back to the slot's dimension when 0) and is placed at the next position along `direction` in load order.

## Anchors

Anchors adapt dialogs and controls from the 800x600 reference space to wider screens (such as 16:9) without shipping a second layout file. Dialog anchors resolve against the screen size; child control anchors resolve against their parent dialog's resolved size.

Supported anchor names:

| Anchor | Horizontal | Vertical |
|---|---|---|
| `left`, `top`, `top_left` | Keep left coordinate | Keep top coordinate |
| `right`, `top_right` | Keep right margin | Keep top coordinate |
| `bottom`, `bottom_left` | Keep left coordinate | Keep bottom margin |
| `bottom_right` | Keep right margin | Keep bottom margin |
| `center`, `centre` | Center horizontally | Center vertically |
| `center_x`, `centre_x` | Center horizontally | Keep top coordinate |
| `center_y`, `centre_y` | Keep left coordinate | Center vertically |
| `stretch` | Stretch preserving margins (or full width) | Stretch preserving margins (or full height) |
| `stretch_x` | Stretch horizontally | Keep top coordinate |
| `stretch_y` | Keep left coordinate | Stretch vertically |

## Patch file format

```json
{
  "format": "obl.gui_patch",
  "version": 1,
  "ops": [
    { "op": "set_anchor", "dialog": "MainMenu", "anchor": "stretch" },
    { "op": "add_slot", "dialog": "MainMenu", "slot": "mod_buttons",
      "bounds": [20, 534, 200, 574], "direction": "row", "spacing": 8 },
    { "op": "add_control", "dialog": "MainMenu", "slot": "mod_buttons", "id": "my_btn",
      "control": "Button(0, 0, 180, 40, l0_menu_start, None, Mod:my_mod.open, None, MY_TIP)" },
    { "op": "hide_control", "id": "MainMenu.Button.ExtrasMenu" },
    { "op": "move_control", "id": "MainMenu.Button.ShowScenSel", "x": 310, "y": 280 },
    { "op": "add_dialog",
      "dialog": "DialogItem MyDialog(0, 0, 400, 300, l0_play_panel, 99, 1) { Button(150, 240, 250, 264, l0_play_button, CLOSE, ExitDialog, None) }",
      "anchor": "center" }
  ]
}
```

### Operations

Patches from active mods apply in load order after the base layout:

- **`add_dialog`**: parses `dialog` (a `DialogItem ... { ... }` block) and appends it to the layout. Optional `anchor`. Fails if a dialog with that name already exists.
- **`add_slot`**: declares a named `slot` on `dialog` with `bounds: [x0, y0, x1, y1]`, optional `direction` (`"column"` or `"row"`, default `"column"`), and optional `spacing` (default `0`).
- **`add_control`**: adds `control` (one `.gui` control statement) to `dialog`. Optional `slot` (lays out in slot order) or `at` (`"front"` default, or `"back"` to draw behind existing controls); optional `id` and `anchor`.
- **`hide_control`**: hides the control with `id`.
- **`move_control`**: moves the control with `id` using `x`/`y` (keeps size), `dx`/`dy` (relative offset), or `bounds: [x0, y0, x1, y1]`. If two patches move the same control, the later mod wins.
- **`set_anchor`**: sets `anchor` on either `dialog` or control `id`.

## Mod button actions and runtime control

- A `Button` may use `Mod:<mod_id>.<event>` as its left or right action in place of a built-in engine action. The patch loader verifies that `<mod_id>` is active; clicking the button dispatches the event through `obl::gui_patch_runtime` (where presentation scripts register their handler).
- `obl::gui_patch_runtime` (`src/game/ui/gui_patch_runtime.h`) also exposes `FindControl`, `SetControlVisible`, `MoveControl`, `ShowDialog`, `HideDialog`, and `MoveDialog` by string ID for script bindings.
- Any error stops layout loading with a message naming the mod, the patch file, the operation index/name, and the reason (unknown dialog, slot, or ID; duplicate dialog or ID; invalid anchor; or an action referencing an inactive mod).

## Tests

- Unit tests: `gui_patch_test` (`tests/unit/gui_patch_test.cpp`) covers every operation, slot ordering across two mods, 4:3 and 16:9 anchors, every error condition, deterministic ID derivation and collision refusal on all L0 layouts, and byte-identical pass-through when no patches are active.
- Game test: `tests/l0_mod_gui_patch.txt` loads test mods `tests/mods/l0_gui_patch` and `tests/mods/l0_gui_patch_second`, opens the patched main menu, clicks a mod-added button that fires `Mod:l0_gui_patch.ping`, and verifies the dispatch.
