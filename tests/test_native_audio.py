from pathlib import Path
import subprocess


def test_native_pcm16_decoder():
    root = Path(__file__).resolve().parents[1]
    subprocess.run([str(root / "tests/native_audio/run.sh")], cwd=root, check=True)
