# Your first guest need mod

This walkthrough adds a new guest need (`yoga`), a matching commodity family and commodity, a `yoga_studio` building that serves it, and a `yogi` guest type that wants it, following the worked example in `tests/mods/l0_yoga/`. See [guest-needs.md](../reference/guest-needs.md) for the full contract (experimental).

## Folder layout

Place the mod in `<game dir>/mods/l0_yoga/`:

```text
l0_yoga/
├── mod.json
├── catalog/
│   ├── index.patch.json
│   ├── buildings/yoga_studio.json
│   ├── commodities/yoga_class.json
│   ├── commodity_families/yoga.json
│   └── guest_needs/yoga.json
├── guests/
│   └── l0.patch.json
├── scenarios/
│   └── s1/scenario.patch.json
└── text/
    └── en.patch.json
```

## 1. Manifest (`mod.json`)

Guest needs affect the simulation, so `scope` is `"world"`:

```json
{
  "format": "obl.mod",
  "version": 1,
  "id": "l0_yoga",
  "title": "L0 Yoga",
  "mod_version": "1.0.0",
  "pack": "l0",
  "scope": "world"
}
```

## 2. Declare the family, commodity and need

Create `catalog/commodity_families/yoga.json`:

```json
{
  "format": "obl.commodity_family",
  "version": 2,
  "key": "yoga",
  "name": "Yoga"
}
```

Create `catalog/commodities/yoga_class.json` pointing to that family:

```json
{
  "format": "obl.commodity",
  "version": 2,
  "key": "yoga_class",
  "name": "Yoga Class",
  "family": "yoga"
}
```

Create `catalog/guest_needs/yoga.json`:

```json
{
  "format": "obl.guest_need",
  "version": 1,
  "key": "yoga",
  "name": "GUEST_DESIRE_YOGA",
  "family": "yoga"
}
```

## 3. Add a building that serves the need

In `catalog/buildings/yoga_studio.json`, define a standard `"behavior": "none"` building (with `"instant_build": true` and `"power_exempt": true` if you want it ready immediately) whose `operation.attractor` is `"yoga"` and whose `commodities` list offers `"yoga_class"`. Register all four catalog records in `catalog/index.patch.json`:

```json
{
  "format": "obl.catalog_index_patch",
  "version": 1,
  "add": {
    "commodity_families": ["yoga"],
    "commodities": ["yoga_class"],
    "guest_needs": ["yoga"],
    "buildings": ["yoga_studio"]
  }
}
```

Enable the building in `scenarios/s1/scenario.patch.json` by appending `"yoga_studio"` to `/available_buildings`.

## 4. Give a guest type the need (`guests/l0.patch.json`)

Append a new guest type (`yogi`) with `"preferences": { "yoga": [0.05, 0.08] }` and `"max": { "yoga": 3 }`. Existing guest types that do not mention `"yoga"` never want it and keep their existing behavior.

Add the display strings in `text/en.patch.json` (`GUEST_DESIRE_YOGA`, `BUILDING_NAME_YOGA_STUDIO`, `BUILD_TIPTEXT_YOGA_STUDIO`, and `GUEST_THOUGHT_MISSED_YOGA1`..`3`).

## 5. Run and test on L0

Activate the mod with `OBL_PACK=l0` and `OBL_MODS=l0_yoga`. The worked game tests `tests/l0_mod_guest_needs.txt` and `tests/replay_l0_mod_guest_needs.txt` verify that pack guests ignore the need, `yogi` guests seek and visit the studio, save/load preserves the need and world hash, and replays match deterministically.
