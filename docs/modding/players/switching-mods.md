# Switching mods on

A mod is a folder `mods/<id>/` in the game directory. The easiest way to switch mods on and off, reorder them and edit their
settings is the **Mods** screen in the main menu ([mods-screen.md](mods-screen.md)). The game keeps that list in the `Active` key
of the `[Mods]` section of `openbeach.ini`, in load order; the default is no mod.

## Turn a mod on
Add its id to the list, separated by commas, and start the game:
```ini
[Mods]
Active=first_mod,second_mod
```
- A later mod in the list wins where two mods change the same file or the same text key.
- The ids are the folder names. Each mod must sit in its own `mods/<id>/` folder with its `mod.json` next to the files.
- The list is read at start-up only. Changing it while the game runs does nothing until the next start.

## Turn a mod off
Remove its id from `Active`, or leave the list empty (`Active=`), and start the game again. Nothing else needs to change:
the mod's folder can stay where it is.

## A mod that stops the start-up
The game stops at start-up and names the mod and the reason when a listed mod:
- has no folder in `mods/`, or its `mod.json` is missing or invalid;
- has a folder name that differs from its `id`;
- was made for another pack;
- is listed twice;
- needs another mod that is not in the list, or that comes after it;
- does not work with another active mod (`conflicts` in `mod.json`);
- needs a newer version of the game (its script API).

To get the game running again, remove that mod's id from `Active` (or fix the order or the missing mod it names), then start
the game again.

On desktop, edit the `openbeach.ini` in the [game directory](installing.md). In a Chromium browser, you can edit the site's
OPFS file from the developer console while the game is closed. This clears the active list and preserves the other settings:

```js
const root = await navigator.storage.getDirectory();
const file = await root.getFileHandle("openbeach.ini");
const ini = await (await file.getFile()).text();
const writer = await file.createWritable();
await writer.write(ini.replace(/^(\[Mods\][^\S\r\n]*\r?\n)((?:[^\[\r\n][^\r\n]*\r?\n|\r?\n)*?)Active[^\S\r\n]*=[^\r\n]*/mi, "$1$2Active="));
await writer.close();
```

On Android, edit the same file if your device gives you access to the game directory. For an install in private app storage,
Android's **Clear storage** resets the active list along with installed mods, imported packs, settings and local saves. Use
that reset only if you can restore the data you need.

## A mod whose script fails
A mod that changes only how the game looks or reacts (a presentation mod, with scripts) does not stop the game when its script
fails: the game switches that mod off for the rest of the run, writes the mod, the file and the line into `openbeach.log`, and
goes on with the other mods. Report the line to the mod's author, or remove the mod from `Active`.

## Saves made with mods
A save made with mods remembers its mod list; presentation mods are not part of it, so they never get in the way of a save. When you load it with a different list, the game asks first and names the
mod. A replay made with other mods is refused.

Tests and tools can set the list for one run with the `OBL_MODS` environment variable; it overrides `Active` in the same
format. Players do not need it.

The format of `mod.json` and the rules of each layer: [reference/mods.md](../reference/mods.md). The settings file in general:
[build/settings.md](../../build/settings.md).
