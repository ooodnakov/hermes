# Waveshare 349 V1 storage and artwork

`V1Assets` is the V1-only SD/artwork adapter. It mounts one-bit SDMMC on
CLK=GPIO41, CMD=GPIO39, D0=GPIO40. Mount failure never formats the card; mood
frames then come from the compiled fallback.

## SD asset pack

Copy the exporter's `hermes-buddy-349-v1` directory to the FAT32 card root. The
reader uses `/hermes-buddy-349-v1/config.json` and
`/hermes-buddy-349-v1/frames/<mood>/<NNN>.raw4`. It accepts only the V1
`raw4-indexed-640x172` pack with palette size 4 and character region
`(0,24,144,144)`. Each frame must contain exactly 55,040 bytes. The full file is
checked for palette indices, while only the 144x144 art rows are decoded to
RGB565. SD assets therefore keep the exporter's display-sized format, while
firmware fallback data stores only character pixels.

The shared palette is RGB565 `0000, 00C0, 03E0, 57EA`. Values are returned in
caller-owned `uint16_t` storage. Use `FrameInfo::frameCount` and
`FrameInfo::frameMs` to schedule animation; `source` and `sdError` expose
fallback use and why SD could not supply the frame.

## Compiled fallback

Regenerate the V1 character-sized fallback from checked-in previews with:

```sh
uv run --with 'Pillow>=10' python3 scripts/generate_v1_fallback.py
```

The script defaults to paths relative to its checkout, writes only under
`src/storage`, and embeds digests for all preview content and the exporter
implementation in the generated header.

## Configuration provisioning

`mergeConfiguration()` merges top-level JSON sections into
`/hermes-buddy-349-v1/config.json`, preserving keys it does not receive. It
uses a temporary file, verifies the serialized JSON, then renames the existing
file to a backup before replacing it. A backup is restored after a restart if
the final config is absent. A missing card or malformed existing JSON returns
an error and does not replace the existing configuration. No-card operation
continues with compiled artwork; SD-backed provisioning is unavailable until
the card is mounted.
