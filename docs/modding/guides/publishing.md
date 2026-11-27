# Publish a mod

## Where players see it
- **The official catalog**, which every player has: a mod that needs nothing but the engine, works on the L0 pack and
  takes no game data can be proposed as a pull request that adds it to `data/mods/` of this repository, with its tests
  and a page like the ones in [Official mods](../official-mods.md).
- **A community catalog** for any other mod, for example one written for the converted `base` pack. Community catalogs
  are run by their own maintainers, not reviewed by the OpenBeach project. Known ones:

  | Catalog | Address | How to add a mod |
  |---|---|---|
  | Community Mods (listed in the game by default) | `https://openbeach-larry.github.io/mods/catalog.json` | A pull request to [openbeach-larry/mods](https://github.com/openbeach-larry/mods) |

- **Your own catalog**: [Host your own mod catalog](own-catalog.md).

## Steps

1. Check the source with [check_mod.py](../reference/check-mods.md) on each target pack. Pick a version and include a license
   for the files you distribute.
2. ZIP the mod folder with its `mod.json` directly inside that folder. Its name must equal the manifest id. For example,
   run `python -m zipfile -c mod_template-1.0.0.zip mod_template` from the directory above the template.
3. Install the ZIP from **Mods** and test it on the target pack. Share the ZIP directly or publish it at an HTTPS address.
4. For a catalog, record the final ZIP's byte count and SHA-256. In PowerShell use
   `(Get-Item mod_template-1.0.0.zip).Length` and `(Get-FileHash mod_template-1.0.0.zip -Algorithm SHA256).Hash.ToLowerInvariant()`.
   On Linux, use `wc -c < mod_template-1.0.0.zip` and `sha256sum mod_template-1.0.0.zip`.
5. Add those values, the HTTPS ZIP URL and matching manifest metadata to a [catalog entry](../catalogs.md#mod-entry-fields).
   Sign the complete catalog as described in the [signing reference](../catalogs.md#signing-and-key-pinning), then add its
   URL from **Mods → Sources** and test the download. Use `tools/build_catalog.py` to package and sign a catalog from
   a directory of mod sources in one step.

To host many mods for others, follow [Host your own mod catalog](own-catalog.md).

Use the [template](../examples/mod_template/index.md) for a practice release.
