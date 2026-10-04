import subprocess
from pathlib import Path


def test_v1_ble_transport_native() -> None:
    root = Path(__file__).resolve().parents[1]
    subprocess.run(["bash", str(root / "tests/native_ble/run.sh")], check=True)
