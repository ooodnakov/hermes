"""Bounds and coverage regressions for the generated Meslo PUA icon atlas."""

from __future__ import annotations

import hashlib
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets/nerd_icons/source/MesloLGSDZNerdFontMono-Regular.ttf"
DATA = ROOT / "src/ui/nerd_icons_data.h"
FONT_SHA256 = "23f523b6c649dfa6df45f92da7e4b34c009756e4acb7106c0f5f771b9cc65b3b"
ENTRY_RE = re.compile(
    r"\{0x([0-9A-F]+)u, (\d+)u, (\d+), (\d+), (\d+), (-?\d+), (-?\d+)\},"
)


def read_entries() -> list[tuple[int, int, int, int, int, int, int]]:
    text = DATA.read_text(encoding="utf-8")
    return [tuple(int(field, 16 if index == 0 else 10) for index, field in enumerate(row)) for row in ENTRY_RE.findall(text)]


class NerdIconsAtlasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.entries = read_entries()

    def test_source_font_is_pinned(self) -> None:
        self.assertEqual(hashlib.sha256(FONT.read_bytes()).hexdigest(), FONT_SHA256)

    def test_full_bmp_and_plane15_pua_coverage(self) -> None:
        codepoints = [entry[0] for entry in self.entries]
        self.assertEqual(len(codepoints), 10397)
        self.assertEqual(codepoints, sorted(set(codepoints)))
        self.assertIn(0xE000, codepoints)
        self.assertIn(0xF6C5, codepoints)
        self.assertIn(0xF0001, codepoints)
        self.assertIn(0xF1AF0, codepoints)
        self.assertTrue(all(0xE000 <= cp <= 0xF8FF or 0xF0000 <= cp <= 0xFFFFD for cp in codepoints))
        self.assertNotIn(0x1F600, codepoints)

    def test_masks_fit_the_twelve_pixel_line_and_advance_limit(self) -> None:
        for codepoint, _offset, width, height, advance, x, y in self.entries:
            with self.subTest(codepoint=hex(codepoint)):
                self.assertLessEqual(width, 13)
                self.assertLessEqual(height, 12)
                self.assertLessEqual(y + height, 12)
                self.assertGreaterEqual(y, 0)
                self.assertGreaterEqual(x, 0)
                self.assertLessEqual(x + width, advance)
                self.assertLessEqual(advance, 13)
                self.assertGreater(advance, 0)

    def test_alpha4_offsets_are_contiguous_and_match_row_stride(self) -> None:
        offset = 0
        for codepoint, actual_offset, width, height, _advance, _x, _y in self.entries:
            with self.subTest(codepoint=hex(codepoint)):
                self.assertEqual(actual_offset, offset)
                offset += ((width + 1) // 2) * height
        text = DATA.read_text(encoding="utf-8")
        pixel_count = int(re.search(r"kPixelBytes = (\d+)", text).group(1))
        self.assertEqual(offset, pixel_count)

    def test_intentional_blank_spacing_glyph_is_preserved(self) -> None:
        blanks = [entry[0] for entry in self.entries if entry[2] == 0 or entry[3] == 0]
        self.assertEqual(blanks, [0xEC03])


if __name__ == "__main__":
    unittest.main()
