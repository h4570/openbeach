# Modding: guides

Step-by-step guides for mod authors, from nothing to a working mod.

| Guide | What it walks through |
|---|---|
| [First mod](first-mod.md) | Replacing a text key with the template and checking it on L0. |
| [first-building.md](first-building.md) | Adding a playable building: manifest, sprites and icon via `tools/make_sprite.py`, catalog and scenario patches, text, sound, and testing on L0. |
| [first-guest.md](first-guest.md) | Adding a guest type with its own object script: manifest, vocabulary patch, `.obs` script, guest-set patch, text, and testing on L0. |
| [first-need.md](first-need.md) | Adding a mod guest need, commodity family, serving building, and guest type on L0 (`tests/mods/l0_yoga/`). |
| [world-rule.md](world-rule.md) | A rule for every scenario: a world mod whose script (`scripts/world/*.wsc`) pays a bonus at the start of each day, kept across saves. |
| [Frame counter](frame-counter.md) | Drawing an overlay from a Lua presentation script. |
| [GUI patch](first-gui-patch.md) | Adding a menu button with a layout patch. |
| [Mod settings](first-setting.md) | Declaring settings and reading them from Lua. |
| [Key action](first-action.md) | Binding a key to a presentation script. |
| [Publishing](publishing.md) | Building a ZIP and listing it in a signed catalog. |
| [Developer mode](developer-mode.md) | Reloading active mods while developing them. |
| [first-busker.md](first-busker.md) | A character that does something: the generic object functions find a kiosk, walk to it, cheer the guests around and earn money. |
