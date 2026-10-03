from pathlib import Path
import subprocess


def test_speech_frame_url_compatibility(tmp_path):
    root = Path(__file__).resolve().parents[1]
    source = root / "tests/native/test_speech_dispatch.cpp"
    binary = tmp_path / "speech-dispatch-test"
    subprocess.run(
        ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
