#!/usr/bin/env python3
"""Build an indexed OpenBeach sprite (obl.sprite v1) from PNG frames.

Converts one or more RGBA/RGB/P PNG frames into the indexed palette + gray-alpha
sheet format that src/formats/sprite_pack.cpp loads, including the paired color
and depth planes that `kind: "building"` sprites require and single-frame UI
icons for the build menu.

Examples:
    # Building sprite (4 directions, auto-generated depth plane):
    python tools/make_sprite.py --name ice_stand --kind building --directions 4 \
        --hotspot -60 -90 --out mods/ice_stand/sprites \
        --frames art/n.png art/e.png art/s.png art/w.png

    # Building sprite with a single PNG reused for all 4 directions and a night lightmap:
    python tools/make_sprite.py --name ice_stand --kind building --directions 4 \
        --repeat-directions --hotspot -48 -80 --lightmap art/light.png \
        --out mods/ice_stand/sprites --frames art/front.png

    # 48x48 build-panel icon:
    python tools/make_sprite.py --name ice_stand_icon --kind ui \
        --out mods/ice_stand/sprites --frames art/icon.png

    # Multi-animation sprite from a JSON spec:
    python tools/make_sprite.py --spec sprite_spec.json --out mods/my_mod/sprites
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import Sequence

from PIL import Image

PALETTE_SIZES = {
    "ui": 256,
    "special": 256,
    "character": 64,
    "building": 256,
    "object_variants": 64,
}

KEY_RE = re.compile(r"^[A-Za-z0-9_.-]+$")


class SpriteError(ValueError):
    """Raised when input frames or options cannot form a valid obl.sprite."""


def validate_key(value: str, label: str) -> str:
    if not value or not KEY_RE.match(value):
        raise SpriteError(f"invalid {label} {value!r}: expected letters, digits, '_', '.' or '-'")
    return value


def load_rgba(path: Path) -> Image.Image:
    if not path.is_file():
        raise SpriteError(f"frame file not found: {path}")
    try:
        with Image.open(path) as im:
            return im.convert("RGBA")
    except OSError as exc:
        raise SpriteError(f"cannot read image {path}: {exc}") from exc


def check_frame_sizes(frames: Sequence[tuple[Path, Image.Image]], expected: tuple[int, int] | None = None,
                      label: str = "animation") -> tuple[int, int]:
    if not frames:
        raise SpriteError(f"{label}: at least one frame is required")
    size = expected or frames[0][1].size
    if size[0] <= 0 or size[1] <= 0:
        raise SpriteError(f"{label}: frame size {size[0]}x{size[1]} must be positive")
    for path, im in frames:
        if im.size != size:
            raise SpriteError(
                f"{label}: frame {path} is {im.size[0]}x{im.size[1]}, expected {size[0]}x{size[1]}"
            )
    return size


def get_pixels(im: Image.Image):
    fn = getattr(im, "get_flattened_data", None)
    return fn() if fn is not None else im.getdata()


def build_palette(images: Sequence[Image.Image], palette_colors: int, alpha_threshold: int) -> tuple[list[tuple[int, int, int]], dict[tuple[int, int, int], int]]:
    """Collect or quantize opaque RGB pixels into indices 1..palette_colors-1 (0 is reserved for transparent)."""
    max_opaque = palette_colors - 1
    unique: set[tuple[int, int, int]] = set()
    opaque_pixels: list[tuple[int, int, int]] = []
    for im in images:
        for r, g, b, a in get_pixels(im):
            if a >= alpha_threshold:
                rgb = (r, g, b)
                unique.add(rgb)
                opaque_pixels.append(rgb)

    palette: list[tuple[int, int, int]] = [(0, 0, 0)] * palette_colors
    if len(unique) <= max_opaque:
        ordered = sorted(unique)
        mapping = {rgb: idx + 1 for idx, rgb in enumerate(ordered)}
        for idx, rgb in enumerate(ordered):
            palette[idx + 1] = rgb
        return palette, mapping

    # Quantize all opaque pixels together so every animation shares one palette.
    strip = Image.new("RGB", (len(opaque_pixels), 1))
    strip.putdata(opaque_pixels)
    quantized = strip.quantize(colors=max_opaque, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw = quantized.getpalette() or []
    q_pixels = list(get_pixels(quantized))
    used = sorted(set(q_pixels))
    remap: dict[int, int] = {}
    for slot, q_idx in enumerate(used, start=1):
        palette[slot] = (raw[q_idx * 3], raw[q_idx * 3 + 1], raw[q_idx * 3 + 2])
        remap[q_idx] = slot
    mapping: dict[tuple[int, int, int], int] = {}
    for rgb, q_idx in zip(opaque_pixels, q_pixels):
        mapping[rgb] = remap[q_idx]
    return palette, mapping


def index_frame(im: Image.Image, mapping: dict[tuple[int, int, int], int], alpha_threshold: int) -> Image.Image:
    out = Image.new("LA", im.size, (0, 0))
    pixels = [
        (mapping[(r, g, b)], 255) if a >= alpha_threshold else (0, 0)
        for r, g, b, a in get_pixels(im)
    ]
    out.putdata(pixels)
    return out


def make_auto_depth_frame(color_rgba: Image.Image, alpha_threshold: int) -> Image.Image:
    """Default depth plane matching the color frame's opaque mask at front footprint depth (255)."""
    w, h = color_rgba.size
    out = Image.new("LA", (w, h), (0, 0))
    pixels = [(255, 255) if a >= alpha_threshold else (0, 0) for _, _, _, a in get_pixels(color_rgba)]
    out.putdata(pixels)
    return out


