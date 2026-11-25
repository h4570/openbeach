# Official mods

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
