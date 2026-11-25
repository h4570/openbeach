# Modding: reference

Exact contracts for mod authors and engine developers: the file formats of a mod (`mod.json`, text and catalog patches),
the layers the resolver reads, the start-up check, and the save and replay rules. Later pages will add the formats of
the DSL commands and the actions.

| Topic | What it covers |
|---|---|
| [mods.md](mods.md) | Mods as packages: the `mod.json` manifest, the activation setting, the layers of the resolver, the start-up check, text and catalog patches, saves and replays, and the tests that cover them. |
| [buildings.md](buildings.md) | Building records (`obl.building` v2), own icon/preview sprites, behaviors open to mods, and `tools/make_sprite.py`. |
| [data-patches.md](data-patches.md) | Set or merge a member, address an array by key, append records, and resolve conflicts between mods. |
| [gui-patches.md](gui-patches.md) | GUI layout patches (`gui/<layout>.patch.json`, `obl.gui_patch` v1): control IDs, slots, anchors, patch operations, `Mod:<id>.<event>` button actions, and runtime control lookup. |
| [actions.md](actions.md) | Named game actions with their default keys, how a mod declares, moves or takes over an action, the player's `[Keys]` section, and key conflicts. |
| [object-scripts.md](object-scripts.md) | Object scripts (`scripts/object/*.obs`), `scripts/object_vocabulary.patch.json`, per-type guest/staff script selection, and the built-in DSL functions and values. |
| [scripting.md](scripting.md) | Presentation scripts: `scripts/presentation/*.lua`, the `presentation` scope, the lifecycle, the sandbox and its limits, the `obl` API, the commands open to scripts, the per-mod store, the API version and the interpreter. |
| [recipes.md](recipes.md) | Asset recipes (`recipes/*.json`, `obl.asset_recipe` v1): deterministic integer/fixed-point image operations, PNG and indexed `obl.sprite` outputs, `mod-cache/<id>/` caching, missing sources, and the presentation scope rule. |
| [world-scripts.md](world-scripts.md) | World mod scripts: `scripts/world/*.wsc`, load order beside the scenario script, per-script state and variable budgets, `OnEvent` hooks, refused winning/losing calls, and save/replay behavior. |
