"""Compile and run the native harness against the production touch driver."""
from __future__ import annotations

import os
import shlex
import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_axs15231b_touch_native_harness(tmp_path: Path) -> None:
    configured_compiler = os.environ.get("CXX")
    compiler = shlex.split(configured_compiler) if configured_compiler else None
    if not compiler:
        fallback = shutil.which("c++") or shutil.which("g++")
        if fallback:
            compiler = [fallback]
    if not compiler:
        pytest.fail("A C++ compiler is required for this test (set CXX or install c++/g++).")

    binary = tmp_path / "axs15231b-touch-test"
    command = [
        *compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{ROOT / 'tests/native_touch/stubs'}",
        f"-I{ROOT / 'src'}",
        str(ROOT / "tests/native_touch/test_axs15231b_touch.cpp"),
        str(ROOT / "src/input/axs15231b_touch.cpp"),
        "-o",
        str(binary),
    ]
    try:
        subprocess.run(command, check=True, capture_output=True, text=True)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as error:
        pytest.fail(
            f"Native touch harness command failed: {shlex.join(error.cmd)}\n"
            f"stdout:\n{error.stdout}\nstderr:\n{error.stderr}"
        )

    assert "AXS15231B native touch tests passed" in result.stdout
