# Waveshare 3.49 V1 local release bundle

This guide covers only Waveshare ESP32-S3-Touch-LCD-3.49 V1. The
Waveshare 2.8 factory image and `waveshare_esp32_s3_touch_lcd_28` build remain
separate and must not be substituted for this profile.

## Current image record — 2026-10-04

The latest operator image is `hermes-familiar-waveshare-349-v1.bin`,
5,127,824 bytes, SHA-256
`93c7444b1d2541f2468db452b7a9dae86d023d78e96b40e7e2e5938f75f4147b`.
The uploaded data hash matched this value. Its local package is
`dist/waveshare-349-v1-alert-preroll-20261004/`; `manifest.json` has hashes and
sizes for the firmware, boot components, 20 SD frames, and notices. It records
source commit `61ee9ebe8177d7804c5d850cd935be673c36a3ad` and that the source
tree had changes when built. Therefore the commit identifies the base source,
not a clean-tree reproduction of this exact binary.

Build toolchain recorded in that package: PlatformIO Core 6.2.0, pioarduino
platform 55.03.30, Arduino ESP32 3.3.0, ESP-IDF 5.5.0, Xtensa toolchain
14.2.0+20241119, ArduinoJson 7.4.2, LovyanGFX 1.2.30, and NimBLE-Arduino
2.5.1. V1 memory report: 95,180 / 327,680 bytes RAM and 5,127,419 /
13,631,488 bytes flash. The source workflow also emits a GitHub Actions
artifact named `hermes-familiar-waveshare-349-v1-release`, containing the full
V1 release package. Record the corresponding run result after it completes.

The private 16 MB pre-migration flash backup and its restore instructions are
kept outside the repository and are not in this release package. Never use
that legacy recovery image as a V1 image. The bundle's component offsets are
provided for inspection, but standalone flashing of those packaged files has
not been validated; use the named PlatformIO V1 environment to rebuild and
upload. Preserve the user's existing SD configuration and artwork when
provisioning: copy/merge only the required V1 asset files and do not format the
card or replace a private `config.json` with the generated example.

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
The application image must be nonempty; fixed-size bootloader, partition-table,
and OTA initializer components are checked against the V1 upload layout.

The manifest records the firmware and each packaged file's size and SHA-256,
the Git commit and dirty-state counts, installed PlatformIO packages, V1 asset
metadata, and upload component offsets. Dirty status records counts only; it
does not list local paths. No credentials or user SD-card network settings are
packaged. The generated asset config is checked against a static V1-only schema.

The SD asset folder is `sdcard/hermes-buddy-349-v1/`. Copy that folder to the
root of a FAT32 SD card. Its raw4 frames are 640 × 172, two palette indices per
byte, with 20 frames of 55,040 bytes each. The 2.8-inch profile remains a
separate 240 × 320 pack and firmware environment.

The runtime UI has eight pages in this order: FACE (avatar/status/latest
response), MSGS (two message cards, retained detail reader and history), OPS
(six action slots and approval controls), FLEET, CRON, NET, DEV, and SET
(Phosphor/Amber/Ocean/Paper/Gruvbox, mute, animation, and brightness). V1
settings are stored in `ui` under
`/hermes-buddy-349-v1/config.json`; with no writable card they apply only for
the current session. See the README for user behavior and the integration
guide for host protocol details.

To build and upload from the source checkout, use the V1 environment and the
serial port identified for the connected board:

```sh
pio run -e waveshare_esp32_s3_touch_lcd_349_v1 -t upload --upload-port <PORT>
```

This command rebuilds from the checkout. Identify the V1 serial port on the
current host first (commonly `/dev/ttyACM*` on Linux/WSL or
`/dev/cu.usbmodem*` on macOS), and ensure WSL has the ESP32 USB device attached
if using WSL. The bundle contains the prebuilt application, bootloader,
partition table, and 8 KiB OTA initializer with these
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

If V1 firmware needs recovery, rebuild and upload with the command above after
checking that the selected port is the 3.49 V1. If the bootloader or board
cannot start, use the operator's private recovery instructions for that
specific board; no full-flash recovery image is distributed here. Do not erase
the SD card during firmware recovery. The three restarts observed on an older,
superseded image remain unexplained and are not resolved by the current image
record.

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

The battery voltage ADC is not calibrated against a reference. RTC power-loss
retention and long-term accuracy, full motion calibration, two-hour combined
workload, overnight idle, and the complete failure/power matrix were not run;
the user accepted these as unperformed at closeout. The user reports current
speech and general UI/sound function, but that does not supply those missing
measurements. Read the
[V1 handoff](HANDOFF_WAVESHARE_349_V1.md) for dated build and hardware evidence;
keep recovery images and private hardware evidence outside the repository.
