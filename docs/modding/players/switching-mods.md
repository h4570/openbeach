# Switching mods on

A mod is a folder `mods/<id>/` in the game directory. The game uses only the mods listed in the `Active` key of the
`[Mods]` section of `openbeach.ini`, in the order they are listed. The default is no mod.

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
- needs another mod that is not in the list, or that comes after it.

To get the game running again, remove that mod's id from `Active` (or fix the order or the missing mod it names), then start
the game again.

## Saves made with mods
A save made with mods remembers its mod list. When you load it with a different list, the game asks first and names the
mod. A replay made with other mods is refused.

Tests and tools can set the list for one run with the `OBL_MODS` environment variable; it overrides `Active` in the same
format. Players do not need it.

The format of `mod.json` and the rules of each layer: [reference/mods.md](../reference/mods.md). The settings file in general:
[build/settings.md](../../build/settings.md).
