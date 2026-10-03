#!/usr/bin/env python3
"""Build the checked-in 12 px V1 Twemoji atlas from a pinned upstream archive.

The source is jdecked/twemoji v17.0.3.  Use ``--download`` to fetch its pinned
GitHub archive, or ``--archive PATH`` to reuse a local copy.  Both routes verify
the same SHA-256 before reading any files.  Pillow must be exactly 12.3.0; run
with ``uv run --with Pillow==12.3.0 python scripts/generate_emoji_assets.py``.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import sys
import tarfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
SOURCE_TAG = "v17.0.3"
SOURCE_COMMIT = "b6b55fef1e8636b540a6d016a4729ca8cdf2e60b"
ARCHIVE_SHA256 = "705d79de1460e5e775f362f0d0f01fbe3ef8d65bf4648c490e4649704584f747"
ARCHIVE_URL = (
    "https://github.com/jdecked/twemoji/archive/"
    f"{SOURCE_COMMIT}.tar.gz"
)
EXPECTED_PILLOW = "12.3.0"
IMAGE_SIZE = 12
ADVANCE = 13
ARCHIVE_PREFIX = f"twemoji-{SOURCE_COMMIT}/assets/72x72/"
PNG_MEMBER = re.compile(r"^[0-9a-f]+(?:-[0-9a-f]+)*\.png$")


@dataclass(frozen=True)
class Emoji:
    key: bytes
    pixels: bytes
    alpha4: bytes
    source_name: str


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalize_asset_key(filename: str) -> bytes:
    """Convert Twemoji's codepoint filename to UTF-8, ignoring VS16 only."""
    stem = Path(filename).stem
    if not PNG_MEMBER.fullmatch(filename):
        raise ValueError(f"invalid Twemoji PNG filename: {filename!r}")
    codepoints = [int(part, 16) for part in stem.split("-")]
    if any(cp > 0x10FFFF or 0xD800 <= cp <= 0xDFFF for cp in codepoints):
        raise ValueError(f"invalid Unicode scalar in {filename!r}")
    return "".join(chr(cp) for cp in codepoints if cp != 0xFE0F).encode("utf-8")


