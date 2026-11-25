# Workflows

| File | What it does |
|---|---|
| [catalog.yml](catalog.yml) | Checks every mod with the engine, packages and signs the catalog with the `CATALOG_SIGNING_KEY` secret and publishes it to GitHub Pages from `main`; without the secret it only checks. |
