# The Mods screen

Open **Mods** from the main menu to turn installed mods on and off, change their order, edit their settings, install new ones
and manage where they come from, without editing `openbeach.ini`.

A mod is a folder `mods/<id>/` in the game directory with a `mod.json` manifest beside its files.
See [installing and finding your mods](installing.md) for the game directory on your platform.

The screen has three tabs in its top bar: **Installed**, **Browse** and **Sources**. The open tab runs into the page below it.
Each tab is a list on the left and a card about the selected entry on the right. **Back** returns to the main menu, the **?**
in the bottom right corner opens this page in your browser (if that fails, the address is shown in the status bar), and the
status bar shows the game version that `engine_min` is checked against.

The screen takes its colours from the main menu buttons of the pack you play: a pack with coloured button labels gets rounded
boxes and pills in that colour, and any other pack gets a plain flat style. Nothing of the pack is copied or stored.

## Installed
- The list has two sections: **On, in load order** (numbered) and **Off**. The switch on each row turns a mod on or off; a mod
  switched on joins the end of the order.
- Select a row to see the mod in the card: its version, kind (**World** or **Presentation**), its `description`, what needs
  attention, its settings and, under **Technical details**, its packs, taken-over defaults (`takes_over`, including `action.*`),
  declared `actions` and key `bindings`, requirements and incompatible mods.
- The selected active mod shows up and down arrows on its row to move it earlier or later in the load order.
- The card's **Switch on** / **Switch off** button does what the row's switch does; **Remove...** deletes the mod.
- A mark on a row means the mod needs attention: red when it cannot start at all, amber when it overlaps with another mod or
  cannot start beside the current list. The card explains why and, when one click fixes it, offers that button (for example
  **Switch off Calm Music** or **Switch on Tourist Season**).

## Installing and removing mods
Choose **+ Install from file** under the Installed list and select a ZIP containing one mod folder, with its `mod.json`
directly inside that folder. On desktop and in the browser, you can also drop the ZIP onto the game while **Mods** is open.
Android opens the system file picker. The folder name must match the manifest's id. The installed mod is switched on at
once, at the end of the load order; a world mod starts when the game starts again. If it cannot start beside the current
list, it stays off and the status bar says why.

If the id is already installed, the game asks before replacing its files. An active mod stays on after the update, and a mod
that was off stays off. A world mod that is running in the current game cannot be replaced or removed: switch it off and
restart the game first.

Choose **Remove...** in the card, then confirm to delete the mod's files and generated assets; an active mod is switched off
first. An active mod that needed the removed one is switched off too. Your mod settings and private data are kept. In the browser, changes are saved to that site's browser storage.

The archive must be a standard ZIP with stored or deflated entries and portable ASCII filenames. Links, encrypted files
and split archives are refused. Limits are 64 MiB for the ZIP, 32 MiB for one file, 128 MiB expanded and 4,096 entries.
The game checks the manifest, target pack, game version (`engine_min`), script API and scope before writing. If the mod includes
[asset recipes](../reference/recipes.md), the game builds their outputs from the current pack during installation.
An unavailable source disables that recipe and leaves a message in the log.

## Installing from online sources
**Browse** lists the compatible mods of every enabled source. A row shows **Installed** when that version is installed and
**Update** when the source has a newer one. The card shows the mod's version, kind, license, source (third-party sources are
marked), description and the mods it needs.

- Choose **Install and switch on**, or **Update to <version>** when an older version is installed. Any required mods from
  your enabled online sources are installed first in dependency order, and the new mods are switched on together once the
  last one is written. An installed mod of the same or a newer version shows
  **Installed, up to date** instead.
- Every download is checked against the catalog's declared byte size and SHA-256 hash, then validated and installed through
  the same ZIP installer and replace/remove rules.
- If a save names a missing world mod, the game checks your enabled online sources by mod id and content hash and notes where
  it is available.

**Sources** lists where mods come from:

- **OpenBeach Official Mods** (`https://openbeach.app/mods/catalog.json`) is listed first by default and cannot be removed.
- **Community Mods** (`https://openbeach-larry.github.io/mods/catalog.json`), a third-party source, follows it, switched
  on. The game knows its signing key in advance. You can switch it off or remove it; a removed one does not come back.
- Choose **+ Add**, enter or paste an `https://` `catalog.json` address, and confirm the third-party notice to add a source.
- Each source has a switch to stop listing its mods without forgetting it; **Remove** drops a third-party source.
- The game verifies each catalog's Ed25519 signature and remembers its signing key. If a source's signing key ever changes on
  refresh, the game warns you and asks before trusting the new key.
- **Refresh** checks the enabled sources for updates.

See [mod catalogs](../catalogs.md) for the catalog format and hosting requirements.

## Presentation and world mods
- **Presentation** mods change how the game looks or reacts (scripts, GUI patches, added texts, settings). Switching one on, off
  or moving it takes effect at once: its scripts restart and the menus reload.
- **World** mods change game rules or content (buildings, guests, maps, scenarios). The screen saves the new list to `[Mods] Active`
  in `openbeach.ini`; the row and the card note that the change applies when the game starts again.

## Settings
Select an active presentation mod that declares settings to change them in the card's **Settings** with **<** and **>**. Each
change writes to `[Mod.<id>]` in `openbeach.ini` and notifies the mod's scripts at once. Switch the mod on first if its settings
are inactive.

## Conflicts
There are two kinds of conflict:
- **Incompatible mods (`conflicts` in `mod.json`):** when one mod declares that it does not work with another, the screen refuses
  to switch both on at the same time and names the conflicting mod. The start-up check also refuses such a list from
  `openbeach.ini` or `OBL_MODS`. Switch off one of the two to use the other.
- **Overlapping changes:** when active mods change the same value (for example the same field in a JSON data patch), take over
  the same default behavior (`takes_over`, such as `input.touch` or `action.game.speed_up`), or share a hotkey with another mod
  or the engine, the **On** section shows how many overlaps there are (choose it to select an affected mod), and the card of
  each affected mod says what overlaps and who wins. The later mod in the load order wins (and `[Keys]` in `openbeach.ini` beats
  every mod); move a mod with the arrows to change the winner.

## Why a mod cannot start
A mod that cannot start on the current pack or list is marked with the reason:
- **Broken:** `mod.json` is missing or invalid.
- **Made for another pack:** the mod targets a different pack than the one currently loaded.
- **Needs a newer game:** the mod requires a newer presentation script API, or names an `engine_min` newer than the game
  (the status bar shows the running version).
- **Needs `<mod>`, which is off / earlier in the order:** switch on the required mod and place it above the mod that needs it.
- **Does not work with `<mod>`:** one of the two mods names the other in `conflicts`.

If a mod stops the game at start-up before you reach the main menu, remove its id from `[Mods] Active` in `openbeach.ini`
([switching-mods.md](switching-mods.md#a-mod-that-stops-the-start-up)).

See [mod errors and conflicts](mod-errors.md) for startup messages.
