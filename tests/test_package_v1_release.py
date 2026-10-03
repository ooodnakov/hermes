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


def test_release_notices_include_attribution_licenses_manifest_and_hashes(tmp_path):
    notice_groups = package.copy_release_notices(tmp_path)
    files = package.release_file_records(tmp_path)
    records = {entry["path"]: entry for entry in files}
    expected = {
        "notices/twemoji/README.md",
        "notices/twemoji/LICENSE-GRAPHICS.txt",
        "notices/twemoji/manifest.json",
        "notices/nerd_font/NOTICE.md",
        "notices/nerd_font/Apache-2.0.txt",
        "notices/nerd_font/OFL-1.1.txt",
    }
    assert {path for paths in notice_groups.values() for path in paths} == expected
    assert expected <= records.keys()

    twemoji_manifest = json.loads(
        (tmp_path / "notices/twemoji/manifest.json").read_text()
    )
    assert twemoji_manifest["source"]["tag"] == "v17.0.3"
    assert twemoji_manifest["source"]["graphics_license"] == "CC-BY-4.0"
    assert "Creative Commons Attribution 4.0" in (
        tmp_path / "notices/twemoji/README.md"
    ).read_text()
    assert "Apache-2.0" in (tmp_path / "notices/nerd_font/NOTICE.md").read_text()
    assert "SIL Open Font License 1.1" in (
        tmp_path / "notices/nerd_font/NOTICE.md"
    ).read_text()

    for relative in expected:
        path = tmp_path / relative
        assert records[relative]["size_bytes"] == path.stat().st_size
        assert records[relative]["sha256"] == package.sha256(path)
    assert not list(tmp_path.rglob("*.ttf"))
    assert not list(tmp_path.rglob("*.png"))
