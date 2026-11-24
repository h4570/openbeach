# Modding: reference

Exact contracts for mod authors and engine developers: the file formats of a mod (`mod.json`, text and catalog patches),
the layers the resolver reads, the start-up check, and the save and replay rules. Later pages will add the formats of
the DSL commands and the actions.

| Topic | What it covers |
|---|---|
| [mods.md](mods.md) | Mods as packages: the `mod.json` manifest, the activation setting, the layers of the resolver, the start-up check, text and catalog patches, saves and replays, and the tests that cover them. |
| [data-patches.md](data-patches.md) | Set or merge a member, address an array by key, append records, and resolve conflicts between mods. |
| [scripting.md](scripting.md) | Presentation scripts: `scripts/presentation/*.lua`, the `presentation` scope, the lifecycle, the sandbox and its limits, the `obl` API, the commands open to scripts, the per-mod store, the API version and the interpreter. |
