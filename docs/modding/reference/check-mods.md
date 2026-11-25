# Check mods before starting the game

Run the engine checker from a terminal. Point `--game-dir` at a directory that contains the target pack.

```sh
OpenBeach --game-dir ./run --check-mods
OpenBeach --game-dir ./run --check-mods ./my-mod
obl-sim --game-dir ./run --check-mods ./my-mod
```

Without a source directory, the checker uses the game's [activation settings](mods.md), including the order of the
active mods. With a source directory it uses the ID in that directory's manifest and checks that mod by itself. To check
a mod with requirements, install its dependencies and the mod under `mods/`, then check the active list in dependency order.
Use `OBL_PACK=l0` for the L0 pack. The default pack is `base`.

The checker exits with status 0 when the checks pass and 8 when it finds errors. It reports independent errors together.
When a file cannot be parsed, fix that diagnostic first and rerun so the checker can validate the data that depends on it.
It loads presentation scripts in the normal Lua sandbox and calls `on_start` with a headless host. Run a game test for
frame, input, and world callbacks and for behavior that needs a live game. The checker leaves settings and mod stores untouched. It validates [asset recipes](recipes.md) in memory without writing cache files.

| Message class | What to check |
|---|---|
| Pack, manifest or dependency | Install the target pack, fix the ID, version or field named in `mod.json`, and put required mods earlier in the active list. |
| Scope or API | Move simulation data to a world mod, remove world-mod settings, or use a game that supports the declared presentation API. |
| Settings, actions or keys | Compare setting types and defaults with the manifest reference. Fix unknown actions, keys, and references to undeclared settings. |
| JSON schema or patch | The file and JSON pointer or operation identify the invalid field. The game resolves targets through the active layers. |
| Text, GUI or catalog patch | Check the format and the target. Presentation text patches may add keys. Catalog additions must name distinct existing records. GUI operations must name an existing layout, control or slot where required. |
| Asset recipe | Check the output path, recipe schema, and operation bounds. A missing source disables that output and prints a warning, as it does during conversion. |
| Reference or asset | Check the spelling and existence of the catalog record, scenario, guest set, sprite, text key or object script. |
| Object vocabulary or script | Use distinct state and message names. Fix the header, state, message or command named by the parser. |
| World script | Fix a parse error or a refused command. A mod cannot open the winning or losing menus or unlock the next scenario. |
| Presentation script | Fix the reported Lua file or startup callback. Sandbox errors and exhausted instruction or memory budgets also fail the check. |
| Conflict | Overrides of the same value, key or input area report the winner. A declared incompatibility is an error. |

For mod CI, the Python wrapper also runs the file checks from `tools/check_content.py` over the mod's source tree.
Those rules detect installation files, player data, archives and converted-pack provenance markers. The engine performs
pack and mod validation, and the wrapper performs the content scan.

```sh
python tools/check_mod.py ./my-mod --engine ./OpenBeach --game-dir ./run --pack l0
```
