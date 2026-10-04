import subprocess
from pathlib import Path


def test_native_sensor_firmware_logic():
    runner = Path(__file__).parent / "native_sensors" / "run.sh"
    subprocess.run([str(runner)], check=True)
