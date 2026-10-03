import importlib.util
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT / "scripts" / "package_v1_release.py"
spec = importlib.util.spec_from_file_location("package_v1_release", SCRIPT_PATH)
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


def test_release_hash_guard_accepts_matching_hash_and_rejects_mismatch():
    package.check_expected_hash("ab12", "AB12")
    with pytest.raises(ValueError, match="SHA-256 mismatch"):
        package.check_expected_hash("ab12", "cd34")


def test_release_output_must_be_empty_and_stay_out_of_source_tree(tmp_path):
    package.check_output_dir(tmp_path / "new-release")
    nonempty = tmp_path / "existing"
    nonempty.mkdir()
    (nonempty / "keep.txt").write_text("keep")
    with pytest.raises(FileExistsError):
        package.check_output_dir(nonempty)
    with pytest.raises(ValueError, match="ignored dist"):
        package.check_output_dir(ROOT / "scripts" / "unsafe-output")


def test_v1_asset_config_accepts_only_static_board_asset_schema():
    config = json.loads((ROOT / "sdcard/hermes-buddy-349-v1/config.json").read_text())
    package.validate_asset_config(config)
    unsafe = dict(config, transport={"token": "example"})
    with pytest.raises(ValueError, match="unapproved fields"):
        package.validate_asset_config(unsafe)
    wrong_profile = dict(config, profile="legacy")
    with pytest.raises(ValueError, match="not the 640x172 349-v1 profile"):
        package.validate_asset_config(wrong_profile)


def test_flash_artifact_sizes_match_the_verified_v1_upload_layout(tmp_path):
    sizes = {
        "bootloader.bin": 20256,
        "partitions.bin": 3072,
        "boot_app0.bin": 8192,
        "hermes-familiar-waveshare-349-v1.bin": 1,
    }
    artifacts = {}
    for name, size in sizes.items():
        path = tmp_path / name
        path.write_bytes(b"0" * size)
        artifacts[name] = path
    package.validate_flash_artifacts(artifacts)
    artifacts["boot_app0.bin"].unlink()
    with pytest.raises(ValueError, match="boot_app0.bin"):
        package.validate_flash_artifacts(artifacts)
