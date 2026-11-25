# Your first guest mod

This walkthrough adds a new guest type — a busker — with its own object script, following the worked example in `tests/mods/l0_busker/`. The guest arrives on L0, walks to a spot on the beach, plays an animation while waiting on a custom mod message, and then leaves.

## Folder layout

Place the mod in `<game dir>/mods/l0_busker/`:

```text
l0_busker/
├── mod.json
├── guests/
│   └── l0.patch.json
├── scripts/
│   ├── object_vocabulary.patch.json
│   └── object/
│       └── busker.obs
└── text/
    └── en.patch.json
```

## 1. Manifest (`mod.json`)

Object scripts run inside the simulation tick, so `scope` is `"world"`:

```json
{
  "format": "obl.mod",
  "version": 1,
  "id": "l0_busker",
  "title": "L0 Busker",
  "mod_version": "1.0.0",
  "pack": "l0",
  "scope": "world"
}
```

## 2. Declare states and messages (`scripts/object_vocabulary.patch.json`)

List the new state (`STATE_*`) and message (`MT_*`) names your script uses. The engine assigns their numeric ids after the pack's in mod load order and refuses collisions with the pack or earlier mods:

```json
{
  "format": "obl.object_script_vocabulary_patch",
  "version": 1,
  "states": [
    "STATE_BUSKER_WALK",
    "STATE_BUSKER_PERFORM"
  ],
  "messages": [
    "MT_BUSKER_DONE"
  ]
}
```

## 3. Write the object script (`scripts/object/busker.obs`)

Start with `#obl-script 1 object`. `STATE_NULL`, `STATE_STARTUP`, and `STATE_GUEST_LEAVING_WORLD` come from the pack's vocabulary (leaving in `STATE_GUEST_LEAVING_WORLD` counts the departure in the resort totals); the two busker states and `MT_BUSKER_DONE` come from the patch above:

```text
#obl-script 1 object
State STATE_NULL
	OnUpdate
	End
	OnEntry
		Set WalkAnimation "D_WALK_NORMAL";
		Set StandAnimation "D_STAND_NORMAL";
		Set WalkSpeed 60;
		ChangeState STATE_STARTUP;
	End
End

State STATE_STARTUP
	OnUpdate
	End
	OnEntry
		ChangeState STATE_BUSKER_WALK;
	End
	OnExit
		PlayAnimation WalkAnimation;
	End
End

State STATE_BUSKER_WALK
	OnEntry
		PlayAnimation WalkAnimation;
		Message MT_MOVE2POINT Me 62 62;
	End
	Message MT_MOVEIMHERE
		OnReceipt
			ChangeState STATE_BUSKER_PERFORM;
		End
	End
	Message MT_PATHBLOCKED
		OnReceipt
			Message MT_MOVE2POINT Me 62 62;
		End
	End
End

State STATE_BUSKER_PERFORM
	OnEntry
		PlayAnimation StandAnimation;
		Message MT_BUSKER_DONE Me 0 0 Time 4;
	End
	Message MT_BUSKER_DONE
		OnReceipt
			ChangeState STATE_GUEST_LEAVING_WORLD;
		End
	End
End

State STATE_GUEST_LEAVING_WORLD
	OnEntry
		Function ClearMyDelayedMsgs;
		Message MT_KILLOBJECT -2 Me 0;
	End
End
```

See [object-scripts.md](../reference/object-scripts.md) for the full list of commands, functions, and values.

## 4. Add the guest type (`guests/l0.patch.json`) and text (`text/en.patch.json`)

Append the new guest type to the scenario's guest set using a JSON patch ([data-patches.md](../reference/data-patches.md)), naming the script stem in `"script"`:

```json
{
  "format": "obl.json_patch",
  "version": 1,
  "ops": [
    {
      "op": "append",
      "path": "/types",
      "items": [
        {
          "key": "busker",
          "name": "BUSKER",
          "gender": "m",
          "sprite": "dummy",
          "script": "busker",
          "money": 0,
          "nights": [1, 1],
          "sleep_time": [22, 24],
          "wake_time": [7, 9]
        }
      ]
    }
  ]
}
```

Add the display name in `text/en.patch.json`:

```json
{
  "format": "obl.text_patch",
  "version": 1,
  "entries": {
    "BUSKER": "Busker"
  }
}
```

## 5. Run and test on L0

Activate the mod with `OBL_PACK=l0` and `OBL_MODS=l0_busker` (or `[Mods] Active = l0_busker` in `openbeach.ini`). Any syntax error, unknown state/message, or name collision stops start-up naming the mod, file, and line.

The worked game tests `tests/l0_mod_object_script.txt` and `tests/l0_mod_object_script_replay.txt` verify determinism across runs, mid-script save/reload in `STATE_BUSKER_PERFORM`, and replay playback with `l0_busker`.
