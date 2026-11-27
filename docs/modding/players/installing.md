# Install and find your mods

Open **Mods** from the main menu, then choose **+ Install from file** under the Installed list and select the ZIP. On desktop and in the browser you
can also drop the ZIP onto the open Mods screen. On Android, use the system file picker.

To install from a catalog on any platform, choose **Mods → Sources → + Add**, enter its HTTPS address and confirm
the notice. On **Browse**, select an entry and choose **Install**. Then switch the mod on from the Mods screen. See
[the Mods screen](mods-screen.md) for updates, dependencies and conflicts.

The installed mod lives in `mods/<id>/` next to `openbeach.ini` in the game directory.

| Platform | Game directory |
|---|---|
| Windows and Linux | `userdata/` for a release or the directory passed with `--game-dir`. The demo has its own `l0/` directory. |
| macOS | `~/Library/Application Support/OpenBeach/userdata/` for the installed pack, or `l0/` beside it for the demo. |
| Browser | The site's private browser storage (OPFS), managed from the Mods screen. |
| Android | The app's `files/userdata/` for the imported pack. Use the Mods screen for everyday changes. |

If the game stops before the menu, see [switching mods](switching-mods.md#a-mod-that-stops-the-start-up). For full
game-directory details, see the [release layouts](../../build/releases.md).
