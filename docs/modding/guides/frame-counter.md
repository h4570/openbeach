# Draw a frame counter

Use the [Frame Counter manifest](../../../data/mods/frame_counter/mod.json) and its
[Lua script](../../../data/mods/frame_counter/scripts/presentation/main.lua). The manifest declares presentation scope,
API 1 and the two supported packs. Put the script in `scripts/presentation/main.lua`.

1. In `on_frame`, return early if `obl.game.in_world()` is false.
2. Format `obl.stats.fps()` and `obl.stats.frame_ms()` into a short label. Use `obl.overlay.measure` to size its background.
3. Use `obl.screen.size()` and `obl.screen.safe_area()` to anchor the label. Draw it with `obl.overlay.rect` and `obl.overlay.text`.

Activate the mod and start a scenario. Switch its corner from the Mods screen to verify the result. See the
[script API](../reference/scripting.md) and [settings guide](first-setting.md).
