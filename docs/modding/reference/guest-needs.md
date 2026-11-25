# Guest needs

> **Experimental:** The guest-needs format (`obl.guest_need` v1), guest-set need fields, and `GetGuestDesire` / need-key `ChangeGuestsDesire` script calls are experimental and may change before stabilizing.

A world mod (`"scope": "world"`) can declare extra guest needs alongside the 26 built-in desires of the pack. Pack desires keep their fixed slots, growth rates, weather modifiers, and complaint counters unchanged; mod needs follow them in mod load order. For a step-by-step walkthrough, see [first-need.md](../guides/first-need.md).

## Catalog record (`obl.guest_need` v1)

Place one file per need in `catalog/guest_needs/<key>.json` (validated against `data/schemas/guest_need.schema.json`) and append `<key>` under `add.guest_needs` in `catalog/index.patch.json`:

```json
{
  "format": "obl.guest_need",
  "version": 1,
  "key": "yoga",
  "name": "GUEST_DESIRE_YOGA",
  "family": "yoga"
}
```

| Field | Type | Meaning |
|---|---|---|
| `format` | `"obl.guest_need"` | **Required.** Format tag. |
| `version` | `1` | **Required.** Schema version. |
| `key` | key | **Required.** Unique catalog key; must match `<key>.json` and the `catalog/index.patch.json` entry. |
| `name` | text key | **Required.** Text-table key shown in the guest panel when this need is the guest's strongest desire. |
| `family` | key | **Required.** Commodity family key (`catalog/commodity_families/<family>.json`) whose buildings serve this need. |

`guest_needs` is optional in `catalog/index.json` (packs without it load with zero needs).

### Limits and mod headroom

- Up to **16** guest needs per world (`tables::kMaxGuestNeeds`).
- Mod catalog patches may add up to **16** commodity families and **32** commodities beyond the pack's base caps (24 families and 41 commodities), so a mod can introduce a new family and commodity even on packs already at the base cap.

### Load-time refusals

The catalog and guest-set loaders refuse a need if:

- `key` matches any of the 26 pack desire slots, pack `preferences` names, or pack `max` names;
- `key` is added twice;
- `family` is `"none"`, does not exist in the catalog, or already serves a pack desire (compared without ASCII case);
- two needs share the same `family`.

## Guest-set preferences and limits (`obl.guest_set`)

In `guests/<set>.json` (typically via `guests/<set>.patch.json`), any guest type can opt into a need by adding its key to `preferences` and `max`:

```json
"preferences": {
  "yoga": [0.05, 0.08]
},
"max": {
  "yoga": 3
}
```

- `preferences.<need>` is a `[min, max]` growth-rate range. When a guest of that type is constructed, one random draw after all built-in draws picks its rate.
- `max.<need>` is the repeat limit (like `MAX_<desire>`).
- A guest type that omits `preferences.<need>` never wants the need, draws nothing for it, and stays unchanged.
- Any unknown name in `preferences` or `max` that is neither a pack name nor a declared guest need is refused at load.

## Serving a need, thoughts and panel text

- **Buildings:** any building that offers a commodity in the need's `family` serves the need automatically through its normal `commodities` list (no special `behavior` or trait is required). See [buildings.md](buildings.md).
- **Priority and growth:** a need grows by `dt * rate` clamped to `1.0` (weather modifiers do not affect it). During desire search, needs are checked after the 26 pack desires; ties go to the pack desire and a need at `0` is never picked.
- **Refusal thought:** if no provider can serve the need, the guest records `GUEST_THOUGHT_MISSED_<FAMILY>` (with `<FAMILY>` uppercased, followed by `1`..`3` as with other thoughts).
- **Panel text:** `GUEST_DESIRE` and `GUEST_DESIRE_0` display the need's `name` text key when a need is strictly stronger than every pack desire. `GUEST_DESIRE_<KEY>` (with `<KEY>` uppercased) formats the need's current value.

## Object scripts

- `ChangeGuestsDesire` ([object-scripts.md](object-scripts.md#generic-functions-for-mod-characters)) accepts a need key in `Arg2` and updates only guests whose type names that need.
- `GetGuestDesire` reads a guest's desire or need value (`Arg1` guest id, `Arg2` desire or need key; returns `0` for a need the guest's type does not name).

## Saves and world hashes

- `obl.save` stores `objects[].guest.needs` as `{ "<key>": [rate, desire] }` only for guests whose type has needs in the world.
- When an older save without a `needs` entry (or without a newly added need) is loaded into a world that declares the need, existing guests whose type names that need receive the midpoint of their type's `preferences.<need>` range as their growth rate and `0` as the current desire (without consuming any RNG draws).
- World hashes emit `guest.needs` only when the guest's type has needs, so worlds without guest-need mods keep identical saves and hashes.
