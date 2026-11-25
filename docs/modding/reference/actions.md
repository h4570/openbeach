# Actions and key bindings

The game's hotkeys are named **actions**. Each action has default keys; a mod can declare actions of its own, move any action to
other keys, or take an engine action over; the player's `openbeach.ini` has the last word. Keys are named like in script input
events (`F5`, `LeftShift`, `Escape`, `Digit4`; the names are those of `src/core/key_code.h`, compared ignoring case). Bindings never
reach the simulation: they are presentation data, kept out of saves and replays.

## Engine actions
The default table reproduces the keys the game always had. A held action acts while its key is down, the others once per press.

| Action | Default keys | What it does |
|---|---|---|
| `camera.left` | `Left` | Scroll the map left (held) |
| `camera.right` | `Right` | Scroll the map right (held) |
| `camera.up` | `Up` | Scroll the map up (held) |
| `camera.down` | `Down` | Scroll the map down (held) |
| `game.pause` | `P` | Pause or resume the game |
| `panel.load_game` | `F5` | Open or close the load game panel |
| `panel.save_game` | `F4` | Open or close the save game panel |
| `panel.options` | `F2` | Open or close the options panel |
| `panel.music` | `M` | Open or close the music panel |
| `panel.build` | `B` | Open or close the build panel |
| `panel.scenery` | `S` | Open or close the scenery panel |
| `camera.zoom_in` | `PageUp` | Zoom the map in one level |
| `camera.zoom_out` | `PageDown` | Zoom the map out one level |
| `game.speed_up` | `Equals` | Raise the game speed one level |
| `game.speed_down` | `Minus` | Lower the game speed one level |
| `tool.cancel` | `Escape` | Leave the building, scenery, paint or tool mode |
| `place.rotate` | `Space` | Turn the building or scenery being placed |
| `place.keep` | `LeftShift`, `RightShift` | Hold to go on placing after a click (held) |

Keys that are not actions stay fixed: the debug chords with Shift and Alt, Ctrl in lists, Alt+Enter, the keys of text fields and
dialogs.

## In mod.json
```json
{
  "actions": [{ "id": "zoom_fit", "label": "Fit the park", "keys": "F9" }],
  "bindings": { "game.pause": ["F10"] },
  "takes_over": ["action.game.speed_up"]
}
```
- `actions`: actions the mod declares. The name is `<mod id>.<id>` (the id: lowercase letters, digits, `_`). `keys` is one key name,
  a comma separated list or a list of names, at most four; none or `"none"` leaves it unbound until the player binds it. A press
  calls the script's `on_action(id)` ([scripting.md](scripting.md)); the same `on_action` also hears GUI button actions, so
  keep the ids apart from those events. The key still reaches `on_input` afterwards. A mod that declares actions ships scripts.
- `bindings`: moves any action (an engine action, the mod's own or another mod's) to the given keys; they replace its keys. A name
  no action has is skipped and logged. A mod that only rebinds needs no scripts, and may be of either scope.
- `takes_over` with `action.<engine action>`: while the mod runs, the engine ignores that action's keys and the mod does the work
  with its own input handling. Same rules as the areas in [scripting.md](scripting.md#taking-over-a-default): when the mod fails
  or stops the action is the engine's again, and the later of two mods wins.

## In openbeach.ini
```ini
[Keys]
game.pause=F10
place.keep=LeftShift, RightShift
panel.music=none
```
The key is the action's full name (a mod's is `<mod id>.<id>`). The value is a list of key names, or `none` to unbind; an empty
value or a missing line means the default (or the mod's binding). An unknown key name is logged and the line ignored. Scripted
test runs read no file: `bind-key <action> <keys|default>` stands in for the line.

## Conflicts
One key acts for one action. When several actions end up with the same key, the strongest source keeps it: the player's `[Keys]`
line, then the later mod in load order (the mod's declared keys and its bindings count alike), then the earlier mods, then the
engine's defaults; between two actions of one source, the one listed later. The losers lose that key. Every such key is written to
the start-up log (`key bindings: key "F9" is bound to "a.x", "b.y"; "b.y" wins (mod "b")`). The same lines are what the mods
screen lists as key conflicts (`mod_pack::KeyConflicts`).

## Tests
`key_actions_test` (unit): the default table, the ini, a mod's action and binding, conflicts and their winner, the takeover, and
that this page's table agrees with the code; `mod_pack_test`: the manifest fields. Game tests on L0: `tests/l0_mod_actions.txt`
(`l0_actions`: the action fires on F9, `game.pause` moves to F10), `tests/l0_mod_action_takeover.txt` (`l0_takeover`) and
`tests/l0_key_binding.txt` (`bind-key`).
