#!/usr/bin/env python3
"""Verify own button captions and that gameplay replaces the entire loading-screen bottom strip."""
import sys
from pathlib import Path

from PIL import Image


def check(run: Path) -> None:
    # Compare the actual four-state art, not merely whether a fallback sprite accepted a click.
    pack = run / "packs" / "l0"
    for capture, buttons in (
        ("l0_menu_labels", [(310, 314, "start"), (300, 363, "sandbox"), (290, 412, "load"),
                            (280, 461, "extras"), (270, 510, "exit")]),
        ("l0_scenario_labels", [(168, 408, "island_1"), (20, 534, "menu"), (598, 534, "play")]),
    ):
        screen = Image.open(run / "captures" / f"{capture}.png").convert("RGB")
        for x, y, name in buttons:
            sprite = Image.open(pack / "sprites" / f"l0_menu_{name}" / "sheet.png")
            # Caption pixels are brighter than the background in every state, through RGB565 rounding.
            caption = sprite.crop((0, 0, 180, 40)).getchannel("L")
            bright = [screen.getpixel((x + px, y + py)) for py in range(13, 27) for px in range(180)
                      if caption.getpixel((px, py)) == 255]
            background = screen.getpixel((x + 3, y + 3))
            assert bright and min(map(sum, bright)) > sum(background) + 100, f"missing caption: {name}"
    world = Image.open(run / "captures" / "l0_menu_world.png").convert("RGB")
    # These rows were excluded from world rendering and retained the scenario buttons/loading gradient.
    # Outside the HUD controls, the opaque own bar must paint every interior row identically.
    reference = world.getpixel((650, 525))
    for x in (650, 740, 790):
        for y in range(513, 599):
            assert world.getpixel((x, y)) == reference, f"stale loading pixels at {x},{y}"
    assert reference != (0, 0, 0), "HUD strip was not painted"
    print("PASS L0 menu captions and loading-screen replacement")


if __name__ == "__main__":
    check(Path(sys.argv[1]))
