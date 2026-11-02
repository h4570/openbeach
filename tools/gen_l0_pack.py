#!/usr/bin/env python3
"""Generates the own L0 pack content (data/packs/l0/, docs maps-scenarios.md "The L0 scenario").

Everything is drawn or computed here without reading the game or a converted pack. The keys the engine looks up by name
and a few format constants are the engine's own, so such small files equal their counterparts in an imported pack.
    python tools/gen_l0_pack.py [--out data/packs/l0]
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

from PIL import Image, ImageDraw

SCREENS = ["completion0", "completion1", "completion2", "completion3", "completion4", "completion5", "completion6",
           "completion7", "completion8", "credits", "deepred", "extrascreen", "frontscreen", "loading_1", "loading_2",
           "mainmenu", "scenarioscreen"]


# Filenames are pack choices; the frozen diagnostic drawing labels remain unchanged.
SCREEN_FILES = {
    'mainmenu': 'l0_welcome',
    'scenarioscreen': 'l0_islands',
    'extrascreen': 'l0_options',
    'credits': 'l0_acknowledgements',
    'deepred': 'l0_opening',
    'frontscreen': 'l0_title',
}


def gray_palette() -> list[int]:
    return [v for i in range(256) for v in (i, i, i)]


def indexed(size: tuple[int, int], fill: int) -> Image.Image:
    im = Image.new("P", size, fill)
    im.putpalette(gray_palette())
    return im


def write_images(out: Path) -> None:
    images = out / "images"
    (images / "screens").mkdir(parents=True, exist_ok=True)
    (images / "haze").mkdir(parents=True, exist_ok=True)
    # Cursor atlas: 32 x 32 cells; one plain arrow shape in the first cell.
    cursor = indexed((256, 256), 0)
    d = ImageDraw.Draw(cursor)
    d.polygon([(1, 1), (1, 20), (6, 15), (12, 24), (15, 22), (9, 13), (16, 13)], fill=255, outline=128)
    cursor.save(images / "cursor.png")
    for n in range(6):
        haze = indexed((32, 24), 0)
        d = ImageDraw.Draw(haze)
        for y in range(24):
            for x in range(32):
                d.point((x, y), fill=(x * 3 + y * 5 + n * 17) % 2)
        haze.save(images / "haze" / f"{n}.png")
    for name in SCREENS:
        shade = 40 + (sum(map(ord, name)) * 7) % 120
        screen = indexed((800, 600), shade)
        d = ImageDraw.Draw(screen)
        d.rectangle([40, 40, 759, 559], outline=255)
        d.text((60, 60), f"OpenBeach L0 {name}", fill=255)
        screen.save(images / "screens" / f"{SCREEN_FILES.get(name, name)}.png")


def write_palette(path: Path, colors: int, base: tuple[int, int, int]) -> None:
    """One palette row: entry 0 is transparent black, the rest ramps from dark to `base`."""
    im = Image.new("RGB", (colors, 1))
    for i in range(colors):
        k = i / (colors - 1)
        im.putpixel((i, 0), tuple(int(c * (0.3 + 0.7 * k)) for c in base) if i else (0, 0, 0))
    im.save(path)


def sheet(path: Path, cols: int, rows: int, size: tuple[int, int], draw) -> None:
    """Gray + alpha sheet: gray is the palette index, alpha 255 where a pixel is set."""
    w, h = size
    im = Image.new("LA", (cols * w, rows * h), (0, 0))
    for i in range(cols * rows):
        cell = Image.new("LA", size, (0, 0))
        draw(ImageDraw.Draw(cell), i, w, h)
        im.paste(cell, ((i % cols) * w, (i // cols) * h))
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)


def write_json(path: Path, doc: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:  # LF on every platform: pack files are read byte for byte
        f.write(json.dumps(doc, indent=2) + "\n")


def write_sprites(out: Path) -> None:
    sprites = out / "sprites"
    sprites.mkdir(parents=True, exist_ok=True)
    # dummy: the fallback of every missing sprite name (two animations like the engine's own fallback layout).
    write_palette(sprites / "dummy.palettes.png", 256, (255, 0, 255))

    def figure(d, i, w, h):
        d.rectangle([w // 2 - 8, 10, w // 2 + 8, h - 2], fill=(200 - 60 * (i % 2), 255))
        d.ellipse([w // 2 - 6, 1, w // 2 + 6, 13], fill=(255, 255))

    sheet(sprites / "dummy/dummy.png", 2, 1, (40, 78), figure)
    sheet(sprites / "dummy/dummy_1.png", 2, 1, (40, 78), figure)
    anim = {"size": [40, 78], "hotspot": [-18, -77], "columns": 2, "unique_frames": 2}
    write_json(sprites / "dummy.json", {
        "format": "obl.sprite", "version": 1, "name": "dummy", "kind": "special", "pixels": "indexed",
        "palettes": "dummy.palettes.png", "palette_colors": 256, "palette_count": 1,
        "animations": [
            {"name": "dummy", "directions": 2, "frames": 1, "sheets": ["dummy/dummy.png"], **anim},
            {"name": "dummy", "directions": 1, "frames": 2, "sheets": ["dummy/dummy_1.png"], **anim}]})

    # pavement: 16 tiles chosen by which of N, E, S, W continue the path (bits 0, 2, 4, 6 of the neighbor mask).
    write_palette(sprites / "pathtile_01.palettes.png", 64, (190, 170, 120))

    def tile(d, i, w, h):
        cx, cy = w // 2, h // 2
        d.polygon([(cx, 0), (w - 1, cy), (cx, h - 1), (0, cy)], fill=(40, 255))
        for bit, (x, y) in enumerate([(cx, 2), (w - 8, cy), (cx, h - 4), (6, cy)]):
            if i & (1 << bit): d.rectangle([x - 3, y - 2, x + 3, y + 2], fill=(63, 255))

    sheet(sprites / "pathtile_01/set1.png", 16, 1, (62, 33), tile)
    masks = [((m & 1) | ((m >> 2) & 1) << 1 | ((m >> 4) & 1) << 2 | ((m >> 6) & 1) << 3) for m in range(256)]
    write_json(sprites / "pathtile_01.json", {
        "format": "obl.sprite", "version": 1, "name": "pathtile_01", "kind": "character", "pixels": "indexed",
        "palettes": "pathtile_01.palettes.png", "palette_colors": 64, "palette_count": 1,
        "animations": [{"name": "set1", "directions": 1, "frames": 16, "size": [62, 33], "hotspot": [-31, -32],
                        "sheets": ["pathtile_01/set1.png"], "columns": 16, "unique_frames": 16}],
        "autotile": {"masks": masks, "full": 15}})


def write_wav(path: Path, hz: int, seconds: float) -> None:
    """A short 16-bit mono sine tone with a linear fade out."""
    rate = 22050
    count = int(rate * seconds)
    frames = b"".join(
        struct.pack("<h", int(12000 * math.sin(2 * math.pi * hz * i / rate) * (1 - i / count))) for i in range(count))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(frames)


def write_sounds(out: Path) -> None:
    sounds = out / "sounds"
    sounds.mkdir(parents=True, exist_ok=True)
    for name, hz, seconds in [("beep", 660, 0.2), ("splash", 220, 0.3), ("surf", 110, 0.5)]:
        write_wav(sounds / f"{name}.wav", hz, seconds)
    write_json(sounds / "sound_script.json", {
        "format": "obl.sound_script", "version": 2,
        "crowd": {"max_number": 20.0, "max_volume": 1.0, "fade_time": 0.3},
        "groups": {"effects": {"limit": 2}},
        "sprite_sounds": {"dummy": {"dummy": {"sounds": [{"sample": "beep", "volume": 1.0}]}}}, "building_sounds": {}, "events": {}})


def write_media(out: Path) -> None:
    """Own silent soundtrack and a one-second blue intro; requires ffmpeg on PATH."""
    music = out / "music"
    video = out / "video"
    music.mkdir(parents=True, exist_ok=True)
    video.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        silent = Path(temp) / "silence.mp3"
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                        "-f", "lavfi", "-i", "anullsrc=r=44100:cl=stereo", "-t", "4",
                        "-c:a", "libmp3lame", "-b:a", "32k", "-map_metadata", "-1", str(silent)], check=True)
        # The built-in playlist and the separate night stream still use these content keys.
        for period in ("day", "night"):
            for number in range(1, 7):
                (music / f"{period}{number}.mp3").write_bytes(silent.read_bytes())
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i", "color=c=0x204060:s=320x240:r=25", "-t", "1",
                    "-c:v", "mpeg1video", "-pix_fmt", "yuv420p", "-an", "-map_metadata", "-1",
                    "-f", "mpeg", str(video / "intro.mpg")], check=True)


def write_palette_modifiers(out: Path) -> None:
    """The engine needs 12 tables: three tone plans of four (base, two scale steps, blend)."""
    sprites = out / "sprites"
    im = Image.new("RGB", (64, 12))
    for t in range(12):
        for i in range(64):
            v = int(255 * i / 63)
            im.putpixel((i, t), (v, v * (4 - t % 4) // 4, v * (4 - t % 4) // 4))
    im.save(sprites / "palette_modifiers.png")
    write_json(sprites / "palette_modifiers.json", {
        "format": "obl.palette_modifiers", "version": 1, "image": "palette_modifiers.png", "colors": 64, "tables": 12})


FONTS = {"guifont7": (7, False), "guifont8": (8, False), "guifont8b": (8, True), "guifont12b": (12, True),
         "guifont14b": (14, True), "photo16": (16, True)}


def write_fonts(out: Path) -> None:
    """Own 5 x 7 capitals, enlarged with nearest-neighbour pixels; no installed font input."""
    alphabet = {
        "A": "01110 10001 10001 11111 10001 10001 10001", "B": "11110 10001 10001 11110 10001 10001 11110",
        "C": "01111 10000 10000 10000 10000 10000 01111", "D": "11110 10001 10001 10001 10001 10001 11110",
        "E": "11111 10000 10000 11110 10000 10000 11111", "F": "11111 10000 10000 11110 10000 10000 10000",
        "G": "01111 10000 10000 10111 10001 10001 01111", "H": "10001 10001 10001 11111 10001 10001 10001",
        "I": "11111 00100 00100 00100 00100 00100 11111", "J": "00111 00010 00010 00010 10010 10010 01100",
        "K": "10001 10010 10100 11000 10100 10010 10001", "L": "10000 10000 10000 10000 10000 10000 11111",
        "M": "10001 11011 10101 10101 10001 10001 10001", "N": "10001 11001 11001 10101 10011 10011 10001",
        "O": "01110 10001 10001 10001 10001 10001 01110", "P": "11110 10001 10001 11110 10000 10000 10000",
        "Q": "01110 10001 10001 10001 10101 10010 01101", "R": "11110 10001 10001 11110 10100 10010 10001",
        "S": "01111 10000 10000 01110 00001 00001 11110", "T": "11111 00100 00100 00100 00100 00100 00100",
        "U": "10001 10001 10001 10001 10001 10001 01110", "V": "10001 10001 10001 10001 10001 01010 00100",
        "W": "10001 10001 10001 10101 10101 10101 01010", "X": "10001 10001 01010 00100 01010 10001 10001",
        "Y": "10001 10001 01010 00100 00100 00100 00100", "Z": "11111 00001 00010 00100 01000 10000 11111",
        "1": "00100 01100 00100 00100 00100 00100 01110", " ": "00000 " * 6 + "00000",
    }
    alphabet.update({
        "0": "01110 10001 10011 10101 11001 10001 01110",
        "2": "01110 10001 00001 00010 00100 01000 11111",
        "3": "11110 00001 00001 01110 00001 00001 11110",
        "4": "00010 00110 01010 10010 11111 00010 00010",
        "5": "11111 10000 10000 11110 00001 00001 11110",
        "6": "01110 10000 10000 11110 10001 10001 01110",
        "7": "11111 00001 00010 00100 01000 01000 01000",
        "8": "01110 10001 10001 01110 10001 10001 01110",
        "9": "01110 10001 10001 01111 00001 00001 01110",
        ".": "00000 00000 00000 00000 00000 00100 00100",
        ",": "00000 00000 00000 00000 00100 00100 01000",
        ":": "00000 00100 00100 00000 00100 00100 00000",
        ";": "00000 00100 00100 00000 00100 00100 01000",
        "!": "00100 00100 00100 00100 00100 00000 00100",
        "?": "01110 10001 00001 00010 00100 00000 00100",
        "-": "00000 00000 00000 11111 00000 00000 00000",
        "_": "00000 00000 00000 00000 00000 00000 11111",
        "+": "00000 00100 00100 11111 00100 00100 00000",
        "=": "00000 00000 11111 00000 11111 00000 00000",
        "/": "00001 00001 00010 00100 01000 10000 10000",
        "\\": "10000 10000 01000 00100 00010 00001 00001",
        "(": "00010 00100 01000 01000 01000 00100 00010",
        ")": "01000 00100 00010 00010 00010 00100 01000",
        "[": "01110 01000 01000 01000 01000 01000 01110",
        "]": "01110 00010 00010 00010 00010 00010 01110",
        "'": "00100 00100 00000 00000 00000 00000 00000",
        '"': "01010 01010 00000 00000 00000 00000 00000",
        "$": "00100 01111 10100 01110 00101 11110 00100",
        "%": "11001 11010 00010 00100 01000 01011 10011",
        "*": "00000 10101 01110 11111 01110 10101 00000",
        "<": "00010 00100 01000 10000 01000 00100 00010",
        ">": "01000 00100 00010 00001 00010 00100 01000",
        "#": "01010 01010 11111 01010 11111 01010 01010",
        "&": "01100 10010 10100 01000 10101 10010 01101",
        "@": "01110 10001 10111 10101 10111 10000 01111",
        "^": "00100 01010 10001 00000 00000 00000 00000",
        "`": "01000 00100 00000 00000 00000 00000 00000",
        "{": "00010 00100 00100 01000 00100 00100 00010",
        "}": "01000 00100 00100 00010 00100 00100 01000",
        "|": "00100 00100 00100 00100 00100 00100 00100",
        "~": "00000 00000 01001 10110 00000 00000 00000",
    })
    fonts = out / "fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    for name, (size, bold) in FONTS.items():
        width = round(5 * size / 7)
        cw, ch = width + int(bold) + 1, size + 2
        cols = 16
        atlas = Image.new("LA", (cols * cw, 16 * ch), (0, 0))
        glyphs = {}
        for code in range(256):
            try:  # Keep every defined byte: ReadFont requires the complete Windows-1252 table.
                char = bytes([code]).decode('cp1252')
            except UnicodeDecodeError:
                continue
            key = f"{ord(char):04x}"
            x, y = (code % cols) * cw, (code // cols) * ch
            if code < 0x20 or code == 0x7f:
                glyphs[key] = [x, y, x, y]
                continue
            # Lowercase uses capitals; unsupported extended characters visibly use the question mark.
            rows = alphabet.get(char.upper(), alphabet[" " if char == "\u00a0" else "?"]).split()
            tile = Image.new("LA", (5, 7), (0, 0))
            for py, row in enumerate(rows):
                for px, bit in enumerate(row):
                    if bit == "1":
                        tile.putpixel((px, py), (1, 255))
            tile = tile.resize((width, size), Image.Resampling.NEAREST)
            if bold:
                atlas.paste(tile, (x + 1, y), tile.getchannel("A"))
            atlas.paste(tile, (x, y), tile.getchannel("A"))
            # Rectangles are exclusive; the blank right column supplies inter-character spacing.
            glyphs[key] = [x, y, x + cw, y + size]
        atlas.save(fonts / f"{name}.png")
        write_json(fonts / f"{name}.json", {"format": "obl.font", "version": 1, "name": name, "size": size,
                                            "bold": bold, "atlas": f"{name}.png", "glyphs": glyphs, "kerning": []})


def write_maps(out: Path) -> None:
    """A small island on a 128 x 128 grid: grass, a sand rim, shallow water, deep sea. Legend: obl.map terrain_types."""
    grass, sand, shallow, deep = 0, 1, 2, 3
    side, cx, cy = 128, 64, 64
    im = Image.new("P", (side, side), deep)
    im.putpalette([v for c in ((90, 170, 60), (230, 210, 140), (90, 200, 220), (20, 60, 140)) for v in c] + [0] * (252 * 3))
    for y in range(side):
        for x in range(side):
            d = math.hypot((x - cx) * 1.0, (y - cy) * 1.2)
            if d < 10:
                im.putpixel((x, y), grass)
            elif d < 15:
                im.putpixel((x, y), sand)
            elif d < 22:
                im.putpixel((x, y), shallow)
    folder = out / "maps" / "scenario1"
    folder.mkdir(parents=True, exist_ok=True)
    im.save(folder / "terrain.png")
    # The pre-rendered island (windowed runs; headless skips both): terrain colours at 32 px per cell, and a flat depth plane.
    im.convert("RGB").resize((4096, 4096), Image.NEAREST).save(folder / "color.png", optimize=True)
    Image.new("I;16", (4096, 4096), 0).save(folder / "depth.png", optimize=True)
    legend = ["grass", "sand", "shallow_water", "deep_sea", "shoreline", "rock", "path"]
    write_json(folder / "map.json", {
        "format": "obl.map", "version": 2, "event_area": [0, 0, 4096, 4096], "size_px": [4096, 4096], "cell_px": 32, "cells": [128, 128],
        "terrain": "terrain.png", "color": "color.png", "depth": "depth.png",
        "terrain_types": [{"index": i, "type": t} for i, t in enumerate(legend)],
        "height_scale": 0.0, "depth_plane_offset": 17, "depth_slant_offset": 0, "water_anim": "water/1",
    })
    # The shimmer: 15 frames of four 128 x 128 on/off masks (gray 255 = on), a sparse own dot pattern that drifts per frame.
    water = Image.new("L", (15 * side, 4 * side), 0)
    for frame in range(15):
        for mask in range(4):
            for k in range(160):
                x = (k * 37 + frame * 5 + mask * 11) % side
                y = (k * 53 + mask * 29 + frame * 3) % side
                water.putpixel((frame * side + x, mask * side + y), 255)
    (out / "maps" / "water").mkdir(parents=True, exist_ok=True)
    water.save(out / "maps" / "water" / "1.png")
    write_json(out / "maps" / "water" / "1.json", {
        "format": "obl.water_anim", "version": 1, "sheet": "1.png", "mask_size": [side, side],
        "frames": 15, "masks_per_frame": 4, "rate": 7.5,
    })


def footprint(w: int, h: int) -> dict:
    """The engine's (h + 2) x (w + 2) cells per layer: a margin rim, buildable interior, one entrance cell."""
    cols, rows = w + 2, h + 2
    rim = [[{"margin": 1} if x in (0, cols - 1) or y in (0, rows - 1) else {"terrain": 1} for x in range(cols)] for y in range(rows)]
    work = [[{} for _ in range(cols)] for _ in range(rows)]
    work[rows - 1][cols // 2] = {"terrain": 1}
    body = [[251658240 if 0 < x < cols - 1 and 0 < y < rows - 1 else 0 for x in range(cols)] for y in range(rows)]
    return {"size": [w, h], "layers": {"placement": rim, "work": work, "depth": [[0] * cols for _ in range(rows)], "body": body}}


def building(key: str, name: str, category: str, behavior: str, size: int, cost: int, attractor: str, commodity: str | None,
             price: float) -> dict:
    return {
        "format": "obl.building", "version": 2, "key": key, "name": name, "category": category, "behavior": behavior,
        "sprite": "dummy", "icon": 0, "cost": {"build": cost, "refund": cost * 3 // 4},
        "construction": {"build_time": 1, "demolish_time": 1, "max_builders": 4},
        "size": [size, size], "build_on": ["land", "beach"], "baseplates": [7],
        "points": {"serve": [0, -64], "staff": [0, 0], "entrance": [0, 0]},
        "operation": {"guest_capacity": 4, "opening_hours": "allhours", "max_instances": 99, "max_queue": 6,
                      "attractor": attractor, "power": 0, "staff": {"types": [], "max": 0}},
        "commodities": [{"commodity": commodity, "intensity": [0.5, 0.5], "daily_intensity_factor": 0.98, "price": price,
                         "daily_price_factor": 0.98, "cost": 0.1}] if commodity else [],
        "failure": {"average_time": 6.0, "percent": 5, "age": 2},
        "footprint": footprint(size, size),
    }


def write_catalog(out: Path) -> None:
    """A small own vocabulary (two commodity families, two desires, two commodities, one opening time) and three buildings."""
    root = out / "catalog"
    # The engine looks these keys up by name (guest desires, building lifecycle), so the vocabulary is complete; names, family
    # assignments and desire groups are our own.
    families = {k: k.capitalize() for k in [
        "none", "accomdtn", "alcohol", "amusemnt", "fight", "firstaid", "flirt", "gengoods", "leave", "meals", "partyfun", "shower",
        "snacks", "softdrnk", "souvenir", "sportfun", "stroll", "sunbathe", "swimfun", "relief", "vandal", "waterfun", "lotion",
        "pills"]}
    by_family = {
        "accomdtn": ["apartmnt"], "amusemnt": ["banrides", "slots"], "alcohol": ["beers"], "meals": ["burgers", "litemeal", "qualmeal"],
        "sportfun": ["climb", "frisbees", "golf", "tennis", "volley", "workout"], "partyfun": ["clubadmt", "dancing", "livevent"],
        "sunbathe": ["deckchr", "parasols", "sunlnger"], "gengoods": ["essntial"], "waterfun": ["fishtrip", "funslide", "jetride",
        "glides", "cruises", "boathire"], "souvenir": ["gifts"], "pills": ["headpills"], "snacks": ["ices"],
        "swimfun": ["lilos", "pedhire", "snorkel", "surfboard", "swimming", "surfhire"], "firstaid": ["medical"],
        "shower": ["soaks", "showers"], "softdrnk": ["sodas"], "lotion": ["sunlotion"], "relief": ["toilet"]}
    commodities = {c: (c.capitalize(), f) for f, cs in by_family.items() for c in cs}
    desires = {
        "hungry": ["meals", "snacks"], "thirsty": ["softdrnk"], "sleepy": ["accomdtn"], "drunk": ["alcohol"], "horny": ["flirt"],
        "bladder": ["relief"], "ill": ["firstaid"], "angry": ["fight"], "scared": ["leave"],
        "activity": ["amusemnt", "sportfun", "swimfun", "waterfun"], "shower": ["shower"], "suntan": ["sunbathe", "lotion"],
        "headache": ["pills"], "souvenir": ["souvenir"]}
    desires = {k: (k.capitalize(), v) for k, v in desires.items()}
    # The 26 guest desire slots in id order (the `max` names of a guest set); the script words are derived from our keys.
    slots = ["accommodation", "alcohol", "amusement", "first_aid", "headache", "meals", "party_fun", "shower", "snacks", "soft_drinks",
             "souvenirs", "sports_fun", "sunbathe", "swim_fun", "toilet", "water_sports_fun", "stroll", "leave", "vandalise", "fight",
             "flirt", "umbrellas", "suntan", "intimacy", "sleep", "scared"]
    hours = {"allhours": (0.0, 0.0), "morning": (6.0, 12.0), "day": (9.0, 18.0), "evening": (17.0, 23.0), "night": (21.0, 4.0)}
    buildings = [building("loo", "Toilet Block", "other", "toilet", 3, 800, "relief", "toilet", 1.0),
                 building("kiosk", "Soda Kiosk", "food", "none", 3, 600, "softdrnk", "sodas", 2.0),
                 building("hut", "Beach Hut", "leisure", "none", 2, 300, "softdrnk", "sodas", 1.5)]
    # The engine looks up the staff homes by name (the staff buttons ask whether each is in the scenario), so they exist in the
    # catalog; the scenario does not offer them yet.
    for key, name in [("baywatch", "Lifeguard Post"), ("handyman", "Handyman Shed"), ("rep", "Rep Office"),
                      ("builderyard", "Builder Yard"), ("cleanershack", "Cleaner Shack"), ("security_post", "Security Post")]:
        buildings.append(building(key, name, "other", "none", 2, 500, "none", None, 0.0))
    for key, name in families.items():
        write_json(root / "commodity_families" / f"{key}.json", {"format": "obl.commodity_family", "version": 2, "key": key, "name": name})
    for key, (name, family) in commodities.items():
        write_json(root / "commodities" / f"{key}.json", {"format": "obl.commodity", "version": 2, "key": key, "name": name, "family": family})
    for key, (name, fams) in desires.items():
        write_json(root / "desires" / f"{key}.json", {"format": "obl.desire", "version": 2, "key": key, "name": name, "families": fams})
    for key in slots:
        write_json(root / "desire_slots" / f"{key}.json", {"format": "obl.desire_slot", "version": 2, "key": key,
                                                           "script_name": key.title().replace("_", "")})
    for key, (opens, closes) in hours.items():
        write_json(root / "opening_hours" / f"{key}.json", {"format": "obl.opening_hours", "version": 2, "key": key, "open": opens, "close": closes})
    for doc in buildings:
        if doc["key"] == "builderyard":
            # Its two builders spawn with it (SpawnInitialStaff) and build every placed site (scripts/object/builder.obs).
            doc["operation"]["staff"] = {"types": ["builder", "builder"], "max": 4}
        if doc["key"] in ("loo", "kiosk", "hut"):
            doc["requires"] = "rep"  # the build tree: these unlock once the rep office stands
        write_json(root / "buildings" / f"{doc['key']}.json", doc)
    write_json(root / "staff" / "builder.json", {
        "format": "obl.staff", "version": 2, "key": "builder", "name": "Builder", "gender": "m", "sprite": "dummy", "script": "BUILDER",
        "menu": "BuilderInfo", "wage": 10, "recommended_wage": 10, "scale": 0.0002, "expectation_growth": 1.0, "initial_happiness": 0.8,
        "hours": [6, 22]})
    write_json(root / "index.json", {
        "format": "obl.catalog_index", "version": 2, "commodity_families": list(families), "commodities": list(commodities),
        "opening_hours": list(hours), "desires": list(desires), "desire_slots": slots, "staff": ["builder"], "events": [], "beers": [], "fines": [],
        "vehicles": ["bus"], "tables": [], "scenery": [], "buildings": [b["key"] for b in buildings],
    })
    # BoatEvent uses BUS on an island without a WHARF.
    write_json(root / "vehicles" / "bus.json", {
        "format": "obl.vehicle", "version": 2, "key": "bus", "name": "Island Shuttle", "sprite": "dummy",
        "size": [32, 32], "capacity": 6, "max_speed": 200, "force": 200, "mass": 1, "move_type": "points",
        "creation_point": [0, 0], "towed": "NONE", "anim_set": "dummy"})


def write_texts(out: Path) -> None:
    # The engine refuses a pack without any language table (text_load::LanguageSelection), so L0 ships its own English one.
    write_json(out / "text" / "en.json", {
        "format": "obl.text", "version": 1, "language": "en",
        "entries": [["L0_TITLE", "OpenBeach L0"], ["L0_DESC", "Finish every offered building and earn 1 from visitors before 17:00 on day 2."],
                    ["L0_WON", "Every building is ready and visitors have bought drinks. Scenario won."],
                    ["L0_LOST", "17:00 on day 2: finish the buildings and serve visitors. Scenario lost."]],
    })


def write_scenario(out: Path) -> None:
    keys = ["loo", "kiosk", "hut"]
    write_json(out / "scenarios" / "s1" / "scenario.json", {
        "format": "obl.scenario", "version": 1, "id": "s1", "kind": "campaign", "title": "L0_TITLE", "description": "L0_DESC",
        "map": "scenario1", "start_view": [2048, 2048], "catalog": "catalog/index.json", "guests": "l0",
        "available_buildings": keys, "tilesets": {"pavement": "pathtile_01"}, "radar": {"image": "dummy", "pos": [9, 0]}, "progress_dialog": "L0Progress", "script": "world.wsc",
    })


def write_world_script(out: Path) -> None:
    """The world script: message ids from our own enum; the functions and values are the engine's script API."""
    import re
    header = (Path(__file__).resolve().parent.parent / "src/game/messages/message_state.h").read_text(encoding="utf-8")
    messages = [(int(i), n) for n, i in re.findall(r"(MT_\w+)\s*=\s*(\d+)", header)]
    assert [i for i, _ in messages] == list(range(108)), "the message enum must hold the 108 world-script messages"
    rows = lambda pairs: [{"id": i, "name": n} for i, n in pairs]
    write_json(out / "scripts" / "world_vocabulary.json", {
        "format": "obl.world_script_vocabulary", "version": 1, "messages": rows(messages)})
    script = "\n".join([
        "#obl-script 1 world",
        "// The own L0 scenario: a small island, three buildings (the kiosk stands from the start, for the guests) and the own guest script.",
        "State STATE_WORLD_STARTUP",
        "	OnEntry",
        "		LoadScript GuestLogic data/script/object/guest.lgc;",
        "		LoadScript BuilderLogic data/script/object/builder.lgc;",
        "		Area Beach 1 54 54 50 50 78 50 50 78 78 78;",
        "		BoatEntry 2300 4000 6 -32 0;",
        "		BoatPoint 2300 4000;",
        "		BoatPoint 2112 4000 Unload;",
        "		BoatPoint 2300 4000 Destroy;",
        "		BoatExit 2300 4000 6 -32 0;",
        "		BoatEvent Day 0 Time 14 6;",
        "		Create Building REP 60 60 0;",
        "		Create Building KIOSK 66 60 0;",
        "		Create Building BUILDERYARD 56 66 0;",
        "		Set PlayerCash 20000;",
        "		MoveCamera 2048 2048 0;",
        "		ChangeState STATE_WORLD_RUNGAME;",
        "	End",
        "	OnUpdate",
        "	End",
        "	OnExit",
        "	End",
        "End",
        "",
        "// The goal: every offered building finished and at least 1 earned from visitors before 17:00 on day 1",
        "// (day 2 to the player). tests/bots/l0/build_tree.bot wins it headless.",
        "State STATE_WORLD_RUNGAME",
        "	OnEntry",
        "	End",
        "	OnUpdate",
        "		If ( BuildTreeExist == 1 )",
        "			If ( IncomeToday >= 1 )",
        "				Set ScenarioObj0 1;",
        "				ShowMenu ScenarioWon L0_WON;",
        "				Function UnlockNextScenario;",
        "				ChangeState STATE_WORLD_AFTERGAME;",
        "				Function PauseGame;",
        "			End",
        "		End",
        "		If ( GameDay >= 1 )",
        "			If ( GameTime >= 17 )",
        "				ShowMenu ScenarioLost L0_LOST;",
        "				ChangeState STATE_WORLD_AFTERGAME;",
        "				Function PauseGame;",
        "			End",
        "		End",
        "	End",
        "	OnExit",
        "	End",
        "End",
        "",
        "State STATE_WORLD_AFTERGAME",
        "	OnEntry",
        "	End",
        "	OnUpdate",
        "	End",
        "	OnExit",
        "	End",
        "End", ""])
    path = out / "scenarios" / "s1" / "world.wsc"
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(script)


GUEST_SCRIPT = """#obl-script 1 object
// The own L0 guest: arrives, walks to a building it desires, is served, leaves. Every behavior beyond that stays out.

State STATE_NULL
	OnUpdate
	End
	OnEntry
		Set WalkAnimation "D_WALK_NORMAL";
		Set StandAnimation "D_STAND_NORMAL";
		Set WalkSpeed 60;
		ChangeState STATE_STARTUP;
	End
End

State STATE_STARTUP
	OnUpdate
	End
	OnEntry
		If (WorldStarted)
			ChangeState STATE_GUEST_ENTERING_WORLD;
		Else
			Function SetupStartValues;
			Message MT_STATECHANGE Me STATE_GUEST_SEARCH_DESIRE 0;
		End
	End
	OnExit
		PlayAnimation WalkAnimation;
	End
End

State STATE_GUEST_ENTERING_WORLD
	OnEntry
		Set ActionState 0;
		PlayAnimation WalkAnimation;
		ChangeState STATE_GUEST_SEARCH_DESIRE;
	End
End

State STATE_GUEST_SEARCH_DESIRE
	OnUpdate
		Function UpdateDesires;
	End
	OnEntry
		Set WalkPoint 0;
		PlayAnimation WalkAnimation;
		If (NotOnPath)
			Function GetOnPath;
		Else
			Function WalkAlongPath;
		End
	End
	Message MT_DO_NEXT_ACTION
		OnReceipt
			If (MessState == STATE_GUEST_SEARCH_DESIRE)
				Function SearchDesire;
			End
		End
	End
	Message MT_MOVEIMHERE
		OnReceipt
			If (MessState == STATE_GUEST_SEARCH_DESIRE)
				PlayAnimation StandAnimation;
				Message MT_DO_NEXT_ACTION Me 0 0 Time 2;
			End
		End
	End
	Message MT_PATHBLOCKED
		OnReceipt
			If (MessState == STATE_GUEST_SEARCH_DESIRE)
				Message MT_MOVE2POINT Me WalkPointY WalkPointX;
			End
		End
	End
End

State STATE_GUEST_HEADING_DESIRE
	OnUpdate
		Function UpdateDesires;
	End
	OnEntry
		PlayAnimation WalkAnimation;
		Set ActionState 0;
	End
	Message MT_PATHBLOCKED
		OnReceipt
			If (MessState == STATE_GUEST_HEADING_DESIRE)
				// The L0 island has no pavement: walk straight to the building.
				If (DestObject != NO_OBJECT)
					Message MT_MOVE2OBJECT Me DestObject 0;
				Else
					ChangeState STATE_GUEST_SEARCH_DESIRE;
				End
			End
		End
	End
	Message MT_BUILDING_BEING_REMOVED
		OnReceipt
			If (Sender == DestObject)
				Message MT_MOVESTOP Me 0 0;
				PlayAnimation StandAnimation;
				Set MissedPurchase true;
				ChangeState STATE_GUEST_SEARCH_DESIRE;
			End
		End
	End
	Message MT_BUILDING_CLOSING
		OnReceipt
			If (Sender == DestObject)
				Set MissedPurchase true;
				Message MT_MOVESTOP Me 0 0;
				PlayAnimation StandAnimation;
				ChangeState STATE_GUEST_SEARCH_DESIRE;
			End
		End
	End
	Message MT_MOVEIMHERE
		OnReceipt
			If (MessState == STATE_GUEST_HEADING_DESIRE)
				PlayAnimation StandAnimation;
				If (Activity)
					ChangeState STATE_GUEST_SEARCH_DESIRE;
				Else
					ChangeState STATE_GUEST_REACHED_DESIRE;
				End
			End
		End
	End
	Message MT_LAST_POINT
		OnReceipt
			If (MessState == STATE_GUEST_HEADING_DESIRE)
				If (Activity == false)
					PlayAnimation StandAnimation;
					ChangeState STATE_GUEST_REACHED_DESIRE;
				End
			End
		End
	End
End

State STATE_GUEST_REACHED_DESIRE
	OnEntry
		If (DestObject != NO_OBJECT)
			Function AlterQueueOpinion;
			Function AlterSafetyOpinion;
			Message MT_REQUEST_COMMOD DestObject DesiredCommod 0;
		End
	End
	Message MT_NO_STOCK
		OnReceipt
			Function MissedDesire;
			ChangeState STATE_GUEST_SEARCH_DESIRE;
		End
	End
	Message MT_COMMOD_PRICE
		OnReceipt
			Function FaceSender;
			Function AlterPriceOpinion;
			If (MessFloatData > MaxPrice)
				Message MT_PRICE_NOT_OK Sender MessData DesiredCommod;
				Function MissedDesire;
				ChangeState STATE_GUEST_SEARCH_DESIRE;
			Else
				If (MessFloatData > Cash)
					Message MT_PRICE_NOT_OK Sender MessData DesiredCommod;
					Function MissedDesire;
					ChangeState STATE_GUEST_SEARCH_DESIRE;
				Else
					Message MT_PRICE_OK Sender 0 0;
				End
			End
		End
	End
	Message MT_QUEUE_FULL
		OnReceipt
			Function MissedDesire;
			ChangeState STATE_GUEST_SEARCH_DESIRE;
		End
	End
	Message MT_JOIN_QUEUE
		OnReceipt
			// WalkPoint marks queue membership only until admission; native opinion helpers also write ActionState.
			Set WalkPoint 1;
			Set ActionPerson DestObject;
			ChangeState STATE_GUEST_WAITING_DESIRE;
		End
	End
	Message MT_IM_READY
		OnReceipt
			Set ActionPerson MessData;
			ChangeState STATE_GUEST_ENTER_BUILDING;
		End
	End
	OnUpdate
		Function UpdateDesires;
	End
	OnExit
		Function AddRide;
	End
End

State STATE_GUEST_ENTER_BUILDING
	OnEntry
		// WAITING_DESIRE is also used inside: clear the marker before any delayed move-up can arrive.
		Set WalkPoint 0;
		Set ActionState 0;
		PlayAnimation WalkAnimation;
		Message MT_MOVE_DIRECT_2OBJECT Me DestObject 1;
	End
	Message MT_MOVEIMHERE
		OnReceipt
			If (MessState == STATE_GUEST_ENTER_BUILDING)
				If (ActionState == 0)
					Set ActionState 1;
					Message MT_MOVE_INSIDE_BUILDING Me DestObject 0;
				Else
					PlayAnimation StandAnimation;
					ChangeState STATE_GUEST_WAITING_DESIRE;
					Message MT_IM_READY DestObject ActionPerson DesiredCommod;
				End
			End
		End
	End
End

State STATE_GUEST_WAITING_DESIRE
	OnEntry
		PlayAnimation StandAnimation;
	End
	OnUpdate
		Function UpdateDesires;
	End
	Message MT_MOVE2QUEUEPOINT
		OnReceipt
			// Native queue messages do not carry MessState; this handler is already scoped to WAITING_DESIRE.
			If (WalkPoint == 1)
				If (Sender == DestObject)
					PlayAnimation WalkAnimation;
					Message MT_MOVE2POINT Me MessData MessFlags;
				End
			End
		End
	End
	Message MT_IM_READY
		OnReceipt
			Set ActionPerson MessData;
			ChangeState STATE_GUEST_ENTER_BUILDING;
		End
	End
	Message MT_SERVED_COMMOD
		OnReceipt
			Set ActionState 0;
			Function UpdatePriceScore;
			Minus Cash CommodPrice;
			PlaySound cash;
			Function AlterDesires;
			ChangeState STATE_GUEST_EXIT_BUILDING;
		End
	End
	Message MT_NO_STOCK
		OnReceipt
			Function MissedDesire;
			ChangeState STATE_GUEST_SEARCH_DESIRE;
		End
	End
End

State STATE_GUEST_EXIT_BUILDING
	OnEntry
		Set WalkPoint 0;
		PlayAnimation WalkAnimation;
		Function SetVisible;
		// Leaving frees the guest's place in the building; the building then lets the next queued guest in (MT_IM_READY).
		Message MT_COMMOD_QUALITY DestObject DesiredCommod 0;
		Message MT_MOVE_INSIDE_BUILDING Me DestObject 1;
	End
	Message MT_MOVEIMHERE
		OnReceipt
			If (MessState == STATE_GUEST_EXIT_BUILDING)
				Add WalkPoint 1;
				If (WalkPoint == 1)
					Message MT_MOVE_DIRECT_2OBJECT Me DestObject 1;
				Else
					ChangeState STATE_GUEST_LEAVING_WORLD;
				End
			End
		End
	End
End

State STATE_GUEST_LEAVING_WORLD
	// No wharf on the L0 island: the guest is removed in this state, which counts the departure.
	OnEntry
		Function ClearMyDelayedMsgs;
		Message MT_KILLOBJECT -2 Me 0;
	End
End
"""


BUILDER_SCRIPT = """#obl-script 1 object
// The own L0 builder: waits at the yard, walks to the job the construction manager hands out (MT_CONSTRUCT), works it off
// (RemoveCostPoint every tick) until MT_CONSTRUCTION_FINISHED, then waits for the next one. Off-shift builders release jobs.

State STATE_NULL
	OnEntry
		Set WalkAnimation "D_WALK_NORMAL";
		Set StandAnimation "D_STAND_NORMAL";
		ChangeState STATE_STARTUP;
	End
End

State STATE_STARTUP
	OnEntry
		ChangeState STATE_BUILDER_RESTING;
	End
	Message MT_DEMOLISH
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
	Message MT_CONSTRUCT
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
End

State STATE_BUILDER_RESTING
	OnUpdate
		If (AtWork)
			Function ReallocateBuilders;
		End
	End
	OnEntry
		PlayAnimation StandAnimation;
		Set BuilderLocation 0;
		// Asks the manager for work: a waiting job answers with MT_CONSTRUCT.
		If (AtWork)
			Function ReallocateBuilders;
		End
	End
	Message MT_DEMOLISH
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
	Message MT_CONSTRUCT
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
End

State STATE_BUILDER_GO_BUILD
	OnUpdate
		If (AtWork == false)
			Function FreeBuilderOfJob;
			ChangeState STATE_BUILDER_RESTING;
		End
	End
	OnEntry
		If (AtWork && IsJobStillValid)
			Set BuilderLocation 1;
			PlayAnimation WalkAnimation;
			// The L0 island has no pavement: walk straight to the site.
			Message MT_MOVE2POINT Me BuildEntPointY BuildEntPointX;
		Else
			Function FreeBuilderOfJob;
			ChangeState STATE_BUILDER_RESTING;
		End
	End
	Message MT_PATHBLOCKED
		OnReceipt
			Message MT_MOVE2POINT Me BuildEntPointY BuildEntPointX;
		End
	End
	Message MT_MOVEIMHERE
		OnReceipt
			If (MessState == STATE_BUILDER_GO_BUILD)
				If (AmIDemolishing)
					ChangeState STATE_BUILDER_DO_DEMOLISHING;
				Else
					ChangeState STATE_BUILDER_DO_BUILDING;
				End
			End
		End
	End
	Message MT_DEMOLISH
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
	Message MT_CONSTRUCTION_FINISHED
		OnReceipt
			Function FreeBuilderOfJob;
			ChangeState STATE_BUILDER_RESTING;
		End
	End
End

State STATE_BUILDER_DO_BUILDING
	OnEntry
		PlayAnimation StandAnimation;
		Set BuilderLocation 3;
	End
	OnUpdate
		If (AtWork && IsJobStillValid)
			Function RemoveCostPoint;
		Else
			Function FreeBuilderOfJob;
			ChangeState STATE_BUILDER_RESTING;
		End
	End
	Message MT_DEMOLISH
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
	Message MT_CONSTRUCTION_FINISHED
		OnReceipt
			Set BuilderLocation 5;
			Function FreeBuilderOfJob;
			ChangeState STATE_BUILDER_RESTING;
		End
	End
	Message MT_DEMOLISH
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
	Message MT_CONSTRUCT
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
End

State STATE_BUILDER_DO_DEMOLISHING
	OnEntry
		PlayAnimation StandAnimation;
		Set BuilderLocation 3;
	End
	OnUpdate
		If (AtWork && IsJobStillValid)
			Function RemoveCostPoint;
		Else
			Function FreeBuilderOfJob;
			ChangeState STATE_BUILDER_RESTING;
		End
	End
	Message MT_DEMOLISH
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
	Message MT_CONSTRUCTION_FINISHED
		OnReceipt
			Set BuilderLocation 5;
			Function FreeBuilderOfJob;
			ChangeState STATE_BUILDER_RESTING;
		End
	End
	Message MT_DEMOLISH
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
	Message MT_CONSTRUCT
		OnReceipt
			If (AtWork)
				ChangeState STATE_BUILDER_GO_BUILD;
			Else
				Function FreeBuilderOfJob;
			End
		End
	End
End
"""


def write_feedback_vocabulary(out: Path) -> None:
    """The feedback words: complaint keys by commodity family, scenario message-type words, the notice for each missing
    staff class and the hireable professions (src/game/feedback/feedback_vocabulary.h). Same words the engine's own texts use."""
    keys = ["ACCOMMODATION", "ALCOHOL", "AMUSEMENT", "FIRSTAID", "HEADACHE", "MEALS", "PARTYFUN", "SHOWER", "SNACKS", "SOFTDRINKS",
            "SOUVENIRS", "SPORTSFUN", "SUNBATHING", "SWIMMINGFUN", "TOILET", "WATERSPORTSFUN", "STROLLAROUND", "LEAVERESORT", "VANDALISE",
            "FIGHT", "FLIRT", "UMBRELLAS", "SUNTANLOTION", "HARDSEX", "SLEEP", "SCARED"]
    kinds = ["SCENERY", "BUILDING", "GUEST", "VEHICLE", "STAFF", "LIFEGUARD", "MECHANIC", "REP", "BUILDER", "CLEANER", "SECURITYGUARD"]
    types = [{"word": "MESS_TYPE_WORLD", "silences": "world"}, {"word": "MESS_TYPE_OBJECT", "silences": "object"}]
    types += [{"word": f"MESS_TYPE_{kind}", "type": 3 + i} for i, kind in enumerate(kinds)]
    missing = [("Cleaner", "NO_CLEANERS", "CLEANER"), ("Rep", "NO_REPS", "REP"), ("Mechanic", "NO_MECHANICS", "MECHANIC"),
               ("SecurityGuard", "NO_SECURITY", "SECURITYGUARD"), ("Lifeguard", "NO_LIFEGUARDS", "LIFEGUARD")]
    professions = ["Cleaner", "Builder", "Mechanic", "Rep", "Lifeguard", "SecurityGuard"]
    write_json(out / "scripts" / "feedback_vocabulary.json", {
        "format": "obl.feedback_vocabulary", "version": 1,
        "commodity_keys": [f"FEEDBACK_{key}" for key in keys],
        "message_types": types,
        "missing_staff": [{"class": cls, "key": f"FEEDBACK_{key}", "type": f"MESS_TYPE_{kind}"} for cls, key, kind in missing],
        "professions": [{"name": name, "hire_type": i} for i, name in enumerate(professions)]})


def write_object_script(out: Path) -> None:
    """The own guest and builder behavior, in the syntax the engine parses (`data/script/object/<name>.lgc` is `<name>.obs`)."""
    for name, text in (("guest", GUEST_SCRIPT), ("builder", BUILDER_SCRIPT)):
        path = out / "scripts" / "object" / f"{name}.obs"
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)


# (id, name): the states the own scripts use; the ids are the engine's. Functions and values are the engine's script API.
OBJECT_STATES = [(0, "STATE_NULL"), (1, "STATE_STARTUP"), (12, "STATE_GUEST_SEARCH_DESIRE"), (13, "STATE_GUEST_HEADING_DESIRE"),
                 (14, "STATE_GUEST_REACHED_DESIRE"), (15, "STATE_GUEST_WAITING_DESIRE"), (27, "STATE_GUEST_ENTER_BUILDING"),
                 (28, "STATE_GUEST_EXIT_BUILDING"), (40, "STATE_GUEST_ENTERING_WORLD"), (41, "STATE_GUEST_LEAVING_WORLD"),
                 (95, "STATE_BUILDER_RESTING"), (97, "STATE_BUILDER_GO_BUILD"), (99, "STATE_BUILDER_DO_BUILDING"), (100, "STATE_BUILDER_DO_DEMOLISHING")]


def write_object_vocabulary(out: Path) -> None:
    """The object script vocabulary: all 108 message ids from our enum and the states the own scripts name."""
    import re
    header = (Path(__file__).resolve().parent.parent / "src/game/messages/message_state.h").read_text(encoding="utf-8")
    messages = [(int(i), n) for n, i in re.findall(r"(MT_\w+)\s*=\s*(\d+)", header)]
    rows = lambda pairs: [{"id": i, "name": n} for i, n in pairs]
    write_json(out / "scripts" / "object_vocabulary.json", {
        "format": "obl.object_script_vocabulary", "version": 1, "states": rows(OBJECT_STATES), "messages": rows(messages)})


def write_guests(out: Path) -> None:
    """The smallest own guest set: one type on the placeholder sprite; the guest item grows it."""
    write_json(out / "guests" / "l0.json", {
        "format": "obl.guest_set", "version": 2,
        "types": [{"key": "visitor", "name": "Visitor", "gender": "m", "sprite": "dummy", "money": 200, "nights": [1, 2],
                   "preferences": {"soft_drinks": [0.9, 1.0], "stroll": [0.01, 0.02]},
                   "tolerances": {k: [0.5, 1.0] for k in ("beauty", "crowding", "attractiveness", "price", "variety", "cleanliness", "safety")},
                   "opinion_start": {k: [0.8, 0.9] for k in ("beauty", "crowding", "attractiveness", "price", "variety", "cleanliness", "safety")},
                   "max": {"soft_drinks": 1.0}, "sleep_time": [22, 24], "wake_time": [7, 9]}],
        "names": {"surnames": ["L0_SURNAME"], "male": ["L0_MALE"], "female": ["L0_FEMALE"]}})


def write_menu_sprites(out: Path) -> None:
    """Hand-drawn 5 x 7 capitals baked into own buttons; the diagnostic fonts remain placeholders."""
    alphabet = {
        "A": "01110 10001 10001 11111 10001 10001 10001", "B": "11110 10001 10001 11110 10001 10001 11110",
        "C": "01111 10000 10000 10000 10000 10000 01111", "D": "11110 10001 10001 10001 10001 10001 11110",
        "E": "11111 10000 10000 11110 10000 10000 11111", "F": "11111 10000 10000 11110 10000 10000 10000",
        "G": "01111 10000 10000 10111 10001 10001 01111", "H": "10001 10001 10001 11111 10001 10001 10001",
        "I": "11111 00100 00100 00100 00100 00100 11111", "J": "00111 00010 00010 00010 10010 10010 01100",
        "K": "10001 10010 10100 11000 10100 10010 10001", "L": "10000 10000 10000 10000 10000 10000 11111",
        "M": "10001 11011 10101 10101 10001 10001 10001", "N": "10001 11001 11001 10101 10011 10011 10001",
        "O": "01110 10001 10001 10001 10001 10001 01110", "P": "11110 10001 10001 11110 10000 10000 10000",
        "Q": "01110 10001 10001 10001 10101 10010 01101", "R": "11110 10001 10001 11110 10100 10010 10001",
        "S": "01111 10000 10000 01110 00001 00001 11110", "T": "11111 00100 00100 00100 00100 00100 00100",
        "U": "10001 10001 10001 10001 10001 10001 01110", "V": "10001 10001 10001 10001 10001 01010 00100",
        "W": "10001 10001 10001 10101 10101 10101 01010", "X": "10001 10001 01010 00100 01010 10001 10001",
        "Y": "10001 10001 01010 00100 00100 00100 00100", "Z": "11111 00001 00010 00100 01000 10000 11111",
        "1": "00100 01100 00100 00100 00100 00100 01110", " ": "00000 " * 6 + "00000",
    }
    sprites = out / "sprites"
    for label in ("START", "SANDBOX", "LOAD", "EXTRAS", "EXIT", "ISLAND 1", "MENU", "PLAY", "CREDITS", "RESET"):
        name = "l0_menu_" + label.lower().replace(" ", "_")
        write_palette(sprites / f"{name}.palettes.png", 256, (150, 210, 240))

        def draw(d, i, w, h):
            d.rectangle([0, 0, w - 1, h - 1], fill=(45 + i * 25, 255), outline=(220, 255))
            left = (w - (len(label) * 12 - 2)) // 2
            for n, char in enumerate(label):
                for y, row in enumerate(alphabet[char].split()):
                    for x, bit in enumerate(row):
                        if bit == "1":
                            px, py = left + n * 12 + x * 2, 13 + y * 2
                            d.rectangle([px, py, px + 1, py + 1], fill=(255, 255))

        sheet(sprites / name / "sheet.png", 4, 1, (180, 40), draw)
        write_json(sprites / f"{name}.json", {
            "format": "obl.sprite", "version": 1, "name": name, "kind": "ui", "pixels": "indexed",
            "palettes": f"{name}.palettes.png", "palette_colors": 256, "palette_count": 1,
            "animations": [{"name": name, "directions": 4, "frames": 1, "size": [180, 40], "hotspot": [0, 0],
                            "columns": 4, "unique_frames": 4, "sheets": [f"{name}/sheet.png"]}]})


def write_play_gui(out: Path) -> None:
    """Own play layouts and labels; kept separate from the scenario and guest generators."""
    sprites = out / "sprites"
    sprites.mkdir(parents=True, exist_ok=True)
    for name, size, frames in [("l0_play_button", (100, 24), 4), ("l0_play_panel", (380, 260), 1),
                               ("l0_play_bar", (800, 88), 1), ("l0_play_bar640", (640, 70), 1), ("UI3_bp_thumb", (48, 48), 1),
                               ("UI3_bp_thumb_grey", (48, 48), 1)]:
        write_palette(sprites / f"{name}.palettes.png", 256, (130, 190, 220))
        def rectangle(d, i, w, h):
            d.rectangle([0, 0, w - 1, h - 1], fill=(55 + i * 25, 255), outline=(220, 255))
        sheet(sprites / name / "sheet.png", frames, 1, size, rectangle)
        write_json(sprites / f"{name}.json", {
            "format": "obl.sprite", "version": 1, "name": name, "kind": "ui", "pixels": "indexed",
            "palettes": f"{name}.palettes.png", "palette_colors": 256, "palette_count": 1,
            "animations": [{"name": name, "directions": frames, "frames": 1, "size": list(size), "hotspot": [0, 0],
                            "columns": frames, "unique_frames": frames, "sheets": [f"{name}/sheet.png"]}]})

    labels = {}
    def button(x, y, label, action):
        key = "L0_PLAY_" + action.upper()
        labels[key] = label
        return f"\tButton({x}, {y}, {x + 100}, {y + 24}, l0_play_button, {key}, {action}, None, {key})"
    def variable(x, y, key, width=190):
        return f"\tVariable({x}, {y}, {x + width}, {y + 20}, {key}, 0, 16777215, 0)"
    def dialog(name, children):
        return "\n".join([f"DialogItem {name}(210, 120, 590, 380, l0_play_panel, 0, 1)", "{", *children, "}"])
    # BUILD_* image lists support categories 0..2. Give our public buildings selectable categories;
    # the startup representative station is a helper and stays outside the build palette.
    for key in ["loo", "kiosk", "hut", "rep"]:
        path = out / "catalog" / "buildings" / f"{key}.json"
        doc = json.loads(path.read_text(encoding="utf-8"))
        doc["menu"] = "L0Building"
        if key in ["loo", "hut"]:
            doc["category"] = "leisure"
        write_json(path, doc)
        labels[f"BUILDING_NAME_{key.upper()}"] = doc["name"]
        labels[f"BUILD_DESC_{key.upper()}"] = f"Build a {doc['name'].lower()} on the island."
        labels[f"BUILD_TIPTEXT_{key.upper()}"] = doc["name"]
    # The manager allocates a bottom HUD surface (94/75 rows); UI pixels above it corrupt that surface.
    # Cover the whole reserved strip: the world redraw excludes it, so transparent gaps retain loading-screen pixels.
    hud = ["\tImage(0, 512, 800, 600, l0_play_bar, None)",
           button(8, 568, "Build", "BuildList"), button(112, 568, "Camera", "CameraMode"),
           button(216, 568, "Goals", "ShowGoal"), variable(330, 570, "PlayerMoney$"), variable(575, 570, "Time")]
    # B coordinates keep the HUD inside its bottom surface at every screen height.
    import re
    hud = [re.sub(r"\((\d+), (\d+), (\d+), (\d+),",
                  lambda m: f"(B{m[1]}, {600 - int(m[2])}, {m[3]}, {600 - int(m[4])},", row, count=1) for row in hud]
    parts = ["#obl-gui 1", "Font(GuiFont8)", "Font(GuiFont8b)", "Font(GuiFont12b)",
             "MainItem {\n" + "\n".join(hud) + "\n}",
             "MainItem640 {\n" + "\n".join([hud[0].replace("800", "640").replace("B0, 88", "B0, 70").replace("l0_play_bar", "l0_play_bar640"), *hud[1:-1]]) + "\n}",
             dialog("BuildPanel", [
                 "\tImageList(12, 35, 160, 95, 52, 52, BUILD_FOOD_SMALL, None)",
                 "\tImageList(12, 105, 160, 165, 52, 52, BUILD_LEISURE_SMALL, None)",
                 variable(170, 40, "BUILD_NAME_TEXT"), variable(170, 70, "BUILD_COST_AMOUNT"),
                 button(12, 210, "Place", "PlaceBuilding"), button(260, 210, "Close", "ExitDialog")]),
             dialog("CancelBuildMode", [button(12, 35, "Cancel", "ExitBuildMode")]),
             dialog("L0Building", [variable(12, 35, "BUILDING_NAME"), variable(12, 65, "BUILDING_OPEN"),
                 button(12, 110, "Close shop", "CloseBuilding"), button(120, 110, "Open shop", "OpenBuilding"),
                 button(12, 150, "Repair", "CallRepair"), button(120, 150, "Clean", "CallCleaner"),
                 button(12, 185, "Follow", "LockCamera"),
                 button(120, 185, "Sell", "SellButton"),
                 button(260, 210, "Close", "ExitDialog")]),
             dialog("ConfirmDemolish", [variable(12, 35, "BUILDING_NAME"),
                 button(12, 110, "Sell", "ConfirmSell"), button(260, 210, "Close", "ExitDialog")]),
             dialog("CameraControls", [button(12, 35, "Zoom in", "ZoomIn"), button(120, 35, "Zoom out", "ZoomOut"),
                 button(12, 115, "Photo", "DoCameraGrab"),
                 button(260, 210, "Back", "CameraMode")]),
             dialog("Objectives", [variable(12, 35, "SCENARIO_TITLE"), variable(12, 70, "SCENARIO_DESCRIPTION"),
                 button(260, 210, "Close", "ExitDialog")]),
             "DialogItem L0Progress(0, 0, 93, 59, CLEAR, 24, 0)\n{\n" + variable(2, 22, "NumGuests", 80) + "\n}"]
    path = out / "gui" / "in_game.gui"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(("\n\n".join(parts) + "\n").encode("utf-8"))
    labels.update({"OPEN": "Open", "CLOSED": "Closed"})
    path = out / "text" / "en.json"
    doc = json.loads(path.read_text(encoding="utf-8"))
    doc["entries"].extend(sorted(labels.items()))
    write_json(path, doc)


def write_menu_gui(out: Path) -> None:
    """Own credits and object browser tables (the engine refuses a pack without them) and the labels of the menu dialogs
    in gui/main_menu.gui that the base pack has and L0 lacked (CreditsScreen, LoadGameMenu, ResetProgressConfirm)."""
    sprites = out / "sprites"
    write_palette(sprites / "l0_menu_panel.palettes.png", 256, (150, 210, 240))

    def panel(d, i, w, h):
        d.rectangle([0, 0, w - 1, h - 1], fill=(45 + i * 25, 255), outline=(220, 255))

    sheet(sprites / "l0_menu_panel" / "sheet.png", 1, 1, (595, 295), panel)
    write_json(sprites / "l0_menu_panel.json", {
        "format": "obl.sprite", "version": 1, "name": "l0_menu_panel", "kind": "ui", "pixels": "indexed",
        "palettes": "l0_menu_panel.palettes.png", "palette_colors": 256, "palette_count": 1,
        "animations": [{"name": "l0_menu_panel", "directions": 1, "frames": 1, "size": [595, 295], "hotspot": [0, 0],
                        "columns": 1, "unique_frames": 1, "sheets": ["l0_menu_panel/sheet.png"]}]})
    # Authored by us (provenance own, as pack.json): no imported data. Eight button names are required; no row uses them.
    write_json(out / "gui" / "object_browser.json", {
        "format": "obl.object_browser", "version": 2, "provenance": "own",
        "lists": {name: [] for name in ["guest", "building", "cell", "staff", "hire", "event"]},
        "headings": [], "buttons": ["l0_play_button"] * 8})
    # A single final row (kind 1); own_credits appends the L0 block after it and takes over the end mark.
    write_json(out / "gui" / "credits.json", {
        "format": "obl.credits", "version": 2, "provenance": "own",
        "rows": [{"font": 2, "line": 0, "color": 0, "long_spacing": True, "kind": 1}]})
    labels = {"CREDIT_ENTRY_0": "OpenBeach L0 (own placeholder island)",
              "L0_RESET_TITLE": "Reset progress", "L0_RESET_CONFIRM": "Reset all scenario progress?",
              "L0_RESET_YES": "Yes", "L0_RESET_NO": "No",
              "L0_LOAD_TITLE": "Load game", "L0_LOAD_SCENARIO": "Scenario", "L0_LOAD_GAMEINFO": "Game",
              "L0_LOAD_SAVED": "Saved", "L0_LOAD_LOAD": "Load", "L0_LOAD_DELETE": "Delete", "L0_LOAD_CANCEL": "Cancel"}
    path = out / "text" / "en.json"
    doc = json.loads(path.read_text(encoding="utf-8"))
    doc["entries"].extend(sorted(labels.items()))
    write_json(path, doc)


def write_startup_content(out: Path) -> None:
    """Own startup layout and parameters; engine vocabulary is documented in l0-provenance.md."""
    write_json(out / "rules/weather_parameters.json", {
        "format": "obl.weather_parameters", "version": 2,
        "light_keys": [[hour, 0xffffff] for hour in range(24)],
        "state_colors": [0] * 6 + [0xffffff] * 8,
    })
    write_json(out / "rules/game_parameters.json", {
        "format": "obl.game_parameters",
        "version": 1,
        "parameters": {
            "play_movie": True,
            "sound_enabled": True,
            "night_day_enabled": True,
            "startup_in_windowed_mode": False,
            "quick_load_map": False,
            "day_length_in_seconds": 600,
            "map_gridsize_xy": 40,
            "map_scale_z": 2.5,
            "buffer_lightmaps": False,
            "dont_unload_map_chunks": False,
            "refresh_rects": False,
            "debugfont_filename": "l0_diagnostic",
            "debugfont_name": "L0 Diagnostic",
            "debugfont_size": 6,
            "map_rmb_movement_speed": 10,
            "map_nudge_speed": 30,
            "map_max_velocity": 60.0
        }
    })
    (out / "gui/main_menu.gui").write_bytes("""#obl-gui 1
Font(GuiFont8)
Font(GuiFont8b)
Font(GuiFont12b)

// Own buttons contain hand-drawn readable captions, independent of the diagnostic text font.
DialogItem MainMenu(0, 0, 800, 600, None, 0, 0)
{
	TGA(0, 0, 800, 600, l0_welcome.tga)
	Button(310, 314, 490, 354, l0_menu_start, None, ShowScenSel, None, L0_MENU_TIP_START)
	Button(300, 363, 480, 403, l0_menu_sandbox, None, PlaySandBox, None, L0_MENU_TIP_SANDBOX)
	Button(290, 412, 470, 452, l0_menu_load, None, LoadGameMenu, None, L0_MENU_TIP_LOAD)
	Button(280, 461, 460, 501, l0_menu_extras, None, ExtrasMenu, None, L0_MENU_TIP_OPTIONS)
	Button(270, 510, 450, 550, l0_menu_exit, None, ExitWindows, None, L0_MENU_TIP_EXIT)
}

DialogItem ScenarioScreen(0, 0, 800, 600, NONE, 0, 0)
{
	TGA(0, 0, 800, 600, l0_islands.tga)
	RadioGroup(0, 0, 532, 476, GAME_SCENARIO_SELECT, None)
	{
		RadioButton(168, 408, 348, 448, l0_menu_island_1, 0, 0, None, L0_MENU_TIP_ISLAND)
	}
	Variable(561, 289, 777, 319, SS_CUR_SCENARIO_TITLE, 1, 0, 2)
	Variable(561, 319, 777, 349, SS_CUR_SCENARIO_STATUS, 0, 0, 2)
	Variable(561, 349, 777, 491, SS_CUR_SCENARIO_DESC, 0, 0, 2)
	Button(20, 534, 200, 574, l0_menu_menu, None, ShowMainMenu, None, L0_MENU_TIP_MENU)
	Button(598, 534, 778, 574, l0_menu_play, None, StartNewGame, None, L0_MENU_TIP_PLAY)
	VariableImage(559, 86, 765, 205, SCENARIO_PIC_LARGE)
	VariableImage(538, 12, 770, 76, SANDBOX_TITLE)
}

DialogItem ExtrasScreen(0, 0, 800, 600, NONE, 0, 0)
{
	TGA(0, 0, 800, 600, l0_options.tga)
	Button(334, 363, 514, 403, l0_menu_credits, None, CreditsMenu, None, L0_MENU_TIP_CREDITS)
	Button(354, 412, 534, 452, l0_menu_reset, None, ResetProgress, None, L0_MENU_TIP_RESET)
	Button(20, 534, 200, 574, l0_menu_menu, None, ShowMainMenu, None, L0_MENU_TIP_MENU)
}

DialogItem CreditsScreen(0, 0, 800, 600, NONE, 0, 0)
{
	TGA(0, 0, 800, 600, l0_acknowledgements.tga)
	Button(20, 534, 200, 574, l0_menu_menu, None, ExtrasMenu, None, L0_MENU_TIP_OPTIONS)
}

DialogItem LoadGameMenu(102, 152, 697, 447, l0_menu_panel, 0, 0)
{
	Text(0, 8, 595, 30, L0_LOAD_TITLE, 2, 0, 4)
	ListBox(160, 57, 135, 261, 0, 0, 0, 0, LoadInfoBox, 15, 5)
	Text(32, 156, 311, 171, L0_LOAD_SCENARIO, 1, 0, 2)
	Text(311, 156, 422, 171, L0_LOAD_GAMEINFO, 1, 0, 2)
	Text(422, 156, 537, 171, L0_LOAD_SAVED, 1, 0, 2)
	ListBox(32, 172, 537, 262, 551, 157, 567, 262, LoadListBox, 15, 6)
	VariableImage(43, 61, 151, 132, SCENARIO_PIC_LOAD)
	Button(411, 53, 511, 77, l0_play_button, L0_LOAD_LOAD, LoadGame, None, L0_LOAD_LOAD)
	Button(411, 84, 511, 108, l0_play_button, L0_LOAD_DELETE, DeleteLoadGame, None, L0_LOAD_DELETE)
	Button(411, 115, 511, 139, l0_play_button, L0_LOAD_CANCEL, ExitLoadGame, None, L0_LOAD_CANCEL)
}

// Use a separate window key so confirmation keeps the options screen open.
DialogItem ResetProgressConfirm(210, 170, 590, 430, l0_play_panel, 20, 0)
{
	Text(0, 8, 380, 40, L0_RESET_TITLE, 2, 0, 4)
	Text(20, 60, 360, 150, L0_RESET_CONFIRM, 0, 0, 2)
	Button(60, 200, 160, 224, l0_play_button, L0_RESET_NO, ExitDialog, None, L0_RESET_NO)
	Button(220, 200, 320, 224, l0_play_button, L0_RESET_YES, ResetProgressConfirm, None, L0_RESET_YES)
}
""".encode("utf-8"))
    (out / "gui/intro.gui").write_bytes("""#obl-gui 1
DialogItem DeepRedLogo(0, 0, 800, 600, NONE, 0, 0)
{
	TGA(0, 0, 800, 600, l0_opening.tga)
}

DialogItem FrontScreen(0, 0, 800, 600, NONE, 0, 0)
{
	TGA(0, 0, 800, 600, l0_title.tga)
}
""".encode("utf-8"))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "data/packs/l0"))
    out = Path(ap.parse_args().out)
    write_images(out)
    write_sprites(out)
    write_sounds(out)
    write_media(out)
    write_fonts(out)
    write_palette_modifiers(out)
    write_maps(out)
    write_catalog(out)
    write_texts(out)
    write_scenario(out)
    write_world_script(out)
    write_object_vocabulary(out)
    write_feedback_vocabulary(out)
    write_object_script(out)
    write_guests(out)
    write_menu_sprites(out)
    write_play_gui(out)
    write_menu_gui(out)
    write_startup_content(out)


if __name__ == "__main__":
    main()
