# Give a mod a key action

The [l0_actions manifest](../../../tests/mods/l0_actions/mod.json) declares `ping` and gives it the default key F9.
Declare the action on a presentation mod with an API 1 Lua script. Use the short id in the script's `on_action(id)`
and the qualified id `l0_actions.ping` in the player's `[Keys]` section.

Activate the example and press F9 in a scenario. The [script](../../../tests/mods/l0_actions/scripts/presentation/main.lua)
logs the action. The example also binds the built-in `game.pause` action to F10. If a key overlaps, the player's binding
wins first, followed by the later mod in load order. See [named actions](../reference/actions.md).
