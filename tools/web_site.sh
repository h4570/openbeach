#!/usr/bin/env bash
# Assembles the browser game's static site (docs/architecture/browser-build.md, "Site") from the Emscripten builds:
#   tools/web_site.sh <browser bin> <importer bin> <site directory>
# <browser bin> holds OpenBeach.js/.wasm (browser_files.js is compiled in as its pre-js), <importer bin>
# obl-import-web.js/.wasm and import_worker.js; web/ adds the page. The directory is emptied first and holds only our
# code: no game data. tools/wasm_linux.sh runs it after both builds and checks the result with web/site_check.cjs.
set -euo pipefail

root="${OBL_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
browser="$1"
importer="$2"
site="$3"

rm -rf "$site"
mkdir -p "$site"
cp "$browser/OpenBeach.js" "$browser/OpenBeach.wasm" "$site/"
cp "$importer/obl-import-web.js" "$importer/obl-import-web.wasm" "$importer/import_worker.js" "$importer/language_ini.js" "$site/"
cp "$root/web/index.html" "$root/web/site.js" "$root/web/site_text.js" "$root/assets/logo/openbeach-logo.svg" "$site/"
cp -r "$root/assets/screenshots" "$site/"
# GitHub Pages serves the files as they are, without a Jekyll pass.
touch "$site/.nojekyll"
du -sh "$site"
