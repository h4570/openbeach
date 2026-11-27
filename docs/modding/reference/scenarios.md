# Mod scenarios and scenario sets

Mods can ship their own campaign scenarios and sandboxes beside the pack's campaign by adding a scenario set file under `scenarios/sets/<set id>.json` (format [`obl.scenario_set`](../../../data/schemas/scenario_set.schema.json) v1). Multiple independent mods can be active at once without replacing `scenarios/campaign.json`.

## Set file (`scenarios/sets/<set id>.json`)

```json
{
  "format": "obl.scenario_set",
  "version": 1,
  "id": "lagoon_tales",
  "title": "LAGOON_TALES_TITLE",
  "description": "LAGOON_TALES_DESC",
  "scenarios": [
    { "scenario": "lagoon_tales_reef" },
    { "scenario": "lagoon_tales_storm" }
  ],
  "unlock": "sequential",
  "initially_available": ["lagoon_tales_reef"],
  "requires_completed": ["s3"],
  "sandboxes": [
    { "scenario": "lagoon_tales_free" }
  ],
  "sandbox_unlock": "all"
}
```

- `id`: set identifier. Must start with the owning mod's id with `-` written as `_` (it may equal that prefix, or start with `<prefix>_`).
- `title`, `description`: text keys for the set name and optional description.
- `scenarios` (optional): ordered list of campaign scenario entries `{ "scenario": "<id>" }`. May be empty.
- `sandboxes` (optional): ordered list of sandbox entries `{ "scenario": "<id>" }`, independent of the number of campaign entries. May be empty.
- `unlock`: `"sequential"` (default; winning one scenario opens the next of the set, and the first is open unless `initially_available` lists the starting entries) or `"all"` (every scenario in the set is open).
- `initially_available` (optional): scenario ids from this set that start unlocked under `sequential`.
- `requires_completed` (optional): pack campaign scenario ids (such as `"s1"`, `"s3"`). Until all listed pack scenarios are completed, every scenario in the set stays locked.
- `sandbox_unlock`: `"all"` (default; every sandbox in the set is open) or `"set_completed"` (sandboxes open once every campaign scenario in the set is completed).

## Ids and validation

The engine discovers `scenarios/sets/*.json` over active mod layers in load order, then set order. The mod that ships the file owns the set.

- The set `id` and every scenario/sandbox id it lists must start with the owning mod's prefix (`<mod_id>` with `-` replaced by `_`).
- Every listed scenario must have `scenarios/<id>/scenario.json` in the owning mod, a valid `map` in the pack (or active mods), and a readable world script.
- Start-up and `--check-mods` refuse: a wrong prefix, a duplicate set or scenario id (including across two sets or colliding with the pack), a scenario not in its own mod, a missing `scenario.json`, `map` or `script`, or an unknown `requires_completed` id. On an unknown `map`, the error lists the map keys available in the pack.

## Pictures and texts

Each scenario's `scenarios/<id>/scenario.json` supplies its own presentation fields:

- `title` and `description` (and optional `menu_texts` `{ "title", "description" }`) are text keys looked up in the text table (supply them via `text/<lang>.patch.json`).
- `pictures` (`large`, `load`, `save`) can name either a sprite frame `{ "sprite": "<key>", "frame": <n> }` or a logical PNG path (such as `"scenarios/<id>/preview.png"`).

## Scenario scripts

A mod scenario's own `world.wsc` (or the file named by `script` in `scenario.json`) is parsed in strict mod mode and requires `STATE_WORLD_STARTUP`.

- `Function UnlockNextScenario`, `ShowMenu ScenarioWon`, and `ShowMenu ScenarioLost` are allowed (unlike world-mod scripts under `scripts/world/*.wsc`, which cannot end a scenario).
- `ShowMenu GameWon` is reserved for the pack campaign's end credits and is refused in mod scenario scripts. Winning the last scenario of a set marks it completed and returns to the menu.

## Progress, saves and replays

- Extra scenario progress is stored by scenario id in `progress.json` under the optional `"extra": { "<id>": <flags> }` object using the same flag bits as the 12 pack slots (`1` locked, `2` unlocked, `4` always available, `8` completed).
- Entries for mods that are not currently active are preserved when `progress.json` is written. "Reset progress" and the unlock cheat cover active extras.
- Saves record the scenario id and active mods in the header. Loading a save whose mod is inactive triggers the missing-mod confirmation and names the mod if the scenario is unknown. Replays record the mod like any world mod.
