#!/usr/bin/env python3
"""Assemble a local Waveshare 3.49 V1 firmware and SD asset release bundle."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENV = "waveshare_esp32_s3_touch_lcd_349_v1"
BUILD = ROOT / ".pio" / "build" / ENV
ASSET_DIR = "hermes-buddy-349-v1"
EXPECTED_FRAME_BYTES = 640 * 172 // 2
FLASH_LAYOUT = [
    {"offset": "0x00000000", "artifact": "bootloader.bin"},
    {"offset": "0x00008000", "artifact": "partitions.bin"},
    {"offset": "0x0000e000", "artifact": "boot_app0.bin"},
    {"offset": "0x00010000", "artifact": "hermes-familiar-waveshare-349-v1.bin"},
]


def run(args: list[str], *, cwd: Path = ROOT) -> str:
    result = subprocess.run(args, cwd=cwd, check=True, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return result.stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_expected_hash(actual: str, expected: str | None) -> None:
    if expected and actual.lower() != expected.lower():
        raise ValueError(f"firmware SHA-256 mismatch: {actual}")


def validate_flash_artifacts(artifacts: dict[str, Path]) -> None:
    expected = {"bootloader.bin": 20256, "partitions.bin": 0x0C00,
                "boot_app0.bin": 0x2000, "hermes-familiar-waveshare-349-v1.bin": None}
    for name, size in expected.items():
        path = artifacts[name]
        if not path.is_file() or (size is not None and path.stat().st_size != size):
            raise ValueError(f"missing or unexpected V1 flash artifact: {name}")


def check_output_dir(out: Path) -> None:
    if out == ROOT or ROOT in out.parents:
        try:
            relative = out.relative_to(ROOT)
        except ValueError:
            relative = Path("..")
        if not relative.parts or relative.parts[0] != "dist":
            raise ValueError("in-checkout release output must be under ignored dist/")
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        raise FileExistsError(f"release output directory must be new or empty: {out}")


def validate_asset_config(config: dict) -> None:
    allowed = {"name", "version", "format", "layout", "profile", "width", "height",
               "character_region", "bottom_band_px", "palette_size", "palette_index_max",
               "palette_rgb565", "animations", "notes"}
    if set(config) != allowed:
        raise ValueError("SD config contains missing or unapproved fields")
    if config.get("profile") != "349-v1" or (config.get("width"), config.get("height")) != (640, 172):
        raise ValueError("generated SD config is not the 640x172 349-v1 profile")


def git_state() -> dict:
    head = run(["git", "rev-parse", "HEAD"])
    branch = run(["git", "branch", "--show-current"])
    rows = run(["git", "status", "--porcelain=v1", "--untracked-files=all"])
    status_counts = {"modified": 0, "added": 0, "deleted": 0, "untracked": 0, "other": 0}
    for row in rows.splitlines():
        code = row[:2]
        if code == "??":
            status_counts["untracked"] += 1
        elif "D" in code:
            status_counts["deleted"] += 1
        elif "A" in code:
            status_counts["added"] += 1
        elif "M" in code or "R" in code or "C" in code:
            status_counts["modified"] += 1
        else:
            status_counts["other"] += 1
    return {
        "commit": head,
        "branch": branch,
        "dirty": bool(rows),
        "status_counts": status_counts,
    }


def pio_versions(pio: str) -> dict:
    core = run([pio, "--version"])
    report = run([pio, "pkg", "list", "-e", ENV])
    platform = re.search(r"^Platform\s+([^@]+)\s+@\s+([^\s]+)", report, re.M)
    if not platform:
        raise RuntimeError("PlatformIO package report did not include the selected platform")
    packages = {}
    for name, version in re.findall(r"^[├└]──\s+([^@]+)\s+@\s+([^\s]+)", report, re.M):
        packages[name.strip()] = version.strip()
    return {"platformio_core": core, "platform": platform.group(2), "packages": packages}


def validate_and_copy_assets(source: Path, destination: Path) -> dict:
    sys.path.insert(0, str(ROOT / "scripts"))
    import export_sd_pack as exporter

    config_path = source / "config.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    validate_asset_config(config)
    if config.get("palette_size") != 4:
        raise ValueError("unexpected V1 palette size")
    frame_records = []
    for state, animation in config.get("animations", {}).items():
        for name in animation.get("frames", []):
            path = source / "frames" / state / name
            raw = path.read_bytes()
            exporter.validate_raw4(raw, 640, 172, 4)
            if len(raw) != EXPECTED_FRAME_BYTES:
                raise ValueError(f"unexpected frame size: {state}/{name}")
            frame_records.append({
                "path": f"frames/{state}/{name}",
                "size_bytes": len(raw),
                "sha256": sha256(path),
            })
    if len(frame_records) != 20:
        raise ValueError(f"expected 20 V1 frames, found {len(frame_records)}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination)
    (destination / "README.txt").write_text(
        "Hermes Familiar 3.49 V1 SD asset pack\n"
        "Copy this hermes-buddy-349-v1 folder to the root of a FAT32 SD card.\n"
        "Frames are 640x172 raw4, two pixels per byte; palette indices are 0-3.\n",
        encoding="utf-8")
    return {
        "directory": f"sdcard/{ASSET_DIR}",
        "profile": "349-v1",
        "format": config["format"],
        "width": 640,
        "height": 172,
        "palette_size": 4,
        "frame_count": len(frame_records),
        "frame_bytes": EXPECTED_FRAME_BYTES,
        "config_sha256": sha256(config_path),
        "frames": frame_records,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=ROOT / "dist" / "waveshare-349-v1",
                    help="new or empty destination directory (default: dist/waveshare-349-v1)")
    ap.add_argument("--pio", default="pio", help="PlatformIO Core executable")
    ap.add_argument("--expected-firmware-sha256",
                    help="fail unless the V1 build image matches this known SHA-256")
    ap.add_argument("--previously-verified-flash-sha256",
                    help="record an earlier firmware image whose upload hash was verified")
    args = ap.parse_args()
    out = args.out.expanduser().resolve()
    check_output_dir(out)

    firmware = BUILD / "firmware.bin"
    bootloader = BUILD / "bootloader.bin"
    partitions = BUILD / "partitions.bin"
    for path in (firmware, bootloader, partitions):
        if not path.is_file():
            raise FileNotFoundError(f"missing V1 build artifact: {path.name}; build {ENV} first")
    firmware_hash = sha256(firmware)
    check_expected_hash(firmware_hash, args.expected_firmware_sha256)

    system_info = run([args.pio, "system", "info"])
    core_dir_match = re.search(r"^PlatformIO Core Directory\s+(.+)$", system_info, re.M)
    if not core_dir_match:
        raise RuntimeError("PlatformIO did not report its Core Directory")
    boot_app0 = Path(core_dir_match.group(1).strip()) / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"
    flash_artifacts = {
        "bootloader.bin": bootloader,
        "partitions.bin": partitions,
        "boot_app0.bin": boot_app0,
        "hermes-familiar-waveshare-349-v1.bin": firmware,
    }
    validate_flash_artifacts(flash_artifacts)

    with tempfile.TemporaryDirectory(prefix="espherm-v1-assets-") as temporary:
        temp = Path(temporary)
        generated_assets = temp / ASSET_DIR
        previews = temp / "previews"
        run([sys.executable, str(ROOT / "scripts" / "export_sd_pack.py"),
             "--profile", "349-v1", "--out", str(generated_assets), "--preview", str(previews)])

        out.mkdir(parents=True, exist_ok=True)
        firmware_out = out / "firmware"
        firmware_out.mkdir()
        named_firmware = firmware_out / "hermes-familiar-waveshare-349-v1.bin"
        shutil.copy2(firmware, named_firmware)
        shutil.copy2(bootloader, firmware_out / "bootloader.bin")
        shutil.copy2(partitions, firmware_out / "partitions.bin")
        shutil.copy2(boot_app0, firmware_out / "boot_app0.bin")
        asset_info = validate_and_copy_assets(generated_assets, out / "sdcard" / ASSET_DIR)

    versions = pio_versions(args.pio)
    source = git_state()
    readme = f"""Hermes Familiar — Waveshare ESP32-S3 Touch LCD 3.49 V1

