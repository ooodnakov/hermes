# Waveshare 3.49 V1 local release bundle

Build and package the V1 artifacts from the checkout:

```sh
pio run -e waveshare_esp32_s3_touch_lcd_349_v1
python3 scripts/package_v1_release.py --expected-firmware-sha256 <sha256>
```

The script writes to the ignored `dist/waveshare-349-v1/` directory by default.
It reads the V1 firmware, bootloader, and partition table from that PlatformIO
build, gets `boot_app0.bin` from the selected Arduino framework package, and
regenerates the SD pack from checked-in `assets/sd_preview/` images. It refuses
nonempty output directories and rejects an unexpected firmware hash. Use
`--previously-verified-flash-sha256 <sha256>` to preserve the identity of an
earlier image whose upload hash was verified. Python Pillow, listed in
`requirements-host.txt`, is required for the asset export.

The manifest records the firmware and each packaged file's size and SHA-256,
the Git commit and dirty-state counts, installed PlatformIO packages, V1 asset
metadata, and upload component offsets. Dirty status records counts only; it
does not list local paths. No credentials or user SD-card network settings are
packaged. The generated asset config is checked against a static V1-only schema.

The SD asset folder is `sdcard/hermes-buddy-349-v1/`. Copy that folder to the
root of a FAT32 SD card. Its raw4 frames are 640 × 172, two palette indices per
byte, with 20 frames of 55,040 bytes each. The 2.8-inch profile remains a
separate 240 × 320 pack and firmware environment.

To build and upload from the source checkout, use the V1 environment and the
serial port identified for the connected board:

```sh
pio run -e waveshare_esp32_s3_touch_lcd_349_v1 -t upload --upload-port <PORT>
```

This command rebuilds from the checkout. The bundle contains the prebuilt
application, bootloader, partition table, and 8 KiB OTA initializer with these
offsets from the verified upload log:

| Offset | Component |
| --- | --- |
| `0x00000000` | `bootloader.bin` |
| `0x00008000` | `partitions.bin` |
| `0x0000e000` | `boot_app0.bin` |
| `0x00010000` | V1 application image |

Direct standalone flashing of the packaged components has not been exercised.
The configured upload speed is 115200 baud; the board uses QIO flash, 16 MB
flash, and 8 MB OPI PSRAM.

The pin map follows `src/boards/v1/board_config.h`:

| Function | Pins |
| --- | --- |
| QSPI display CS, CLK, D0–D3 | GPIO 9, 10, 11–14 |
| Display reset, backlight | GPIO 21, 8 |
| Touch I²C SDA, SCL | GPIO 17, 18 |
| Peripheral I²C SDA, SCL | GPIO 47, 48 |
| SD_MMC CLK, CMD, D0 | GPIO 41, 39, 40 |
| ES8311 MCLK, BCLK, LRCK, DOUT | GPIO 7, 15, 46, 45 |
| Battery ADC; power key; boot key | GPIO 4; 16; 0 |

The battery voltage ADC is not calibrated against a reference. Audio-ready
diagnostics do not establish speech audibility at normal volume. Read the
[V1 handoff](HANDOFF_WAVESHARE_349_V1.md) for dated build and hardware evidence;
keep recovery images and private hardware evidence outside the repository.
