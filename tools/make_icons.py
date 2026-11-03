#!/usr/bin/env python3
"""Renders the program icons from the OpenBeach mark (assets/logo/openbeach-mark.svg), docs/build/releases.md "Icons".

    python tools/make_icons.py      (needs: pip install resvg-py pillow)

Writes assets/logo/openbeach.ico (16 to 256 px, uncompressed 32-bit bitmaps: the Windows resource compiler takes them, and
so does the game's own decoder, core/cursor_file.h DecodeIcon, which reads no PNG entries) and assets/logo/openbeach-256.png
(the Linux desktop icon). Each size is rendered from the vector, not scaled down from the largest one, so small icons stay
sharp. Run it again after a change of the mark and commit the results.
"""

import io
import struct
from pathlib import Path

import resvg_py
from PIL import Image

LOGO = Path(__file__).resolve().parents[1] / "assets" / "logo"
SIZES = (16, 24, 32, 48, 64, 128, 256)


def render(size):
    png = bytes(resvg_py.svg_to_bytes(svg_path=str(LOGO / "openbeach-mark.svg"), width=size, height=size))
    return Image.open(io.BytesIO(png)).convert("RGBA")


def bitmap_entry(image):
    # BITMAPINFOHEADER with the doubled height of an icon, BGRA rows bottom-up, then the AND mask (1 = transparent).
    # Pillow writes no AND mask for 32-bit entries, which DecodeIcon (rightly) refuses.
    size = image.width
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = image.tobytes("raw", "BGRA")
    stride = size * 4
    xor = b"".join(pixels[y * stride:(y + 1) * stride] for y in reversed(range(size)))
    mask_stride = ((size + 31) // 32) * 4
    alpha = image.getchannel("A").tobytes()
    rows = []
    for y in reversed(range(size)):
        row = bytearray(mask_stride)
        for x in range(size):
            if alpha[y * size + x] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        rows.append(bytes(row))
    return header + xor + b"".join(rows)


def write_ico(path, images):
    entries = [bitmap_entry(image) for image in images]
    out = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    for image, data in zip(images, entries):
        side = image.width % 256  # 256 is written as 0
        out += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    path.write_bytes(out + b"".join(entries))


def main():
    images = [render(size) for size in SIZES]
    write_ico(LOGO / "openbeach.ico", images)
    images[-1].save(LOGO / "openbeach-256.png", optimize=True)
    print(f"wrote {LOGO / 'openbeach.ico'} and {LOGO / 'openbeach-256.png'}")


if __name__ == "__main__":
    main()
