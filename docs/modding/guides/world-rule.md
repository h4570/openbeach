# A rule for every scenario

This guide makes a world mod that pays the player a bonus at the start of each day, in every scenario. Reference:
[world-scripts.md](../reference/world-scripts.md).

## 1. The manifest
`mods/daily_bonus/mod.json`:
```json
{
  "format": "obl.mod",
  "version": 1,
  "id": "daily_bonus",
  "title": "Daily bonus",
  "mod_version": "1.0.0",
  "pack": "base"
}
```
No `scope` means a world mod: saves and replays will record it.

## 2. The script
`mods/daily_bonus/scripts/world/daily_bonus.wsc`:
```text
#obl-script 1 world
State STATE_WORLD_STARTUP
	OnEntry
		ChangeState STATE_PAYING;
	End
	OnUpdate
	End
	OnExit
	End
End

State STATE_PAYING
	OnEntry
	End
	OnUpdate
	End
	OnEvent DayStarted
		Add PlayerCash 5000;
	End
	OnExit
	End
End
```
The bonus lives in a state of its own: `STATE_WORLD_STARTUP` runs again when a save loads, while the state a save
restores is where the rule belongs. The script has its own `Var0`..`Var31` and `Flag0`..`Flag31` for counters, apart from
the scenario's.

## 3. Switch it on
Add `daily_bonus` to `Active` in the `[Mods]` section of `openbeach.ini` ([switching-mods.md](../players/switching-mods.md)) and
start any scenario. A script error, or a call that wins or loses the scenario, stops the start with a message naming the mod
and the file.

## 4. Test it on L0
`tests/mods/l0_world_script` is this mod for the L0 pack, and `tests/l0_mod_world_script.txt` checks it: the cash after two
days, and after a save written before midnight.