This package is only for the 3.49 V1 board. It is not compatible with the
original 2.8-inch board. Firmware SHA-256: {firmware_hash}

Firmware artifacts are in firmware/. The SD card asset folder is
sdcard/{ASSET_DIR}/; copy that folder to the root of a FAT32 SD card.
Verify files using manifest.json before copying or flashing. Component binary
offsets are recorded there; direct standalone esptool flashing has not been
exercised for this bundle.

To build and upload from the source checkout, select the V1 environment and
the serial port that you have identified for this board:

  pio run -e {ENV} -t upload --upload-port <PORT>

USB serial runs at 115200 baud. This command rebuilds from the source checkout
before uploading; it does not write the packaged binary directly. Select the
verified serial port for this board. Do not select the 2.8-inch environment.

Known limits: the battery voltage ADC has not been calibrated against a
reference. Audio-ready diagnostics do not establish normal-volume speech
audibility. See the repository's 349 V1 handoff for current hardware evidence.

Pin map: QSPI display CS/CLK/D0-D3 = GPIO 9/10/11-14, reset GPIO 21,
backlight GPIO 8; touch I2C SDA/SCL = GPIO 17/18; peripheral I2C SDA/SCL =
GPIO 47/48; SD_MMC CLK/CMD/D0 = GPIO 41/39/40; ES8311 audio MCLK/BCLK/LRCK/DOUT
= GPIO 7/15/46/45; battery ADC GPIO 4; power key GPIO 16; boot key GPIO 0.

Build source commit and dirty-state counts, toolchain versions, asset metadata,
and per-file checksums are in manifest.json.
"""
    (out / "README.txt").write_text(readme, encoding="utf-8")
    files = []
    for path in sorted(p for p in out.rglob("*") if p.is_file()):
        if path.name == "manifest.json":
            continue
        files.append({"path": path.relative_to(out).as_posix(),
                      "size_bytes": path.stat().st_size, "sha256": sha256(path)})
    manifest = {
        "format_version": 1,
        "profile": "waveshare-esp32-s3-touch-lcd-349-v1",
        "not_for": "waveshare-esp32-s3-touch-lcd-2.8",
        "source": source,
        "build_environment": ENV,
        "toolchain": versions,
        "firmware": {
            "path": f"firmware/{named_firmware.name}",
            "size_bytes": named_firmware.stat().st_size,
            "sha256": firmware_hash,
            "flash_mode": "qio",
            "flash_size": "16MB",
            "flash_frequency_mhz": 80,
            "upload_speed": 115200,
            "psram": "8MB OPI",
        },
        "previously_verified_flash_sha256": args.previously_verified_flash_sha256,
        "firmware_origin": "existing .pio build artifact; packaging does not rebuild",
        "flash_layout": FLASH_LAYOUT,
        "sd_assets": asset_info,
        "files": files,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"release package: {out}")
    print(f"firmware SHA-256: {firmware_hash}")
    print(f"asset frames: {asset_info['frame_count']} x {asset_info['frame_bytes']} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
