# Host your own mod catalog

Goal: a signed [catalog](../catalogs.md) on GitHub Pages that players add by address, with your mods checked, packaged and
signed on every push. You need a GitHub account, Python 3 and an OpenBeach release archive (Windows or Linux) that contains
`obl_catalog_sign` and `build_catalog.py` next to the game in `game/`. Nothing has to be built.

## 1. Create the repository
Copy the [catalog template](../examples/catalog_template/index.md) into a new repository: `mods/` with one directory per mod
and `.github/workflows/catalog.yml`. Set `CATALOG_TITLE` at the top of the workflow. In **Settings → Pages** choose
**GitHub Actions** as the source. The catalog address is `https://<owner>.github.io/<repository>/catalog.json`, the
`CATALOG_URL` of the workflow (change it for a custom domain or a user site).

List `l0` among each mod's `packs` (or use `"pack": "l0"`): the workflow checks mods against the L0 pack, which needs no game
data. A mod only for the converted `base` pack cannot be checked there; check it on your machine with `--check-mods`.

## 2. Make and back up the key
The signing key is 32 random bytes written as 64 hex characters. Whoever has it can publish to your players.
```
python -c "import secrets; print(secrets.token_hex(32))" > catalog.key
game/obl_catalog_sign pubkey --key-file catalog.key
```
`pubkey` prints the public key players pin; note it down. Keep **two** copies of `catalog.key` outside the repository (a
password manager and an offline copy) and never commit it. Then add the file's text as the repository secret
`CATALOG_SIGNING_KEY` (**Settings → Secrets and variables → Actions**). Without that secret the workflow only checks.

## 3. Build locally (optional)
```
python game/build_catalog.py --mods-dir mods --out-dir site --key-file catalog.key \
  --title "My OpenBeach mods" --url https://<owner>.github.io/<repository>/catalog.json \
  --check-mods --game-dir game/l0 --pack l0
```
It runs the engine's mod check on each mod, packs `<id>-<mod_version>.zip` deterministically, fills `size`, `sha256` and
`license` (from `mod.json`, or `--license`), signs `catalog.json` and verifies it with the engine's parser. The ZIPs and
the catalog land in `site/`. `--title` and `--url` are required for any catalog but the official one; add
`--ephemeral-key` instead of `--key-file` to check without the real key.

## 4. Publish
Push to `main`. The workflow downloads the OpenBeach release named by `OPENBEACH_RELEASE` (the latest when empty), checks,
signs and deploys `site/` to Pages. Pull requests and pushes elsewhere only check.

## 5. Add it in the game and test the browser build
In **Mods → Catalogs → Add catalog** paste the address and confirm the notice. The game pins the key (trust on first use).
The browser build reads the catalog and every ZIP with `fetch()`, so both need CORS headers; GitHub Pages sends
`Access-Control-Allow-Origin: *`. Check another host with:
```
curl -sI -H "Origin: https://example.org" https://<owner>.github.io/<repository>/catalog.json | grep -i access-control
```
Both addresses must be HTTPS, with no redirect to HTTP.

## Releasing a new mod version
Raise `mod_version` in the mod's `mod.json` and push. The ZIP is named after the version, so it gets a new URL, the entry
changes and the catalog is signed again; players see **Update** after **Refresh**. A catalog lists one version per mod id.
The saves of a player record each active world mod's id, version and content hash: a save written with the old version
asks before loading under the new one. The template publishes only the current ZIPs; to keep older ones downloadable, copy
them into `site/` in the workflow before the Pages step, and put what changed in your mod's description for players.

## A lost or leaked key
- **Lost** (no copy): you cannot sign as that catalog any more. Make a new key and publish at a new address. Players add it
  as a new catalog and remove the old one.
- **Leaked**: whoever holds the key can sign a catalog that points players to any ZIP. Replace the secret and sign with a new
  key, and tell players where the change was announced. Players of the old address see the game's key-change warning on
  refresh and refuse unless they confirm the new key; a catalog at a **new** address with the new key is cleaner, since
  nobody has to judge a warning. Delete the old repository's Pages site once the move is announced.
- **Moving on purpose**: sign with the new key and tell players first; they confirm the key-change warning once.
