# Publish a mod

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
   URL from **Mods → Catalogs** and test the download. Use `tools/build_catalog.py` to package and sign a catalog from
   a directory of mod sources in one step.

Use the [template](../examples/mod_template/index.md) for a practice release.