def make_custom_depth_frame(depth_rgba: Image.Image, color_rgba: Image.Image, alpha_threshold: int) -> Image.Image:
    """Depth plane from a grayscale/RGB PNG, masked to the exact opaque pixels of the color frame."""
    w, h = color_rgba.size
    out = Image.new("LA", (w, h), (0, 0))
    color_data = list(get_pixels(color_rgba))
    depth_l = list(get_pixels(depth_rgba.convert("L")))
    pixels = [
        (d, 255) if c[3] >= alpha_threshold else (0, 0)
        for c, d in zip(color_data, depth_l)
    ]
    out.putdata(pixels)
    return out


def make_light_frame(light_rgba: Image.Image, alpha_threshold: int) -> Image.Image:
    """Light plane: tone-table level (1..15) scaled from grayscale brightness, 0 where unlit."""
    w, h = light_rgba.size
    out = Image.new("LA", (w, h), (0, 0))
    light_data = list(get_pixels(light_rgba))
    light_l = list(get_pixels(light_rgba.convert("L")))
    pixels = [
        (max(1, min(15, round(lv * 15 / 255))), 255) if (rgba[3] >= alpha_threshold and lv > 0) else (0, 0)
        for rgba, lv in zip(light_data, light_l)
    ]
    out.putdata(pixels)
    return out


def make_mask_frame(mask_rgba: Image.Image, alpha_threshold: int) -> Image.Image:
    """Mask plane (pick / shadow): alpha >= threshold marks opaque pixels."""
    out = Image.new("LA", mask_rgba.size, (0, 0))
    pixels = [(255, 255) if a >= alpha_threshold else (0, 0) for _, _, _, a in get_pixels(mask_rgba)]
    out.putdata(pixels)
    return out


