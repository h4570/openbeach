# Your first building mod

This walkthrough builds a complete playable building — an ice cream stand — using the worked example in `tests/mods/l0_ice_stand/` as a template. A building mod needs only data, sprites, and optional sound: no scripts and no engine changes.

## Folder layout

Place the mod in `<game dir>/mods/l0_ice_stand/`:

```text
l0_ice_stand/
├── mod.json
├── catalog/
│   ├── index.patch.json
│   └── buildings/
│       └── ice_stand.json
├── scenarios/
│   └── s1/
│       └── scenario.patch.json
├── sprites/
│   ├── ice_stand.json
│   ├── ice_stand.palettes.png
│   ├── ice_stand/
│   │   ├── color.png
│   │   ├── depth.png
│   │   └── lightmap.png
│   ├── ice_stand_icon.json
│   ├── ice_stand_icon.palettes.png
│   └── ice_stand_icon/
│       └── ice_stand_icon.png
├── sounds/
│   ├── chime.wav
│   └── sound_script.patch.json
└── text/
    └── en.patch.json
```

## 1. Manifest (`mod.json`)

A new building changes the simulation, so `scope` is `"world"` (the default):

```json
{
  "format": "obl.mod",
  "version": 1,
  "id": "l0_ice_stand",
  "title": "Ice Cream Stand",
  "mod_version": "1.0.0",
  "pack": "l0",
  "scope": "world"
}
```

Change `"pack"` to `"base"` if your mod targets the full game pack.

## 2. Sprites and icon (`tools/make_sprite.py`)

The renderer draws buildings from indexed `obl.sprite` assets (a 256-color palette PNG plus gray+alpha sheets for the `color` and `depth` planes, and an optional `lightmap` plane for night glow). Convert your own PNG art with `tools/make_sprite.py`:

```powershell
# Building sprite (reusing one PNG across 4 directions, auto-generated depth plane, optional night lightmap):
python tools/make_sprite.py `
  --name ice_stand --kind building --directions 4 --repeat-directions `
  --hotspot -48 -78 --lightmap art/light.png `
  --out mods/l0_ice_stand/sprites --frames art/stand.png

# 48x48 build-menu icon:
python tools/make_sprite.py `
  --name ice_stand_icon --kind ui `
  --out mods/l0_ice_stand/sprites --frames art/icon.png
```

If you draw all four rotations, omit `--repeat-directions` and pass four PNGs (`--frames north.png east.png south.png west.png`). See [buildings.md](../reference/buildings.md#sprites-and-icons) for custom depth maps and multi-animation specs.

## 3. Building record and catalog patch

Add the building key to `catalog/index.patch.json`:

```json
{
  "format": "obl.catalog_index_patch",
  "version": 1,
  "add": { "buildings": ["ice_stand"] }
}
```

Then write `catalog/buildings/ice_stand.json` (excerpt; see `tests/mods/l0_ice_stand/catalog/buildings/ice_stand.json` for the full footprint):

```json
{
  "format": "obl.building",
  "version": 2,
  "key": "ice_stand",
  "name": "ICE_STAND",
  "category": "food",
  "behavior": "ice_stall",
  "sprite": "ice_stand",
  "icon_sprite": "ice_stand_icon",
  "cost": { "build": 500, "refund": 350 },
  "construction": { "build_time": 1, "demolish_time": 1, "max_builders": 4 },
  "size": [3, 3],
  "build_on": ["land", "beach"],
  "operation": {
    "guest_capacity": 4,
    "opening_hours": "allhours",
    "max_instances": 99,
    "max_queue": 6,
    "attractor": "softdrnk",
    "power": 0,
    "staff": { "types": [], "max": 0 }
  },
  "commodities": [
    {
      "commodity": "sodas",
      "intensity": [0.9, 0.9],
      "daily_intensity_factor": 0.98,
      "price": 3.0,
      "daily_price_factor": 0.98,
      "cost": 0.2
    }
  ],
  "requires": "rep",
  "menu": "L0Building"
}
```

`category` (`"hotel"`, `"food"`, or `"leisure"`) decides which tab of the build panel lists the building. `requires` names a prerequisite building that must already exist before the player can place this one (`"rep"` on L0, where the rep station is present at startup; omitting `requires` or setting `"NONE"` keeps a building locked). `icon_sprite` gives it its own build-panel icon and info-dialog thumbnail without replacing the pack's shared `UI3_bp_thumb` sheet.

## 4. Make it buildable in a scenario

The build panel lists buildings that appear in the scenario's `available_buildings` array. Append your building key with a [JSON data patch](../reference/data-patches.md) at `scenarios/s1/scenario.patch.json`:

```json
{
  "format": "obl.json_patch",
  "version": 1,
  "ops": [
    { "op": "append", "path": "/available_buildings", "items": ["ice_stand"] }
  ]
}
```

## 5. Text and sound

Add the display name (`BUILDING_NAME_<KEY>` in uppercase for the build panel) and build-panel tooltip key (`BUILD_TIPTEXT_<KEY>` in uppercase) in `text/en.patch.json`:

```json
{
  "format": "obl.text_patch",
  "version": 1,
  "entries": {
    "ICE_STAND": "Ice Cream Stand",
    "BUILDING_NAME_ICE_STAND": "Ice Cream Stand",
    "BUILD_TIPTEXT_ICE_STAND": "Ice Cream Stand"
  }
}
```

To attach an ambient sound, drop a 16-bit PCM WAV file in `sounds/chime.wav` and patch `sounds/sound_script.patch.json`:

```json
{
  "format": "obl.json_patch",
  "version": 1,
  "ops": [
    {
      "op": "set",
      "path": "/building_sounds/ice_stand",
      "value": { "sample": "chime", "volume": 0.8, "loop": true }
    }
  ]
}
```

## 6. Test it on L0

Run the game with `OBL_PACK=l0` and `OBL_MODS=l0_ice_stand` (or activate `l0_ice_stand` in `openbeach.ini` under `[Mods] Active=l0_ice_stand`), open the Build menu, select **Ice Cream Stand**, place it on the beach, and watch builders construct it and guests buy from it. The automated check lives in `tests/l0_mod_building.txt`.
