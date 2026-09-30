"""Compile and execute the native harness against the production FamiliarUi."""
from __future__ import annotations

import os
import shlex
import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_familiar_ui_native_harness(tmp_path: Path) -> None:
    configured = os.environ.get("CXX")
    compiler = shlex.split(configured) if configured else None
    if not compiler:
        fallback = shutil.which("c++") or shutil.which("g++")
        if fallback:
            compiler = [fallback]
    if not compiler:
        pytest.fail("A C++ compiler is required for this test (set CXX or install c++/g++).")

    binary = tmp_path / "familiar-ui-test"
    command = [
        *compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-sign-compare",
        f"-I{ROOT / 'tests/native_ui/stubs'}",
        f"-I{ROOT / 'src'}",
        str(ROOT / "tests/native_ui/test_familiar_ui.cpp"),
        str(ROOT / "src/ui/familiar_ui.cpp"),
        "-o",
        str(binary),
    ]
    try:
        subprocess.run(command, check=True, capture_output=True, text=True)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as error:
        pytest.fail(
            f"Native FamiliarUi harness command failed: {shlex.join(error.cmd)}\n"
            f"stdout:\n{error.stdout}\nstderr:\n{error.stderr}"
        )

    assert "Familiar UI native regression tests passed" in result.stdout
