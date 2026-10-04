from pathlib import Path
import subprocess


def test_native_markdown_parser():
    root = Path(__file__).resolve().parents[1]
    subprocess.run([str(root / "tests/native_markdown/run.sh")], cwd=root, check=True)
