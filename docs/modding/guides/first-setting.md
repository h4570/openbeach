# Give a mod settings

The [l0_settings manifest](../../../tests/mods/l0_settings/mod.json) defines a toggle (`wide`), a choice (`theme`) and a
bounded number (`zoom`). Give each setting an id, a label and a default. Supply labelled `options` for a choice and bounds
for a number.

In the [Lua script](../../../tests/mods/l0_settings/scripts/presentation/main.lua), read the initial values with
`obl.settings.get(id)`, then handle changes with `on_setting(id, value)`. Activate the mod, select it in **Mods** and edit
the values in its settings panel. The game writes them under `[Mod.l0_settings]` in `openbeach.ini`.

See [settings fields](../reference/mods.md#settings) for exact types, bounds and label keys.
