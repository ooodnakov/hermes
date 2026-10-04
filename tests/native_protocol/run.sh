#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${CXX:-c++}"
arduinojson="$root/.pio/libdeps/waveshare_esp32_s3_touch_lcd_349_v1/ArduinoJson/src"
if [[ ! -f "$arduinojson/ArduinoJson.h" ]]; then
  echo "ArduinoJson is required; build the V1 PlatformIO environment first." >&2
  exit 2
fi
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT

"$compiler" -std=c++17 -Wall -Wextra -Werror \
  -I"$root/tests/native_protocol/stubs" -I"$arduinojson" -I"$root/src" \
  "$root/tests/native_protocol/test_ui_state.cpp" \
  "$root/src/protocol/ui_state.cpp" \
  -o "$binary"
"$binary"
