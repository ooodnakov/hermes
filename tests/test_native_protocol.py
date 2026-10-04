from pathlib import Path
import subprocess

import pytest


def test_native_ui_state_markdown_parser():
    root = Path(__file__).resolve().parents[1]
    arduinojson = root / ".pio/libdeps/waveshare_esp32_s3_touch_lcd_349_v1/ArduinoJson/src/ArduinoJson.h"
    if not arduinojson.exists():
        pytest.skip("build the V1 PlatformIO environment to provide pinned ArduinoJson headers")
    runner = Path(__file__).parent / "native_protocol" / "run.sh"
    subprocess.run([str(runner)], check=True)
