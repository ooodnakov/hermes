# Nerd Font icon atlas

`source/MesloLGSDZNerdFontMono-Regular.ttf` is the single pinned input used to
generate the monochrome PUA atlas. It is Nerd Fonts 3.4.0, has SHA256
`23f523b6c649dfa6df45f92da7e4b34c009756e4acb7106c0f5f771b9cc65b3b`, and is
the regular style copied from the user's Meslo Nerd Font installation. Only
this style is needed because the UI colors mask coverage with the text color.

The generated atlas contains every mapped codepoint in the Basic Multilingual
Plane Private Use Area (U+E000–U+F8FF) and Supplementary Private Use Area-A
(U+F0000–U+FFFFD): 3,501 and 6,896 codepoints respectively. It omits ordinary
letters, symbols and emoji from the font. Each glyph is rasterized to at most
13×12 pixels for the existing 12 px line box, then stored as a packed 4-bit
coverage mask. The line's baseline reference is 9 px below its top; each
bitmap's `yOffset` is already relative to the line top and keeps all ink inside
that box. Horizontal side bearings are normalized so every mask stays inside
its reported advance, including Powerline glyphs with negative source bearings.

Regenerate the committed C++ table with the pinned tooling:

```sh
uv run --with pillow==12.3.0 --with fonttools==4.61.1 python scripts/generate_nerd_icons.py
```

The generator checks the source font SHA256, Pillow and FontTools versions,
and the FreeType version bundled by the Pillow wheel (2.14.3). The ordinary
firmware build consumes the generated header and does not run Python or fetch
font tooling.

Meslo LG is identified as Apache-2.0 in the Nerd Fonts source catalog; the
Nerd Fonts additions are covered by the Nerd Fonts SIL Open Font License 1.1.
The corresponding license texts are included in `licenses/`. See the [Nerd
Fonts v3.4.0 license](https://github.com/ryanoasis/nerd-fonts/blob/v3.4.0/LICENSE)
and [font catalog](https://github.com/ryanoasis/nerd-fonts/blob/v3.4.0/bin/scripts/lib/fonts.json).
