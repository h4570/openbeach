# Mod errors and conflicts

When a mod stops the game from starting, check the last `FatalError` line in `openbeach.log` in the game directory.
The message names the mod and, when available, the file or setting that needs attention.

- **Missing mod, wrong pack or newer API.** Enable the required mod earlier in the load order, select a compatible pack,
  or update the game or mod.
- **Invalid file, patch, reference or script.** Update the mod, or send its author the message from the log. Remove the
  mod from the active list to start the game while you wait for a fix.
- **Conflict.** A shared key, value or input area follows the priority shown in the conflict message. Mods that declare
  an incompatibility cannot be active together. Use the [Mods screen](mods-screen.md) to change the order or disable a mod.

If the game does not reach the menu, follow [switching mods](switching-mods.md). Mod authors can check a full set at once
with the [command-line checker](../reference/check-mods.md).
