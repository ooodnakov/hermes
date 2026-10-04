import subprocess
from pathlib import Path


def test_native_v1_storage_firmware_logic():
    runner = Path(__file__).parent / "native_storage" / "run.sh"
    subprocess.run([str(runner)], check=True)
