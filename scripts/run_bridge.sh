#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -n "${HERMES_PYTHON:-}" ]]; then
  PY="$HERMES_PYTHON"
elif [[ -x ".venv/bin/python" ]]; then
  PY=".venv/bin/python"
else
  PY="$(command -v python3 || true)"
fi
if [[ -z "$PY" || ! -x "$PY" ]]; then
  echo "Python 3 not found; set HERMES_PYTHON or create .venv/bin/python" >&2
  exit 127
fi
if [[ $# -eq 0 ]]; then
  PORT="${HERMES_SERIAL_PORT:-}"
  if [[ -z "$PORT" ]]; then
    if [[ "$(uname -s)" == "Darwin" ]]; then
      PORT="/dev/cu.usbmodem101"
    else
      PORT="/dev/ttyACM0"
    fi
  fi
  exec "$PY" scripts/hermes_serial_bridge.py --port "$PORT" --interval 1
else
  exec "$PY" scripts/hermes_serial_bridge.py "$@"
fi
