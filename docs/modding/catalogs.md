# Mod catalogs

A mod catalog is a signed `catalog.json` file hosted at an HTTPS address. OpenBeach ships with no built-in catalog:
players add the HTTPS addresses of catalogs they trust from the [Mods screen](players/mods-screen.md#installing-from-catalogs),
and the game downloads, verifies and installs mods through the same [ZIP installer](players/mods-screen.md#installing-and-removing-mods)
used for local archives.

## Catalog format (`obl.mod_catalog`)

A catalog is a UTF-8 JSON object (at most 4 MiB, at most 2,048 entries):

```json
{
  "format": "obl.mod_catalog",
  "version": 1,
  "title": "Example Catalog",
  "public_key": "<64 lowercase hex chars: 32-byte Ed25519 public key>",
  "signature": "<128 lowercase hex chars: 64-byte Ed25519 signature>",
  "mods": [
    {
      "id": "frame_counter",
      "title": "Frame counter",
      "mod_version": "1.0.0",
      "packs": ["base", "l0"],
      "scope": "presentation",
      "api": 1,
      "engine_min": "1.13.0",
      "engine_max": "2.0.0",
      "requires": [],
      "url": "frame_counter-1.0.0.zip",
      "sha256": "<64 lowercase hex chars of the ZIP bytes>",
      "content_hash": "<optional 64 lowercase hex chars: ModContentHash>",
      "size": 4210,
      "license": "MIT"
    }
  ]
}
```

### Top-level fields

| Field | Type | Description |
|---|---|---|
| `format` | string | Must be `"obl.mod_catalog"`. |
| `version` | integer | Must be `1`. |
| `title` | string | Non-empty display name shown in the Mods screen. |
| `public_key` | string | 64 lowercase hex characters: the catalog's 32-byte Ed25519 public key. |
| `signature` | string | 128 lowercase hex characters: the 64-byte Ed25519 signature. |
| `mods` | array | Mod entries, each with a unique `id`. |

### Mod entry fields

| Field | Required | Description |
|---|---|---|
| `id` | yes | Mod directory id (lowercase ASCII letters, digits, `_`, `-`). Must match the ZIP root folder and `mod.json` `id`. |
| `title` | yes | Non-empty display title. |
| `mod_version` | yes | Non-empty version string. |
| `pack` or `packs` | yes | Target pack id or non-empty list of pack ids (`base`, `l0`, ...). Cannot set both. |
| `scope` | no | `"world"` (default) or `"presentation"`. |
| `api` | no | Non-negative presentation script API version (`0` if omitted). Entries requiring a newer API than the engine offers are hidden. |
| `engine_min`, `engine_max` | no | Inclusive `MAJOR.MINOR.PATCH` engine version bounds. Entries outside the running engine's version are filtered out. |
| `requires` | no | Array of required mod ids. Installing a mod installs missing dependencies in topological order first. |
| `url` | yes | `https://` URL of the mod ZIP, or a relative path resolved against `catalog.json` (staying on `https://`). |
| `sha256` | yes | 64 lowercase hex characters: SHA-256 of the ZIP file bytes. Verified before unpacking. |
| `content_hash` | no | 64 lowercase hex characters: the mod's `ModContentHash`. Lets a save with a missing world mod match the exact build in added catalogs. |
| `size` | yes | Exact byte size of the ZIP archive (`1` to `64 MiB`). Verified before unpacking. |
| `license` | yes | Non-empty license name or SPDX identifier (for example `"MIT"`). |

## Signing and key pinning

Catalog indexes are signed with **Ed25519** (RFC 8032):

1. Remove the `"signature"` key from the catalog JSON object (keeping `"public_key"` in place).
2. Serialize the remaining object as compact canonical UTF-8 JSON with lexicographically sorted object keys and no whitespace
   (matching `nlohmann::json::dump()` and Python's `json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)`).
3. Prepend the domain prefix `obl.mod_catalog:v1\n` (ASCII bytes).
4. Sign those bytes with the catalog's 32-byte Ed25519 private seed and write the 64-byte signature as 128 lowercase hex
   characters into `"signature"`.

Use `tools/build_catalog.py` (with `obl_catalog_sign`, built from `src/catalog_tool/`) to package mod directories into
deterministic ZIP archives, fill `size` and `sha256`, sign `catalog.json` with the engine's own Ed25519 implementation,
and verify the result with the engine's parser. The private key is read from an environment variable
(`OBL_CATALOG_PRIVATE_KEY` by default) or `--key-file` and is never stored in the repository.

When a player adds a catalog, OpenBeach pins its `public_key` in `mod-catalogs.json` (trust on first use). On every refresh,
the signature is verified against the downloaded `public_key`, and that key is compared against the pinned key. If the key
changes, the game refuses the update and warns the player, requiring explicit confirmation before trusting the new key.

## Hosting requirements

- **HTTPS only:** Both `catalog.json` and every mod ZIP must be served over HTTPS. HTTP and HTTPS-to-HTTP redirects are refused.
- **CORS for the browser build:** Static hosts serving the web build must send `Access-Control-Allow-Origin: *` (or the site's
  origin) on both `catalog.json` and every `.zip` file so `fetch()` can read them. GitHub Pages sends permissive CORS headers
  by default.
- **Validation before publishing:** Run `OpenBeach --check-mods <mod-dir>` (or `tools/check_mod.py`) on each mod before
  packaging its ZIP so broken manifests, scope violations or invalid recipes are caught before players download them.
