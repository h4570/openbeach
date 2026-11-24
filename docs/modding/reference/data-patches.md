# JSON data patches

To change a pack file, put a patch at the same logical path in your mod and change the filename ending from `.json` to `.patch.json`. For example, `guests/l0.patch.json` edits the L0 guest set.

```json
{
  "format": "obl.json_patch",
  "version": 1,
  "ops": [
    { "op": "set", "path": "/types[key=visitor]/money", "value": 250 },
    { "op": "merge", "path": "/types[key=visitor]", "value": { "litter": 0.2 } },
    { "op": "append", "path": "/types", "items": [
      { "key": "surfer", "name": "Surfer", "gender": "m", "sprite": "dummy" }
    ] }
  ]
}
```

The engine starts with the highest-priority whole copy of the file, including full replacements. It applies active mods' patches in load order and operations in file order. It checks the result against the embedded pack schema before gameplay. A rejected operation reports the mod, patch filename, zero-based operation number and path.

| Operation | Effect |
|---|---|
| `set` | Assign `value` to a member. A final object member can be new if the schema permits it. |
| `merge` | Recursively merge the members of `value` into an existing object. |
| `append` | Add every element of `items` to the end of the target array, preserving order. |

Paths start with `/`. Use `~1` for a literal slash and `~0` for a tilde in an object member name. To descend into an array, select an object by its identity field, for example `/types[key=visitor]` or `/messages[id=0]`. A selector must match exactly once. Numeric array positions, removal and reordering are unsupported. Two appends both take effect, in load order. When two mods assign the same member, the later assignment wins and the startup log names both mods (`mod patch: mod late: ... op 0 /path overrides mod early: ... op 2 /path`). Changing an element another mod appended builds on that mod and is not reported.

A patch whose target file does not exist, invalid JSON, an unknown operation, a path that does not resolve or a result that fails the schema stops the start-up with the mod, the patch file, the operation number and the path.

## Files

| Pack data | Classification and behavior |
|---|---|
| `rules/*.json`, `guests/*.json`, `catalog/<category>/*.json` | World. These feed time, economy, guest behavior and object definitions. For example, set `/parameters/day_length_in_seconds` in `rules/game_parameters.patch.json`. |
| Scenarios, campaign order, map data and script vocabularies | World. The engine consumes these values when it creates and updates the world. Converter metadata such as water-sheet descriptions has an effect only if a runtime reader uses it. |
| `sprites/<sprite>.json` | World. Animation length affects object behavior and palette choices can draw from the simulation RNG. |
| `sounds/sound_script.json` | World. A guest event with sounds consumes a simulation RNG draw. An absent or empty event consumes none. |
| `gui/object_browser.json`, `gui/credits.json`, `gui/layouts.json`, `fonts/*.json`, `sprites/palette_modifiers.json` | Presentation data. These describe lists, text rendering, layout choices and color tables. |
| `text/<language>.patch.json`, `catalog/index.patch.json` | Use the existing text-entry and catalog-name append formats. [Package reference](mods.md) |
| Manifests, saves and player progress | These have their own readers and do not accept generic pack patches. |
| Images, sound samples, script source and `.gui` files | These are outside the JSON format. |

World mods are recorded in saves and replays in load order. Changing a patch changes its mod's content hash. Replay playback requires the recorded mods and matching hashes.
