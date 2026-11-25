# Catalog repository template

Copy this directory into a new GitHub repository (the `.github/` folder at its top) to host your own mod catalog on
GitHub Pages. The [guide](../../guides/own-catalog.md) walks through the key, the secret, Pages and the first release.

| Part | What it is |
|---|---|
| [mods](mods/index.md) | One directory per mod, with the sample mod to replace. |
| [.github](.github/index.md) | The workflow `workflows/catalog.yml`. On every push and pull request: checks each mod with the engine, packages the ZIPs and signs `catalog.json` with the repository secret `CATALOG_SIGNING_KEY`, and publishes to Pages from `main`. Without the secret it only checks. |

Before the first run set `CATALOG_TITLE` (and `CATALOG_URL` for a custom domain) at the top of the workflow. The catalog
address to add in the game is the `CATALOG_URL` value.
