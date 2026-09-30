#!/usr/bin/env python3
"""Export Hermes Familiar PNGs into an SD-card asset pack.

Layout choice C: preserve the square portrait as a 240x240-ish character panel,
place it in the top portrait area, and reserve the bottom 68px for live firmware
terminal text. Output frames are 240x320 raw4: 4-bit indexed, two pixels/byte.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from PIL import Image, ImageEnhance, ImageFilter

W, H = 240, 320
BOTTOM_BAND = 68
PORTRAIT_H = H - BOTTOM_BAND
# RGB preview palette. Firmware uses equivalent RGB565 palette.
PAL = [
    (0, 0, 0),       # 0 off/black
    (0, 24, 0),      # 1 dim phosphor
    (0, 160, 0),     # 2 active phosphor
    (88, 255, 83),   # 3 hot phosphor
]

STATE_MAP = {
    "idle": ["idle_00.png"],
    "blink": ["blink_00.png", "blink_01.png", "idle_00.png"],
    "wink": ["wink_00.png", "idle_00.png"],
    "smile": ["smile_00.png", "idle_00.png"],
    "happy": ["happy_00.png", "happy_01.png", "idle_00.png"],
    "sleep": ["sleep_00.png", "sleep_01.png", "sleep_02.png"],
    "thinking": ["thinking_00.png", "thinking_01.png", "thinking_02.png", "thinking_03.png"],
    "waiting": ["waiting_00.png", "waiting_01.png"],
}

V1_W, V1_H = 640, 172
V1_CHARACTER_SIZE = (144, 144)
V1_CHARACTER_Y = 24


def fit_v1_character(src: Image.Image) -> Image.Image:
    """Put a square character crop in the left 172px of the V1 landscape panel."""
    src = src.convert("RGB")
    # Existing checked-in previews are 240x320 with the face above the black
    # live-console band. Crop the upper square, then scale it to the character
    # region; no unavailable original square artwork is assumed.
    side = min(src.width, src.height)
    left = (src.width - side) // 2
    top = 0
    src = src.crop((left, top, left + side, top + side))
    src = src.resize(V1_CHARACTER_SIZE, Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (V1_W, V1_H), (0, 0, 0))
    canvas.paste(src, (0, V1_CHARACTER_Y))
    return canvas


def fit_square_portrait(src: Image.Image) -> Image.Image:
    """Fit square source into top portrait area without covering bottom band."""
    src = src.convert("RGB")
    # Use most of the portrait zone, preserving full headphones/hair.
    target = 238
    src.thumbnail((target, target), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    x = (W - src.width) // 2
    # Slightly high so chin/shoulders do not enter bottom live console.
    y = 6
    canvas.paste(src, (x, y))
    return canvas


def terminal_quantize(im: Image.Image, sleep_dim: bool = False, *, size=(W, H), bottom_band=BOTTOM_BAND) -> Image.Image:
    im = ImageEnhance.Contrast(im).enhance(1.25)
    im = ImageEnhance.Sharpness(im).enhance(1.25)
    im = im.filter(ImageFilter.UnsharpMask(radius=0.7, percent=115, threshold=2))
    px = im.load()
    width, height = size
    out = Image.new("P", size)
    flat = []
    for c in PAL:
        flat.extend(c)
    flat += [0, 0, 0] * (256 - len(PAL))
    out.putpalette(flat)
    opx = out.load()
    for y in range(height):
        # Bottom band is live firmware territory; keep asset black there.
        if y >= height - bottom_band:
            for x in range(width):
                opx[x, y] = 0
            continue
        scan = 0.75 if (y % 3 == 2) else (0.90 if (y % 3 == 1) else 1.0)
        for x in range(width):
            r, g, b = px[x, y]
            # Source art is already green, but some antialiasing may include RGB.
            # Favor green while still preserving bright linework.
            lum = max(g, int((r + g + b) / 3)) * scan
            # Stable ordered dither; avoids muddy gradients on the low-bit LCD.
            jitter = ((x * 13 + y * 7) & 15) - 7
            v = lum + jitter
            if v < 22:
                idx = 0
            elif v < 72:
                idx = 1
            elif v < 172:
                idx = 2
            else:
                idx = 3
            if sleep_dim and idx > 1:
                idx -= 1
            opx[x, y] = idx
    return out


def pack4(im: Image.Image) -> bytes:
    pix = im.load()
    width, height = im.size
    if width % 2:
        raise ValueError("raw4 frame width must be even")
    data = bytearray()
    for y in range(height):
        for x in range(0, width, 2):
            data.append(((pix[x, y] & 0x0F) << 4) | (pix[x + 1, y] & 0x0F))
    return bytes(data)


def validate_raw4(data: bytes, width: int, height: int, palette_size: int = len(PAL)) -> None:
    """Reject truncated/oversized frames and indices outside the active palette."""
    expected = width * height // 2
    if width <= 0 or height <= 0 or width % 2:
        raise ValueError("raw4 dimensions must be positive with even width")
    if len(data) != expected:
        raise ValueError(f"raw4 size {len(data)} does not match {width}x{height} ({expected} bytes)")
    if not 1 <= palette_size <= 16:
        raise ValueError("raw4 palette size must be between 1 and 16")
    for byte in data:
        if (byte >> 4) >= palette_size or (byte & 0x0F) >= palette_size:
            raise ValueError("raw4 frame contains an index outside the active palette")


def export_state(src_dir: Path, out_root: Path, preview_root: Path, state: str, files: list[str], *, profile="legacy") -> list[str]:
    out_dir = out_root / "frames" / state
    prev_dir = preview_root / state
    out_dir.mkdir(parents=True, exist_ok=True)
    prev_dir.mkdir(parents=True, exist_ok=True)
    exported = []
    for i, name in enumerate(files):
        src_path = src_dir / name
        if not src_path.exists():
            raise FileNotFoundError(src_path)
        with Image.open(src_path) as source:
            im = fit_v1_character(source) if profile == "349-v1" else fit_square_portrait(source)
        q = terminal_quantize(im, sleep_dim=(state == "sleep"),
                              size=(V1_W, V1_H) if profile == "349-v1" else (W, H),
                              bottom_band=0 if profile == "349-v1" else BOTTOM_BAND)
        raw_name = f"{i:03d}.raw4"
        raw = pack4(q)
        validate_raw4(raw, *q.size, len(PAL))
        (out_dir / raw_name).write_bytes(raw)
        q.convert("RGB").save(prev_dir / f"{i:03d}.png")
        exported.append(raw_name)
    return exported


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=None,
                    help="PNG source directory (defaults to checked-in assets/sd_preview)")
    ap.add_argument("--profile", choices=("legacy", "349-v1"), default="legacy",
                    help="legacy preserves the 2.8-inch 240x320 pack; 349-v1 emits 640x172 frames")
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--preview", type=Path, default=None,
                    help="preview output directory (V1 defaults outside the checkout)")
    args = ap.parse_args()

    src_dir = args.src or Path("assets/sd_preview")
    if args.out is None:
        args.out = Path("sdcard/hermes-buddy" if args.profile == "legacy" else "sdcard/hermes-buddy-349-v1")
    if args.preview is None:
        args.preview = (Path("assets/sd_preview_export") if args.profile == "legacy"
                        else Path("/tmp/hermes-buddy-349-v1-preview"))
    frame_size = (V1_W, V1_H) if args.profile == "349-v1" else (W, H)
    width, height = frame_size

    checked_previews = args.src is None
    if checked_previews and args.preview.resolve() == Path("assets/sd_preview").resolve():
        raise ValueError("preview output cannot overwrite the checked-in preview source")

    args.out.mkdir(parents=True, exist_ok=True)
    args.preview.mkdir(parents=True, exist_ok=True)
    (args.out / "frames").mkdir(exist_ok=True)
    (args.out / "sounds").mkdir(exist_ok=True)
    (args.out / "logs").mkdir(exist_ok=True)

    animations = {}
    for state, files in STATE_MAP.items():
        # Checked-in previews use state/NNN.png; retain original source naming
        # when a caller explicitly supplies the old artwork directory.
        names = ([f"{i:03d}.png" for i in range(len(files))]
                 if checked_previews else files)
        state_src_dir = src_dir / state if checked_previews else src_dir
        frames = export_state(state_src_dir, args.out, args.preview, state, names, profile=args.profile)
        animations[state] = {"frames": frames, "frame_ms": 180 if state not in {"sleep", "thinking"} else (700 if state == "sleep" else 220)}

    config = {
        "name": "Hermes Familiar",
        "version": 1,
        "format": f"raw4-indexed-{width}x{height}",
        "layout": "349-v1-character-left-live-ui-right" if args.profile == "349-v1" else "option-c-square-portrait-bottom-console",
        "profile": args.profile,
        "width": width,
        "height": height,
        "character_region": {"x": 0, "y": V1_CHARACTER_Y, "width": V1_CHARACTER_SIZE[0], "height": V1_CHARACTER_SIZE[1]} if args.profile == "349-v1" else {"x": 0, "y": 0, "width": 240, "height": PORTRAIT_H},
        "bottom_band_px": 0 if args.profile == "349-v1" else BOTTOM_BAND,
        "palette_size": len(PAL),
        "palette_index_max": len(PAL) - 1,
        "palette_rgb565": ["0x0000", "0x00C0", "0x03E0", "0x57EA"],
        "animations": animations,
        "notes": "V1 character-region source is derived from checked-in 240x320 previews; original square artwork is unavailable." if args.profile == "349-v1" else "Generated from square source frames; bottom console is intentionally black for live firmware text."
    }
    (args.out / "config.json").write_text(json.dumps(config, indent=2))
    (args.out / "README.txt").write_text(
        "Hermes Familiar SD asset pack\n"
        "Copy this hermes-buddy folder to the root of the FAT32 SD card.\n"
        f"Frames are {width}x{height} raw4, two pixels per byte.\n"
        f"Profile: {args.profile}. Active palette entries: {len(PAL)}.\n"
    )
    print(f"wrote SD pack: {args.out}")
    print(f"wrote previews: {args.preview}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
