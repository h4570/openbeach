# A busker that earns money

[first-guest.md](first-guest.md) makes a guest walk and wait. This guide makes a character that does something, with the generic functions of [object-scripts.md](../reference/object-scripts.md#generic-functions-for-mod-characters). The worked example is `tests/mods/l0_busker_show/`: a busker who looks for the nearest kiosk, walks to it, and every 5 seconds cheers up the guests around him and earns money.

The mod has the same layout as the first guest: a `mod.json` with `"scope": "world"`, a vocabulary patch with its states and messages, a guest patch that gives a guest type the script, and the script in `scripts/object/`. The test mod gives the script to the pack's `visitor` type by merging a `script` field, so every arriving guest is a busker:

```json
{ "format": "obl.json_patch", "version": 1, "ops": [
  { "op": "merge", "path": "/types[key=visitor]", "value": { "script": "busker_show" } }
]}
```

A mod that adds its own type appends it instead, as in [first-guest.md](first-guest.md).

## Find the kiosk and walk to it

```text
State STATE_SHOW_LOOK
	OnEntry
		Set Arg1 "kiosk";
		Function FindNearestBuilding;
		If (Result != NO_OBJECT)
			Set Arg1 Result;
			Function WalkToObject;
			ChangeState STATE_SHOW_WALK;
		Else
			ChangeState STATE_SHOW_PLAY;
		End
	End
End
```

The walk ends in the usual messages, so `STATE_SHOW_WALK` waits for the message for arriving (and, to be safe, for a blocked path) and then changes to the playing state.

## Play

```text
State STATE_SHOW_PLAY
	OnEntry
		Message MT_SHOW_TICK Me 0 0 Time 5;
		Message MT_SHOW_END Me 0 0 Time 16;
	End
	Message MT_SHOW_TICK
		OnReceipt
			Set Arg1 6;
			Set Arg2 0.05;
			Function ChangeGuestsHappiness;
			Set Arg1 5;
			Add Arg1 Result;
			Function PayMoney;
			Message MT_SHOW_TICK Me 0 0 Time 5;
		End
	End
End
```

Each tick raises the happiness of every guest within 6 tiles by 0.05, and the busker earns 5 plus 1 for each guest he reached. The income shows in the finances as an attraction income. The act ends with `MT_SHOW_END`, which sends him to the state that makes him leave.

## Test it

`tests/l0_mod_object_functions_busker.txt` follows the usual pattern: `seed 1`, `fixed-dt 0.02`, `spawn-building kiosk`, `spawn-guests`, `step`, `expect income-today`, then a save in the middle of the act that reloads twice and gives the same world hash. A wrong argument or a limit stops the game with the mod script line, so a test shows it at once.
