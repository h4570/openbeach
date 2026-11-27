# The Mods screen

Open **Mods** from the main menu to turn installed mods on and off, change their order, edit their settings
and check for conflicts without editing `openbeach.ini`.

A mod is a folder `mods/<id>/` in the game directory with a `mod.json` manifest beside its files.
See [installing and finding your mods](installing.md) for the game directory on your platform.

The screen takes its colours from the main menu buttons of the pack you play: a pack with coloured button labels gets rounded
boxes and pills in that colour, and any other pack gets a plain flat style. Nothing of the pack is copied or stored.

## Installing and removing mods
Choose **Install from file** and select a ZIP containing one mod folder, with its `mod.json` directly inside that folder.
On desktop and in the browser, you can also drop the ZIP onto the game while **Mods** is open. Android opens the system
file picker. The folder name must match the manifest's id. The installed mod appears in the list, ready to switch on.

If the id is already installed, the game asks before replacing its files. Switch off an active mod before replacing
or removing it. If a world mod is still running after you switch it off, restart the game first.

Click **X** on a mod's row, then confirm to delete its files and generated assets. Your mod settings and private
data are kept. In the browser, changes are saved to that site's browser storage.

The **?** button in the bottom right corner opens this help page in your browser (if opening fails, the address is shown
in the status bar).

The archive must be a standard ZIP with stored or deflated entries and portable ASCII filenames. Links, encrypted files
and split archives are refused. Limits are 64 MiB for the ZIP, 32 MiB for one file, 128 MiB expanded and 4,096 entries.
The game checks the manifest, target pack, game version (`engine_min`), script API and scope before writing. If the mod includes
[asset recipes](../reference/recipes.md), the game builds their outputs from the current pack during installation.
An unavailable source disables that recipe and leaves a message in the log.

## Installing from online sources
Choose **Online sources** in the **Mods** screen header to browse and install or update mods from online sources:

- **OpenBeach Official Mods** (`https://openbeach.app/mods/catalog.json`) is listed first by default and cannot be removed
  (you can switch it off with **On** / **Off**).
- Choose **Add source**, enter or paste an `https://` `catalog.json` address, and confirm the third-party notice to add a
  third-party online source.
- The game verifies each catalog's Ed25519 signature and remembers its signing key. If a source's signing key ever changes on
  refresh, the game warns you and asks before trusting the new key.
- Select a compatible mod from the list and choose **Install** (or **Update** if an older version is installed). Any
  required mods from your enabled online sources are installed first in dependency order.
- Every download is checked against the catalog's declared byte size and SHA-256 hash, then validated and installed through
  the same ZIP installer and replace/remove rules (switch off an active mod before updating it).
- Choose **Refresh** to check enabled online sources for updates, or **Remove** beside a third-party source to drop it from
  your list.
- If a save names a missing world mod, the game checks your enabled online sources by mod id and content hash and notes where
  it is available.

See [mod catalogs](../catalogs.md) for the catalog format and hosting requirements.

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
- **Needs a newer game:** the mod requires a newer presentation script API, or names an `engine_min` newer than the game
  (the screen's header shows the running version).
- **Needs `<mod>`, which is off / earlier in the order:** switch on the required mod and place it above the mod that needs it.
- **Does not work with `<mod>`:** one of the two mods names the other in `conflicts`.

If a mod stops the game at start-up before you reach the main menu, remove its id from `[Mods] Active` in `openbeach.ini`
([switching-mods.md](switching-mods.md#a-mod-that-stops-the-start-up)).

See [mod errors and conflicts](mod-errors.md) for startup messages.
