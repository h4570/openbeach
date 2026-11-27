# First scenario

This guide builds a one-scenario mod set on an island of the player's copy (pack `base`): pick a map key, write `scenario.json`, add texts, write a `world.wsc` with one objective and a win, attach a picture as `{sprite, frame}`, and check and test it.

A working L0 variant lives in [scenario_template](../examples/scenario_template/index.md).

## 1. Create the mod folder and manifest

Create `mods/sunny_cove/mod.json`:

```json
{
  "format": "obl.mod",
  "version": 1,
  "id": "sunny_cove",
  "title": "Sunny Cove",
  "mod_version": "1.0.0",
  "scope": "world",
  "api": 1,
  "packs": ["base"]
}
```

Every scenario ID and set ID in the mod must start with the mod's ID (`sunny_cove`).

## 2. Declare the scenario set

Create `mods/sunny_cove/scenarios/sets/sunny_cove.json`:

```json
{
  "format": "obl.scenario_set",
  "version": 1,
  "id": "sunny_cove",
  "title": "SUNNY_COVE_SET_TITLE",
  "description": "SUNNY_COVE_SET_DESC",
  "scenarios": [
    { "scenario": "sunny_cove_opening" }
  ],
  "unlock": "all"
}
```

## 3. Pick a map key and write `scenario.json`

Pick any map key from `maps/maps.json` in the pack (for `base`, the campaign islands such as `island1`; for `l0`, `l0_island`). Point `pictures` at a sprite and frame from the pack (`{ "sprite": ..., "frame": ... }`) or a PNG/TGA file in the mod.

Create `mods/sunny_cove/scenarios/sunny_cove_opening/scenario.json`:

```json
{
  "format": "obl.scenario",
  "version": 1,
  "id": "sunny_cove_opening",
  "kind": "campaign",
  "title": "SUNNY_COVE_OPENING_TITLE",
  "description": "SUNNY_COVE_OPENING_DESC",
  "map": "scenario1",
  "start_view": [2048, 2048],
  "catalog": "catalog/index.json",
  "guests": "s1",
  "available_buildings": ["loo", "kiosk", "hut"],
  "tilesets": { "pavement": "pathtile_01" },
  "radar": { "image": "dummy", "pos": [9, 0] },
  "progress_dialog": "ScenarioProgress",
  "script": "world.wsc",
  "pictures": {
    "large": { "sprite": "UI3_Fe_Pics_Large", "frame": 0 },
    "load": { "sprite": "UI3_Fe_Pics_Small", "frame": 0 },
    "save": { "sprite": "UI3_Fe_Pics_Small", "frame": 0 }
  }
}
```

## 4. Add the texts

Create `mods/sunny_cove/text/en.patch.json`:

```json
{
  "format": "obl.text_patch",
  "version": 1,
  "entries": {
    "SUNNY_COVE_SET_TITLE": "Sunny Cove",
    "SUNNY_COVE_SET_DESC": "A short custom scenario set on a sunny island.",
    "SUNNY_COVE_OPENING_TITLE": "Opening Weekend",
    "SUNNY_COVE_OPENING_DESC": "Welcome your first guests and open the resort.",
    "SUNNY_COVE_WON": "Opening Weekend complete!"
  }
}
```

## 5. Write `world.wsc` with one objective and a win

Create `mods/sunny_cove/scenarios/sunny_cove_opening/world.wsc`:

```text
#obl-script 1 world
State STATE_WORLD_STARTUP
	OnEntry
		Set PlayerCash 20000;
		MoveCamera 2048 2048 0;
		ChangeState STATE_WORLD_RUNGAME;
	End
	OnUpdate
	End
	OnExit
	End
End

State STATE_WORLD_RUNGAME
	OnEntry
	End
	OnUpdate
		If ( PlayerCash >= 25000 )
			ShowMenu ScenarioWon SUNNY_COVE_WON;
			Function UnlockNextScenario;
			ChangeState STATE_WORLD_AFTERGAME;
			Function PauseGame;
		End
	End
	OnExit
	End
End

State STATE_WORLD_AFTERGAME
	OnEntry
	End
	OnUpdate
	End
	OnExit
	End
End
```

See [scenarios.md](../reference/scenarios.md) and [world-scripts.md](../reference/world-scripts.md) for the full set of fields and commands.

## 6. Check and test

Validate the mod before launching:

```powershell
OpenBeach.exe --check-mods mods/sunny_cove
```

Then enable `sunny_cove` in the **Mods** screen, open **Play** (campaign), click **More scenarios**, select **Opening Weekend**, and click **Play**.
