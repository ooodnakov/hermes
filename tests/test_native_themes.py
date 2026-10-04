"""Compile and execute the native harness for the production theme palettes."""
from __future__ import annotations

import os
import shlex
import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_native_theme_palettes(tmp_path: Path) -> None:
    configured = os.environ.get("CXX")
    compiler = shlex.split(configured) if configured else None
    if not compiler:
        fallback = shutil.which("c++") or shutil.which("g++")
        if fallback:
            compiler = [fallback]
    if not compiler:
        pytest.fail("A C++ compiler is required for this test (set CXX or install c++/g++).")

    binary = tmp_path / "theme-test"
    command = [
        *compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{ROOT / 'src'}",
        str(ROOT / "tests/native_themes/test_themes.cpp"),
        "-o",
        str(binary),
    ]
    try:
        subprocess.run(command, check=True, capture_output=True, text=True)
        subprocess.run([str(binary)], check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as error:
        pytest.fail(
            f"Native theme harness command failed: {shlex.join(error.cmd)}\n"
            f"stdout:\n{error.stdout}\nstderr:\n{error.stderr}"
        )
