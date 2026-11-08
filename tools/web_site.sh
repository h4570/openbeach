#!/usr/bin/env bash
# Assembles the browser game's static site (docs/architecture/browser-build.md, "Site") from the Emscripten builds:
#   tools/web_site.sh <browser bin> <importer bin> <site directory>
# <browser bin> holds OpenBeach.js/.wasm (browser_files.js is compiled in as its pre-js), <importer bin>
# obl-import-web.js/.wasm, import_worker.js, language_ini.js and shard.js; web/ adds the pages (site_pages.cjs renders them). The directory is emptied
# first and holds only our code: no game data. tools/wasm_linux.sh runs it after both builds and checks the result with web/site_check.cjs.
set -euo pipefail

root="${OBL_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
browser="$1"
importer="$2"
site="$3"

rm -rf "$site"
mkdir -p "$site"
cp "$browser/OpenBeach.js" "$browser/OpenBeach.wasm" "$site/"
cp "$importer/obl-import-web.js" "$importer/obl-import-web.wasm" "$importer/import_worker.js" "$importer/language_ini.js" "$importer/shard.js" "$site/"
cp "$root/web/site.js" "$root/web/site_text.js" "$root/assets/logo/openbeach-logo.svg" "$site/"
cp "$root/web/robots.txt" "$root/assets/logo/favicon.ico" "$root/assets/logo/apple-touch-icon.png" "$site/"
cp "$root/assets/logo/openbeach-mark.svg" "$site/favicon.svg"
cp -r "$root/assets/screenshots" "$site/"
# The pages (every language, the Windows 11 guide) and the sitemap are rendered from web/index.html and the text files.
node "$root/web/site_pages.cjs" "$site"
# GitHub Pages serves the files as they are, without a Jekyll pass.
touch "$site/.nojekyll"
du -sh "$site"
