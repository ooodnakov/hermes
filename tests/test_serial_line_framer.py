import os
import pathlib
import shlex
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


def test_serial_line_framer_native(tmp_path):
    executable = tmp_path / "serial_line_framer_test"
    subprocess.run(
        [
            *shlex.split(os.environ.get("CXX", "c++")),
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT / 'src'}",
            str(ROOT / "tests/native/test_serial_line_framer.cpp"),
            "-o",
            str(executable),
        ],
        check=True,
        capture_output=True,
        cwd=ROOT,
    )
    subprocess.run([str(executable)], check=True, capture_output=True, cwd=ROOT)
