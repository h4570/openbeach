# The Mods screen

Open **Mods** from the main menu to turn installed mods on and off, change their order, edit their settings
and check for conflicts without editing `openbeach.ini`.

A mod is a folder `mods/<id>/` in the game directory with a `mod.json` manifest beside its files.

## Turning mods on and off
- Active mods appear at the top of the list, in load order; inactive mods follow by id.
- Click or tap **On** / **Off** on a mod to switch it. A mod switched on joins the end of the active list.
- Select a mod to read its version, target packs, kind (**World** or **Presentation**), taken-over defaults (`takes_over`,
  including `action.*`), declared `actions` and key `bindings`, requirements and incompatible mods.
- Use **^** and **v** beside an active mod to move it earlier or later in the load order.

## Presentation and world mods
- **Presentation** mods change how the game looks or reacts (scripts, GUI patches, added texts, settings). Switching one on, off
  or moving it takes effect at once: its scripts restart and the menus reload.
- **World** mods change game rules or content (buildings, guests, maps, scenarios). The screen saves the new list to `[Mods] Active`
  in `openbeach.ini` and shows a note that world mod changes apply when the game starts again.

## Settings
Select an active presentation mod that declares settings to change them in the **Settings** panel with **<** and **>**. Each
change writes to `[Mod.<id>]` in `openbeach.ini` and notifies the mod's scripts at once. Switch the mod on first if its settings
are inactive.

## Conflicts
There are two kinds of conflict:
- **Incompatible mods (`conflicts` in `mod.json`):** when one mod declares that it does not work with another, the screen refuses
  to switch both on at the same time and names the conflicting mod. The start-up check also refuses such a list from
  `openbeach.ini` or `OBL_MODS`. Switch off one of the two to use the other.
- **Overlapping changes:** when active mods change the same value (for example the same field in a JSON data patch), take over
  the same default behavior (`takes_over`, such as `input.touch` or `action.game.speed_up`), or share a hotkey with another mod
  or the engine, the **Conflicts** panel lists what overlaps and who wins. The later mod in the load order wins (and `[Keys]` in
  `openbeach.ini` beats every mod); move a mod with **^** / **v** to change the winner.

## Why a mod cannot start
A mod that cannot start on the current pack or list is marked in red with the reason:
- **Broken:** `mod.json` is missing or invalid.
- **Made for another pack:** the mod targets a different pack than the one currently loaded.
- **Needs a newer game:** the mod requires a newer presentation script API.
- **Needs `<mod>`, which is off / earlier in the order:** switch on the required mod and place it above the mod that needs it.
- **Does not work with `<mod>`:** one of the two mods names the other in `conflicts`.

If a mod stops the game at start-up before you reach the main menu, remove its id from `[Mods] Active` in `openbeach.ini`
([switching-mods.md](switching-mods.md#a-mod-that-stops-the-start-up)).
