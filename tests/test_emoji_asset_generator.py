from __future__ import annotations

import importlib.util
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "generate_emoji_assets", ROOT / "scripts/generate_emoji_assets.py"
)
assert SPEC and SPEC.loader
generator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = generator
SPEC.loader.exec_module(generator)


def test_asset_key_removes_only_emoji_presentation_selector() -> None:
    assert generator.normalize_asset_key("2764-fe0f.png") == "❤".encode()
    assert generator.normalize_asset_key("2764-fe0e.png") == "❤︎".encode()
    assert generator.normalize_asset_key("1f469-200d-1f4bb.png") == "👩\u200d💻".encode()


def test_asset_key_rejects_non_codepoint_names() -> None:
    with pytest.raises(ValueError):
        generator.normalize_asset_key("../2764.png")
    with pytest.raises(ValueError):
        generator.normalize_asset_key("d800.png")


def test_alpha4_is_row_major_low_nibble_first() -> None:
    assert generator.pack_alpha4([0, 15, 7, 8]) == bytes([0xF0, 0x87])
    with pytest.raises(ValueError):
        generator.pack_alpha4([1])
    with pytest.raises(ValueError):
        generator.pack_alpha4([16, 0])


def test_rgb565_keeps_primary_colors_and_black() -> None:
    assert generator.rgb565(0, 0, 0) == 0x0000
    assert generator.rgb565(255, 0, 0) == 0xF800
    assert generator.rgb565(0, 255, 0) == 0x07E0
    assert generator.rgb565(0, 0, 255) == 0x001F
    assert generator.rgb565(255, 255, 255) == 0xFFFF


def test_synthetic_atlas_emits_bounded_sorted_index_and_stable_metadata() -> None:
    image = bytes(generator.IMAGE_SIZE * generator.IMAGE_SIZE * 2)
    alpha = bytes(generator.IMAGE_SIZE * generator.IMAGE_SIZE // 2)
    emojis = [
        generator.Emoji("🚀".encode(), image, alpha, "1f680.png"),
        generator.Emoji("😀".encode(), image, alpha, "1f600.png"),
    ]
    # Input order may be arbitrary; production extraction sorts before rendering.
    emojis.sort(key=lambda item: item.key)
    source, manifest = generator.render_cpp(emojis)
    assert "constexpr uint16_t kEmojiCount = 2;" in source
    assert manifest["bitmap"]["rgb565_bytes"] == 2 * 12 * 12 * 2
    assert manifest["bitmap"]["alpha4_bytes"] == 2 * 12 * 12 // 2
    assert manifest["lookup"]["record_count"] == 2
    assert manifest["lookup"]["key_bytes"] == len("🚀😀".encode())
    assert generator.sha256(source.encode()) == manifest["generated_cpp_sha256"]


def test_pinned_png_conversion_is_transparent_and_12px() -> None:
    pil = pytest.importorskip("PIL.Image")
    from PIL import ImageDraw
    import PIL

    if PIL.__version__ != generator.EXPECTED_PILLOW:
        pytest.skip(f"regeneration check pins Pillow {generator.EXPECTED_PILLOW}")
    image = pil.new("RGBA", (72, 72), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rectangle((27, 27, 44, 44), fill=(255, 0, 0, 255))
    buffer = __import__("io").BytesIO()
    image.save(buffer, format="PNG")
    pixels, alpha4 = generator.convert_png(buffer.getvalue(), PIL.__version__)
    assert len(pixels) == 12 * 12 * 2
    assert len(alpha4) == 12 * 12 // 2
    alpha_nibbles = [n for byte in alpha4 for n in (byte & 0x0F, byte >> 4)]
    assert min(alpha_nibbles) == 0
    assert max(alpha_nibbles) == 15
