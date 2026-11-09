#!/bin/bash
# Imports the player's own copy of Beach Life for OpenBeach.app (docs/build/releases.md, "macOS"): the converted pack,
# the cursors and the game language in ~/Library/Application Support/OpenBeach/userdata. The app's launcher runs it in
# Terminal; it works from a terminal too:
#   OpenBeach.app/Contents/Resources/import.sh [--prefer-language <code>] [<installation, CD, extracted CD or .iso>] [<code>]
# <code> is en|fr|it|de|es|pl. Asks for the copy when none is given, and for the language when the copy does not name
# one (a CD) and --prefer-language does not either. Exit code 1 on failure. macOS ships bash 3.2: no ${var,,}.
set -uo pipefail

resources="$(cd "$(dirname "$0")" && pwd)"
convert_bin="$resources/../MacOS/obl-convert"
userdata="$HOME/Library/Application Support/OpenBeach/userdata"
pack="$userdata/packs/base"
# The import writes beside the pack and replaces it only when it succeeded, so a failed one keeps the game playable.
fresh="$userdata/packs/base.new"
source_dir=""
language=""
prefer=""
# The game numbers its languages like BeachLife.ini's [Language] Language=<index>; the converter names them by these codes.
languages="en fr it de es pl"
mountpoint=""
output="$(mktemp)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefer-language) prefer="${2:-}"; shift 2 ;;
        *) if [[ -z "$source_dir" ]]; then source_dir="$1"; else language="$1"; fi; shift ;;
    esac
done

fail() {
    echo
    echo "ERROR: $1" >&2
    exit 1
}
cleanup() {
    rm -f "$output"
    # Gone after a successful import (it became the pack); a failed one leaves no half-written pack behind.
    rm -rf "$fresh"
    if [[ -n "$mountpoint" ]]; then
        hdiutil detach -quiet "$mountpoint" >/dev/null 2>&1 || hdiutil detach -quiet -force "$mountpoint" >/dev/null 2>&1
        rmdir "$mountpoint" 2>/dev/null
    fi
}
trap cleanup EXIT

lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

# Runs obl-convert, shows its output and keeps it for the error check.
convert() {
    "$convert_bin" "$@" 2>&1 | tee "$output"
    return "${PIPESTATUS[0]}"
}

# Sets [Language] Language=<index> and keeps every other setting; the game reads nothing from an INI without
# [Beach Life Options] Version=1, so a new one gets it.
set_language() {
    local index=-1 i=0 code ini="$userdata/BeachLife.ini"
    for code in $languages; do
        [[ "$code" == "$1" ]] && index=$i
        i=$((i + 1))
    done
    [[ $index -ge 0 ]] || return 0
    if [[ ! -f "$ini" ]]; then
        printf '[Beach Life Options]\nVersion=1\n[Language]\nLanguage=%d\n' "$index" > "$ini"
        return 0
    fi
    awk -v n="$index" '
        { sub(/\r$/, "") }
        /^\[/ { if (section && !done) { print "Language=" n; done = 1 } section = tolower($0) == "[language]" }
        section && tolower($0) ~ /^[ \t]*language[ \t]*=/ { if (!done) print "Language=" n; done = 1; next }
        { print }
        END { if (!done) { if (!section) print "[Language]"; print "Language=" n } }' "$ini" > "$ini.new" && mv "$ini.new" "$ini"
}

echo "OpenBeach: import your own copy of Beach Life"
echo
if [[ -z "$source_dir" ]]; then
    echo "Drag your Beach Life folder, your CD or your .iso file into this window (or type its path), then press Enter."
    read -r -p "Copy: " source_dir
fi
# A path dragged into Terminal ends with a space and escapes spaces with backslashes; a pasted one can come in quotes.
source_dir="${source_dir%"${source_dir##*[![:space:]]}"}"
source_dir="${source_dir#\'}"; source_dir="${source_dir%\'}"; source_dir="${source_dir#\"}"; source_dir="${source_dir%\"}"
[[ -e "$source_dir" ]] || source_dir="$(printf '%s' "$source_dir" | sed 's/\\\(.\)/\1/g')"
[[ -n "$source_dir" ]] || fail "No copy given."
[[ -e "$source_dir" ]] || fail "Not found: $source_dir"

case "$(lower "$source_dir")" in
    *.iso | *.cdr | *.img | *.dmg)
        if [[ -f "$source_dir" ]]; then
            # Mounted read-only, without opening a Finder window, and detached again when the script ends.
            echo "Opening the disc image $source_dir ..."
            mountpoint="$(mktemp -d "${TMPDIR:-/tmp}/openbeach-cd.XXXXXX")"
            if ! hdiutil attach -readonly -nobrowse -noautoopen -mountpoint "$mountpoint" "$source_dir" >/dev/null; then
                rmdir "$mountpoint" 2>/dev/null
                mountpoint=""
                fail "Cannot open the disc image. Double-click it in Finder and import the disc that appears instead."
            fi
            source_dir="$mountpoint"
            echo "The disc image is mounted at $source_dir"
        fi
        ;;
esac
[[ -d "$source_dir" ]] || fail "Not a folder or a disc image: $source_dir"

echo
echo "Importing $source_dir into $userdata"
echo "This takes about 15 minutes and happens only once. Your copy is only read, never changed."
echo
# A new import replaces the old pack whole, so no file of another copy stays behind; saves and settings stay.
rm -rf "$fresh"
mkdir -p "$fresh" "$userdata/remaster" || fail "Cannot write to $userdata."
if [[ -n "$language" ]]; then
    convert import "$source_dir" "$fresh" --language "$language"
elif [[ -n "$prefer" ]]; then
    convert import "$source_dir" "$fresh" --prefer-language "$prefer"
else
    convert import "$source_dir" "$fresh"
fi
code=$?
# A CD names no language (an installation does, in its BeachLife.ini): ask which of the copy's languages to play.
if [[ $code -ne 0 && -z "$language" && -z "$prefer" ]] && grep -q "Language" "$output"; then
    echo
    echo "Your copy does not say which language to use. Type one your copy has:"
    echo "  en English (UK/US and the later English CD)"
    echo "  fr French, de German, it Italian, pl Polish, es Spanish (European and French/German CD)"
    read -r -p "Language: " language
    language="$(lower "$language")"
    [[ " $languages " == *" $language "* ]] || fail "Unknown language: $language"
    rm -rf "$fresh"
    mkdir -p "$fresh"
    convert import "$source_dir" "$fresh" --language "$language"
    code=$?
fi
if [[ $code -ne 0 ]]; then
    rm -rf "$fresh"
    fail "The import failed (the message is above). Nothing of your copy was changed."
fi
rm -rf "$pack"
mv "$fresh" "$pack"
convert resources "$source_dir" "$userdata/remaster" || fail "Reading the cursors failed (the message is above)."

# The pack's own language, so the game starts in a language the copy has.
chosen=$(sed -n 's/^  "language": *"\([a-z]*\)".*/\1/p' "$pack/pack.json" | head -1)
set_language "$chosen"
echo
echo "Done. Start OpenBeach from Applications or Launchpad (language: $chosen)."
