# V1 Unicode emoji assets

The V1 display uses Twemoji graphics from [`jdecked/twemoji`](https://github.com/jdecked/twemoji), pinned to release `v17.0.3` at commit `b6b55fef1e8636b540a6d016a4729ca8cdf2e60b`. The source archive URL, SHA-256, generated atlas digests, dimensions, and sequence-index bounds are recorded in [`manifest.json`](manifest.json).

Twemoji graphics are licensed under [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/). Attribution: “Twemoji graphics by Twitter, Inc. and other contributors.” These graphics were adapted by downsampling the upstream 72×72 PNGs to 12×12 RGB565 color pixels with packed A4 alpha. The complete license text is in [`LICENSE-GRAPHICS.txt`](LICENSE-GRAPHICS.txt).

To regenerate the checked-in atlas, install no global dependencies; run the pinned Pillow version through `uv` and explicitly choose either a cached archive or the downloader:

```sh
uv run --with Pillow==12.3.0 python scripts/generate_emoji_assets.py --archive /path/to/twemoji-v17.0.3.tar.gz
uv run --with Pillow==12.3.0 python scripts/generate_emoji_assets.py --download
```

The generator validates the pinned archive SHA-256, source image dimensions, UTF-8 sequence keys, and generated atlas bounds. `--check` compares regenerated source and manifest bytes without modifying files. The output ships in firmware; rendering requires no network, filesystem, or external emoji font.
