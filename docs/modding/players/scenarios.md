# Mod scenarios

Mods can add their own scenario sets beside the campaign and sandbox islands without replacing the built-in ones.

## Where they appear

When at least one active mod adds scenarios for the current mode (campaign or sandbox), a **More scenarios** button appears at the top of the scenario selection screen.

- **Left list:** mod scenarios grouped by set, in mod load order, with **Done** and **Locked** marks. A locked entry explains which scenario you need to win first.
- **Right card:** the selected scenario's picture, title, status, description, and its set's description.
- **Play** starts the selected scenario (disabled while a locked scenario is selected). **Back** returns to the island map.

When you select a mod scenario and return to the island map, no island radio button is selected; clicking any island radio button selects that built-in scenario again.

## How they unlock

Each scenario set chooses its own unlock rules:

- **Sequential:** the first scenario (or the set's initially available scenarios) is open from the start, and winning each scenario unlocks the next one in the set.
- **All:** every scenario in the set is open from the start.
- **Prerequisites:** a set can require winning one or more campaign scenarios (for example `s3`) before any of its scenarios unlock.
- **Sandboxes:** a set's sandbox scenarios are either open from the start or unlock once all campaign scenarios in that set are won.

## Progress when a mod is off

Completion progress for mod scenarios is stored by scenario ID in `progress.json` alongside your campaign progress. Turning a mod off hides its scenarios from the menu, but keeps your progress so it is still there when you turn the mod back on.
