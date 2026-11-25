# Iterate in developer mode

Set `Dev=1` in the `[Mods]` section of the game directory's `openbeach.ini`, then launch OpenBeach.

Edit files in an active mod under `mods/<id>/`. The game checks presentation changes periodically and restarts the scripts
after a change. Text and GUI edits rebuild the interface, so open panels may close. The most recent error appears on screen,
with details in `openbeach.log`. The log also names the mod that consumed an input event.

A world uses the version of the mod it started with. Start a new scenario or sandbox to pick up edits to active world mods.
Changes to shared sprite or sound files, game parameters or the campaign list require restarting the game. Loading a save
or starting a recording or replay keeps the current world mod versions. Developer mode works in the interactive host.
To regenerate assets after editing a recipe, follow the [recipe guide](../reference/recipes.md).