def pack_alpha4(values: Iterable[int]) -> bytes:
    """Pack row-major 0..15 alpha values, low nibble first."""
    items = list(values)
    if len(items) % 2:
        raise ValueError("A4 pixels must have even length")
    output = bytearray(len(items) // 2)
    for i in range(0, len(items), 2):
        lo, hi = items[i], items[i + 1]
        if not (0 <= lo <= 15 and 0 <= hi <= 15):
            raise ValueError("A4 value outside 0..15")
        output[i // 2] = lo | (hi << 4)
    return bytes(output)


def rgb565(r: int, g: int, b: int) -> int:
    r5 = (r * 31 + 127) // 255
    g6 = (g * 63 + 127) // 255
    b5 = (b * 31 + 127) // 255
    return (r5 << 11) | (g6 << 5) | b5


def convert_png(image_bytes: bytes, pillow_version: str) -> tuple[bytes, bytes]:
    if pillow_version != EXPECTED_PILLOW:
        raise RuntimeError(
            f"Pillow {EXPECTED_PILLOW} required for reproducible output; "
            f"found {pillow_version}"
        )
    from PIL import Image

    with Image.open(io.BytesIO(image_bytes)) as source:
        source.load()
        if source.size != (72, 72):
            raise ValueError(f"expected 72x72 Twemoji PNG, got {source.size}")
        rgba = source.convert("RGBA").resize(
            (IMAGE_SIZE, IMAGE_SIZE), Image.Resampling.LANCZOS
        )
        rgb = bytearray()
        alpha = []
        for r, g, b, a in rgba.get_flattened_data():
            value = rgb565(r, g, b)
            rgb.extend((value & 0xFF, value >> 8))
            alpha.append((a * 15 + 127) // 255)
    return bytes(rgb), pack_alpha4(alpha)


def read_source_archive(archive_path: Path, pillow_version: str) -> list[Emoji]:
    data = archive_path.read_bytes()
    digest = sha256(data)
    if digest != ARCHIVE_SHA256:
        raise ValueError(
            f"source archive SHA-256 mismatch: {digest}; expected {ARCHIVE_SHA256}"
        )
    emojis: list[Emoji] = []
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive.getmembers():
            if not member.isfile() or not member.name.startswith(ARCHIVE_PREFIX):
                continue
            name = member.name[len(ARCHIVE_PREFIX) :]
            if "/" in name or not name.endswith(".png"):
                continue
            key = normalize_asset_key(name)
            stream = archive.extractfile(member)
            if stream is None:
                raise ValueError(f"unable to read {member.name}")
            pixels, alpha = convert_png(stream.read(), pillow_version)
            emojis.append(Emoji(key, pixels, alpha, name))
    if not emojis:
        raise ValueError("pinned source archive contains no 72x72 emoji images")
    emojis.sort(key=lambda emoji: emoji.key)
    for previous, current in zip(emojis, emojis[1:]):
        if previous.key == current.key:
            raise ValueError(
                "two source images normalize to the same key: "
                f"{previous.source_name}, {current.source_name}"
            )
    return emojis


def _format_values(values: Iterable[int], width: int, formatter) -> str:
    values = list(values)
    lines = []
    for start in range(0, len(values), width):
        lines.append("  " + ", ".join(formatter(v) for v in values[start : start + width]) + ",")
    return "\n".join(lines)


def render_cpp(emojis: list[Emoji]) -> tuple[str, dict[str, object]]:
    key_blob = bytearray()
    records: list[tuple[int, int, int]] = []
    rgb_bytes = bytearray()
    alpha_blob = bytearray()
    max_codepoints = 0
    max_key_bytes = 0
    for bitmap_index, emoji in enumerate(emojis):
        key = emoji.key
        if not key or len(key) > 255 or len(key_blob) + len(key) > 65535:
            raise ValueError("emoji sequence key table exceeds uint16 bounds")
        if len(emoji.pixels) != IMAGE_SIZE * IMAGE_SIZE * 2:
            raise ValueError(f"invalid RGB565 pixel count for {emoji.source_name}")
        if len(emoji.alpha4) != IMAGE_SIZE * IMAGE_SIZE // 2:
            raise ValueError(f"invalid A4 pixel count for {emoji.source_name}")
        codepoints = len(key.decode("utf-8"))
        max_codepoints = max(max_codepoints, codepoints)
        max_key_bytes = max(max_key_bytes, len(key))
        records.append((len(key_blob), len(key), bitmap_index))
        key_blob.extend(key)
        rgb_bytes.extend(emoji.pixels)
        alpha_blob.extend(emoji.alpha4)

    if len(emojis) > 65535:
        raise ValueError("bitmap index table exceeds uint16 bounds")
    if len(rgb_bytes) != len(emojis) * IMAGE_SIZE * IMAGE_SIZE * 2:
        raise ValueError("RGB565 atlas byte count is inconsistent")
    if len(alpha_blob) != len(emojis) * IMAGE_SIZE * IMAGE_SIZE // 2:
        raise ValueError("A4 atlas byte count is inconsistent")

    source_digest = sha256(bytes(rgb_bytes) + bytes(alpha_blob))
    key_digest = sha256(bytes(key_blob))
    output = [
        "// Generated by scripts/generate_emoji_assets.py; do not edit.",
        f"// Source: jdecked/twemoji {SOURCE_TAG} ({SOURCE_COMMIT}).",
        "namespace {",
        "struct EmojiRecord { uint16_t keyOffset; uint8_t keyLength; uint16_t bitmapIndex; };",
        "static const uint8_t kEmojiKeys[] = {",
        _format_values(key_blob, 16, lambda x: f"0x{x:02X}"),
        "};",
        "static const EmojiRecord kEmojiRecords[] = {",
    ]
    output.extend(
        f"  {{{offset}, {length}, {bitmap}}}," for offset, length, bitmap in records
    )
    output.extend(
        [
            "};",
            "static const uint16_t kEmojiRgb565[] = {",
            _format_values(
                (rgb_bytes[i] | (rgb_bytes[i + 1] << 8) for i in range(0, len(rgb_bytes), 2)),
                12,
                lambda x: f"0x{x:04X}",
            ),
            "};",
            "static const uint8_t kEmojiAlpha4[] = {",
            _format_values(alpha_blob, 16, lambda x: f"0x{x:02X}"),
            "};",
            f"constexpr uint16_t kEmojiCount = {len(emojis)};",
            f"constexpr uint8_t kEmojiMaxCodepoints = {max_codepoints};",
            f"constexpr uint8_t kEmojiMaxKeyBytes = {max_key_bytes};",
            "}  // namespace",
            "",
        ]
    )
    metadata: dict[str, object] = {
        "schema": 1,
        "source": {
            "repository": "https://github.com/jdecked/twemoji",
            "tag": SOURCE_TAG,
            "commit": SOURCE_COMMIT,
            "archive_url": ARCHIVE_URL,
            "archive_sha256": ARCHIVE_SHA256,
            "graphics_license": "CC-BY-4.0",
            "graphics_license_file": "LICENSE-GRAPHICS.txt",
        },
        "generator": {
            "script": "scripts/generate_emoji_assets.py",
            "pillow": EXPECTED_PILLOW,
            "resample": "LANCZOS",
        },
        "bitmap": {
            "width": IMAGE_SIZE,
            "height": IMAGE_SIZE,
            "advance": ADVANCE,
            "rgb565_bytes": len(rgb_bytes),
            "alpha4_bytes": len(alpha_blob),
            "asset_count": len(emojis),
            "rgb565_alpha4_sha256": source_digest,
        },
        "lookup": {
            "key_encoding": "UTF-8, filename codepoints with U+FE0F removed",
            "record_count": len(records),
            "key_bytes": len(key_blob),
            "key_sha256": key_digest,
            "max_codepoints": max_codepoints,
            "max_key_bytes": max_key_bytes,
        },
        "generated_cpp_sha256": sha256("\n".join(output).encode("utf-8")),
    }
    return "\n".join(output), metadata


def pillow_version() -> str:
    try:
        import PIL
    except ImportError as exc:
        raise RuntimeError(
            f"Pillow {EXPECTED_PILLOW} is required; run with uv --with Pillow=={EXPECTED_PILLOW}"
        ) from exc
    return PIL.__version__


def get_archive(args: argparse.Namespace) -> Path:
    if args.archive:
        return args.archive
    if args.download:
        args.cache_dir.mkdir(parents=True, exist_ok=True)
        target = args.cache_dir / f"twemoji-{SOURCE_TAG}-{SOURCE_COMMIT}.tar.gz"
        if not target.exists() or sha256(target.read_bytes()) != ARCHIVE_SHA256:
            with urllib.request.urlopen(ARCHIVE_URL, timeout=60) as response:
                data = response.read()
            digest = sha256(data)
            if digest != ARCHIVE_SHA256:
                raise ValueError(
                    f"downloaded archive SHA-256 mismatch: {digest}; expected {ARCHIVE_SHA256}"
                )
            target.write_bytes(data)
        return target
    raise ValueError("provide --archive PATH or the explicit --download option")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--archive", type=Path, help="cached pinned source archive")
    source.add_argument("--download", action="store_true", help="download pinned archive")
    parser.add_argument(
        "--cache-dir", type=Path, default=Path(".cache/emoji"),
        help="download cache for --download (default: .cache/emoji)",
    )
    parser.add_argument("--output", type=Path, default=ROOT / "src/ui/emoji_assets_data.inc")
    parser.add_argument("--manifest", type=Path, default=ROOT / "assets/emoji/manifest.json")
    parser.add_argument("--check", action="store_true", help="verify outputs without writing")
    args = parser.parse_args(argv)
    try:
        pillow = pillow_version()
        archive = get_archive(args)
        emojis = read_source_archive(archive, pillow)
        source_text, manifest = render_cpp(emojis)
        data = source_text.encode("utf-8")
        manifest["generated_cpp_sha256"] = sha256(data)
        output_manifest = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
        if args.check:
            if not args.output.exists() or args.output.read_bytes() != data:
                raise ValueError(f"generated source differs: {args.output}")
            if not args.manifest.exists() or args.manifest.read_bytes() != output_manifest:
                raise ValueError(f"generated manifest differs: {args.manifest}")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.manifest.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(data)
            args.manifest.write_bytes(output_manifest)
        print(
            f"{len(emojis)} emoji; {len(data)} generated source bytes; "
            f"{manifest['bitmap']['rgb565_bytes'] + manifest['bitmap']['alpha4_bytes']} atlas bytes"
        )
    except (OSError, ValueError, RuntimeError, tarfile.TarError) as exc:
        print(f"emoji asset generation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
