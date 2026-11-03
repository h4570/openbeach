#!/usr/bin/env bash
# Imports the player's own copy of Beach Life into userdata/ of a Linux release (docs/build/releases.md): the converted
# pack, the window icon and cursors, and the game language in BeachLife.ini.
#   ./import.sh [<installation, CD, extracted CD or .iso>] [en|fr|it|de|es|pl]
# Asks for the copy when none is given, and for the language when the copy does not name one (a CD). Exit code 1 on failure.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
userdata="$here/userdata"
pack="$userdata/packs/base"
# The import writes beside the pack and replaces it only when it succeeded, so a failed one keeps the game playable.
fresh="$userdata/packs/base.new"
source_dir="${1:-}"
language="${2:-}"
# The game numbers its languages like BeachLife.ini's [Language] Language=<index>; the converter names them by these codes.
languages=(en fr it de es pl)
loop=""
mountpoint=""
output="$(mktemp)"

fail() {
    echo
    echo "ERROR: $1" >&2
    exit 1
}
cleanup() {
    rm -f "$output"
    # Gone after a successful import (it became the pack); a failed one leaves no half-written pack behind.
    rm -rf "$fresh"
    if [[ -n "$mountpoint" ]]; then udisksctl unmount -b "$loop" >/dev/null 2>&1 || true; fi
    if [[ -n "$loop" ]]; then udisksctl loop-delete -b "$loop" >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT

# Runs obl-convert, shows its output and keeps it for the error check.
convert() {
    "$here/obl-convert" "$@" 2>&1 | tee "$output"
    return "${PIPESTATUS[0]}"
}

# Sets [Language] Language=<index> and keeps every other setting; the game reads nothing from an INI without
# [Beach Life Options] Version=1, so a new one gets it.
set_language() {
    local index=-1 i ini="$userdata/BeachLife.ini"
    for i in "${!languages[@]}"; do [[ "${languages[$i]}" == "$1" ]] && index=$i; done
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
    echo "Type or paste the path of your Beach Life folder, your mounted CD or your .iso file, then press Enter."
    read -r -p "Copy: " source_dir
fi
# A path pasted from a file manager can come in quotes.
source_dir="${source_dir#\'}"; source_dir="${source_dir%\'}"; source_dir="${source_dir#\"}"; source_dir="${source_dir%\"}"
[[ -n "$source_dir" ]] || fail "No copy given."
[[ -e "$source_dir" ]] || fail "Not found: $source_dir"

if [[ -f "$source_dir" && "${source_dir,,}" == *.iso ]]; then
    # udisks mounts a disc image for the desktop user without sudo, like opening it in the file manager.
    echo "Opening the disc image $source_dir ..."
    command -v udisksctl >/dev/null ||
        fail "udisksctl is missing. Open the .iso file in your file manager (or: sudo mount -o loop,ro <file.iso> /mnt) and pass the mounted folder."
    loop=$(udisksctl loop-setup -r -f "$source_dir" 2>&1 | grep -oE '/dev/loop[0-9]+' | head -1)
    [[ -n "$loop" ]] || fail "Cannot open the disc image. Open it in your file manager and pass the mounted folder."
    sleep 1 # the desktop may mount the new device by itself
    mountpoint=$(findmnt -n -o TARGET "$loop" 2>/dev/null | head -1)
    if [[ -z "$mountpoint" ]]; then
        mountpoint=$(udisksctl mount -b "$loop" 2>&1 | sed -n 's/^Mounted .* at \(.*\)$/\1/p' | sed 's/\.$//')
    fi
    [[ -n "$mountpoint" && -d "$mountpoint" ]] || fail "Cannot mount the disc image. Open it in your file manager and pass the mounted folder."
    source_dir="$mountpoint"
    echo "The disc image is mounted at $source_dir"
fi
[[ -d "$source_dir" ]] || fail "Not a folder or an .iso file: $source_dir"

echo
echo "Importing $source_dir into $userdata"
echo "This takes about 15 minutes and happens only once. Your copy is only read, never changed."
echo
# A new import replaces the old pack whole, so no file of another copy stays behind; saves and settings stay.
rm -rf "$fresh"
mkdir -p "$fresh" "$userdata/remaster"
if [[ -n "$language" ]]; then convert import "$source_dir" "$fresh" --language "$language"; else convert import "$source_dir" "$fresh"; fi
code=$?
# A CD names no language (an installation does, in its BeachLife.ini): ask which of the copy's languages to play.
if [[ $code -ne 0 && -z "$language" ]] && grep -q "Language" "$output"; then
    echo
    echo "Your copy does not say which language to use. Type one your copy has:"
    echo "  en English (UK/US and the later English CD)"
    echo "  fr French, de German, it Italian, pl Polish, es Spanish (European and French/German CD)"
    read -r -p "Language: " language
    language="${language,,}"
    [[ " ${languages[*]} " == *" $language "* ]] || fail "Unknown language: $language"
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
convert resources "$source_dir" "$userdata/remaster" || fail "Reading the window icon and the cursors failed (the message is above)."

# The pack's own language, so the game starts in a language the copy has.
chosen=$(sed -n 's/^  "language": *"\([a-z]*\)".*/\1/p' "$pack/pack.json" | head -1)
set_language "$chosen"
echo
echo "Done. Start the game with ./play.sh (language: $chosen)."