def write_sheet(path: Path, frames: Sequence[Image.Image], columns: int) -> None:
    w, h = frames[0].size
    rows = math.ceil(len(frames) / columns)
    sheet = Image.new("LA", (columns * w, rows * h), (0, 0))
    for i, frame in enumerate(frames):
        sheet.paste(frame, ((i % columns) * w, (i // columns) * h))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


def write_palette_png(path: Path, palette: Sequence[tuple[int, int, int]]) -> None:
    im = Image.new("RGB", (len(palette), 1))
    im.putdata(list(palette))
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)


def write_json(path: Path, doc: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(doc, indent=2) + "\n")


def resolve_frame_layout(num_unique: int, directions: int, repeat_directions: bool, label: str) -> tuple[int, list[int] | None]:
    if directions < 1:
        raise SpriteError(f"{label}: directions must be >= 1")
    if repeat_directions:
        frames_per_dir = num_unique
        frame_map = [f for _ in range(directions) for f in range(frames_per_dir)]
        return frames_per_dir, frame_map if directions > 1 else None
    if num_unique % directions != 0:
        raise SpriteError(
            f"{label}: {num_unique} frames cannot be divided evenly across {directions} directions "
            f"(pass --repeat-directions to reuse frames across directions)"
        )
    return num_unique // directions, None


def build_sprite(
    *,
    name: str,
    kind: str,
    out_dir: Path,
    animations: Sequence[dict],
    depth_extents: Sequence[int] | None = None,
    alpha_threshold: int = 128,
) -> Path:
    validate_key(name, "sprite name")
    if kind not in PALETTE_SIZES:
        raise SpriteError(f"unsupported sprite kind {kind!r}: expected one of {', '.join(PALETTE_SIZES)}")
    if not animations:
        raise SpriteError("at least one animation is required")
    if not (1 <= alpha_threshold <= 255):
        raise SpriteError("alpha_threshold must be between 1 and 255")

    palette_colors = PALETTE_SIZES[kind]

    # First pass: load and validate all frames, and collect all color images for a shared palette.
    loaded_anims: list[dict] = []
    color_images: list[Image.Image] = []
    for idx, spec in enumerate(animations):
        anim_name = validate_key(spec["name"], "animation name")
        plane = spec.get("plane")
        if plane is not None and plane not in ("color", "depth", "light", "pick", "shadow"):
            raise SpriteError(f"animation {anim_name}: unsupported plane {plane!r}")
        frame_paths = [Path(p) for p in spec["frames"]]
        loaded = [(p, load_rgba(p)) for p in frame_paths]
        size = check_frame_sizes(loaded, label=f"animation {anim_name}")
        directions = int(spec.get("directions", 1))
        repeat = bool(spec.get("repeat_directions", False))
        frames_per_dir, frame_map = resolve_frame_layout(len(loaded), directions, repeat, f"animation {anim_name}")
        hotspot = spec.get("hotspot")
        if hotspot is None:
            hotspot = [0, 0] if kind == "ui" else [-(size[0] // 2), -size[1]]
        if len(hotspot) != 2:
            raise SpriteError(f"animation {anim_name}: hotspot must have 2 integers")
        columns = int(spec.get("columns", min(len(loaded), 16)))
        if columns < 1:
            raise SpriteError(f"animation {anim_name}: columns must be >= 1")
        entry = {
            "name": anim_name,
            "plane": plane,
            "loaded": loaded,
            "size": size,
            "directions": directions,
            "frames_per_dir": frames_per_dir,
            "frame_map": frame_map,
            "hotspot": [int(hotspot[0]), int(hotspot[1])],
            "columns": columns,
            "sheet_name": spec.get("sheet_name", f"{anim_name}.png" if idx == 0 else f"{anim_name}_{idx}.png"),
            "depth_frames": None,
        }
        if plane is None or plane == "color":
            color_images.extend(im for _, im in loaded)
        if spec.get("depth_frames"):
            depth_paths = [Path(p) for p in spec["depth_frames"]]
            depth_loaded = [(p, load_rgba(p)) for p in depth_paths]
            check_frame_sizes(depth_loaded, expected=size, label=f"animation {anim_name} depth")
            if len(depth_loaded) != len(loaded):
                raise SpriteError(
                    f"animation {anim_name}: {len(depth_loaded)} depth frames for {len(loaded)} color frames"
                )
            entry["depth_frames"] = depth_loaded
        loaded_anims.append(entry)

    # For building sprites, animation 0 must be the color plane and animation 1 the depth plane.
    if kind == "building":
        first = loaded_anims[0]
        if first["plane"] is None:
            first["plane"] = "color"
        if first["plane"] != "color":
            raise SpriteError("building sprite animation 0 must be the color plane")
        has_explicit_depth = len(loaded_anims) >= 2 and loaded_anims[1]["plane"] == "depth"
        if not has_explicit_depth:
            depth_entry = {
                "name": "depth",
                "plane": "depth",
                "loaded": first["loaded"],
                "size": first["size"],
                "directions": first["directions"],
                "frames_per_dir": first["frames_per_dir"],
                "frame_map": first["frame_map"],
                "hotspot": list(first["hotspot"]),
                "columns": first["columns"],
                "sheet_name": "depth.png",
                "depth_frames": first["depth_frames"],
                "auto_depth": first["depth_frames"] is None,
            }
            loaded_anims.insert(1, depth_entry)
        else:
            depth_entry = loaded_anims[1]
            if depth_entry["size"] != first["size"] or len(depth_entry["loaded"]) != len(first["loaded"]):
                raise SpriteError("building depth plane must match color plane size and frame count")
            depth_entry["color_reference"] = first["loaded"]

    palette, mapping = build_palette(color_images, palette_colors, alpha_threshold)

    out_dir.mkdir(parents=True, exist_ok=True)
    palette_filename = f"{name}.palettes.png"
    write_palette_png(out_dir / palette_filename, palette)

    json_anims: list[dict] = []
    used_sheets: set[str] = set()
    for anim in loaded_anims:
        sheet_file = anim["sheet_name"]
        if sheet_file in used_sheets:
            stem = Path(sheet_file).stem
            suffix = Path(sheet_file).suffix or ".png"
            n = 1
            while f"{stem}_{n}{suffix}" in used_sheets:
                n += 1
            sheet_file = f"{stem}_{n}{suffix}"
        used_sheets.add(sheet_file)
        sheet_rel = f"{name}/{sheet_file}"

        plane = anim["plane"]
        if plane == "depth":
            if anim.get("auto_depth"):
                la_frames = [make_auto_depth_frame(im, alpha_threshold) for _, im in anim["loaded"]]
            elif anim.get("depth_frames"):
                la_frames = [
                    make_custom_depth_frame(d_im, c_im, alpha_threshold)
                    for (_, d_im), (_, c_im) in zip(anim["depth_frames"], anim["loaded"])
                ]
            else:
                color_ref = anim.get("color_reference", anim["loaded"])
                la_frames = [
                    make_custom_depth_frame(d_im, c_im, alpha_threshold)
                    for (_, d_im), (_, c_im) in zip(anim["loaded"], color_ref)
                ]
        elif plane == "light":
            la_frames = [make_light_frame(im, alpha_threshold) for _, im in anim["loaded"]]
        elif plane in ("pick", "shadow"):
            la_frames = [make_mask_frame(im, alpha_threshold) for _, im in anim["loaded"]]
        else:
            la_frames = [index_frame(im, mapping, alpha_threshold) for _, im in anim["loaded"]]

        write_sheet(out_dir / sheet_rel, la_frames, anim["columns"])
        anim_doc: dict = {
            "name": anim["name"],
            "directions": anim["directions"],
            "frames": anim["frames_per_dir"],
            "size": [anim["size"][0], anim["size"][1]],
            "hotspot": anim["hotspot"],
            "sheets": [sheet_rel],
            "columns": anim["columns"],
            "unique_frames": len(la_frames),
        }
        if plane is not None:
            anim_doc["plane"] = plane
        if anim["frame_map"] is not None:
            anim_doc["frame_map"] = anim["frame_map"]
        json_anims.append(anim_doc)

    doc: dict = {
        "format": "obl.sprite",
        "version": 1,
        "name": name,
        "kind": kind,
        "pixels": "indexed",
        "palettes": palette_filename,
        "palette_colors": palette_colors,
        "palette_count": 1,
        "animations": json_anims,
    }
    if kind == "building":
        extents = list(depth_extents) if depth_extents is not None else [255, 255, 255, 255]
        if len(extents) != 4 or any(not (0 <= int(e) <= 255) for e in extents):
            raise SpriteError("depth_extents must be 4 integers in 0..255")
        doc["depth_extents"] = [int(e) for e in extents]

    out_json = out_dir / f"{name}.json"
    write_json(out_json, doc)
    return out_json


def load_spec(spec_path: Path) -> dict:
    if not spec_path.is_file():
        raise SpriteError(f"spec file not found: {spec_path}")
    try:
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SpriteError(f"cannot read spec {spec_path}: {exc}") from exc
    base = spec_path.parent
    for anim in spec.get("animations", []):
        anim["frames"] = [str((base / p).resolve()) if not Path(p).is_absolute() else p for p in anim["frames"]]
        if anim.get("depth_frames"):
            anim["depth_frames"] = [
                str((base / p).resolve()) if not Path(p).is_absolute() else p for p in anim["depth_frames"]
            ]
    return spec


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--spec", type=Path, help="JSON spec file for multi-animation sprites")
    p.add_argument("--name", help="Sprite key (written to <out>/<name>.json)")
    p.add_argument("--kind", choices=list(PALETTE_SIZES), default="building", help="Sprite kind (default: building)")
    p.add_argument("--out", type=Path, required=True, help="Output sprites/ directory")
    p.add_argument("--frames", nargs="+", type=Path, help="Input PNG frame(s) in direction-major order")
    p.add_argument("--anim-name", help="Animation name (default: 'color' for building, else sprite name)")
    p.add_argument("--directions", type=int, default=1, help="Number of directions (default: 1)")
    p.add_argument("--repeat-directions", action="store_true", help="Reuse the input frame(s) across all directions")
    p.add_argument("--hotspot", nargs=2, type=int, metavar=("X", "Y"), help="Hotspot offset in pixels")
    p.add_argument("--columns", type=int, help="Frames per sheet row (default: up to 16)")
    p.add_argument("--depth", nargs="+", type=Path, help="Optional depth PNGs for building sprites")
    p.add_argument("--depth-extents", nargs=4, type=int, metavar=("E0", "E1", "E2", "E3"),
                   help="Four depth_extents bytes (0..255) for building sprites")
    p.add_argument("--lightmap", nargs="+", type=Path, help="Optional night lightmap PNGs for building sprites")
    p.add_argument("--shadow", nargs="+", type=Path, help="Optional shadow mask PNGs for building sprites")
    p.add_argument("--pick", nargs="+", type=Path, help="Optional pick mask PNGs for building sprites")
    p.add_argument("--alpha-threshold", type=int, default=128, help="Alpha threshold for opacity (1..255, default: 128)")
    return p.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.spec:
            spec = load_spec(args.spec)
            out_json = build_sprite(
                name=args.name or spec["name"],
                kind=spec.get("kind", args.kind),
                out_dir=args.out,
                animations=spec["animations"],
                depth_extents=spec.get("depth_extents"),
                alpha_threshold=int(spec.get("alpha_threshold", args.alpha_threshold)),
            )
        else:
            if not args.name:
                raise SpriteError("--name is required without --spec")
            if not args.frames:
                raise SpriteError("--frames is required without --spec")
            anim_name = args.anim_name or ("color" if args.kind == "building" else args.name)
            main_anim: dict = {
                "name": anim_name,
                "frames": args.frames,
                "directions": args.directions,
                "repeat_directions": args.repeat_directions,
            }
            if args.hotspot is not None:
                main_anim["hotspot"] = list(args.hotspot)
            if args.columns is not None:
                main_anim["columns"] = args.columns
            if args.depth:
                main_anim["depth_frames"] = args.depth
            if args.kind == "building":
                main_anim["plane"] = "color"
            animations = [main_anim]
            for opt_name, plane, files in (
                ("lightmap", "light", args.lightmap),
                ("shadow", "shadow", args.shadow),
                ("pick", "pick", args.pick),
            ):
                if files:
                    extra: dict = {
                        "name": opt_name,
                        "plane": plane,
                        "frames": files,
                        "directions": args.directions,
                        "repeat_directions": args.repeat_directions,
                    }
                    if args.hotspot is not None:
                        extra["hotspot"] = list(args.hotspot)
                    if args.columns is not None:
                        extra["columns"] = args.columns
                    animations.append(extra)
            out_json = build_sprite(
                name=args.name,
                kind=args.kind,
                out_dir=args.out,
                animations=animations,
                depth_extents=args.depth_extents,
                alpha_threshold=args.alpha_threshold,
            )
    except SpriteError as exc:
        print(f"make_sprite: {exc}", file=sys.stderr)
        return 1
    print(f"wrote {out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
