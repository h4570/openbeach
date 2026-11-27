# Official mods

Official mods live in `data/mods/` and are published as a signed [mod catalog](catalogs.md) alongside the project's site:

```text
https://openbeach.app/mods/catalog.json
```

In the game, open **Mods → Browse** from the main menu: **OpenBeach Official Mods** is listed first under **Sources** by
default (it can be switched off, but not removed) and **Browse** fetches the official catalog automatically. See
[Installing from online sources](players/mods-screen.md#installing-from-online-sources) for how updates and key pinning
work. Because the catalog is published alongside the browser site on GitHub Pages (same origin and permissive CORS headers),
both `catalog.json` and the mod `.zip` archives are reachable from the browser build as well.

## Frame Counter

Frame Counter displays the average frames per second over the last 30 frames and the latest frame time in
milliseconds during gameplay. It supports the L0 and base packs. The counter starts in the top left when the
mod is active. Set `corner` to move it to any of the four corners or set `enabled` to hide it.

The [manifest](../../data/mods/frame_counter/mod.json) declares both [settings](reference/mods.md#settings).
The [Lua script](../../data/mods/frame_counter/scripts/presentation/main.lua) reads them at startup, follows
changes in `on_setting`, and positions the overlay inside the screen's safe area. See
[Presentation scripts](reference/scripting.md#overlay) for the overlay and frame statistics calls.

The [overlay test](../../tests/l0_official_frame_counter.txt) checks the counter and both settings on
the L0 pack. The [replay test](../../tests/l0_official_frame_counter_replay.txt) checks its world hashes
against a replay recorded with no mods.
