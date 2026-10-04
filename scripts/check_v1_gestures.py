#!/usr/bin/env python3
"""Record observable V1 physical gesture events from the USB JSON line stream."""

import argparse
import json
import sys
import time
from pathlib import Path


GESTURES = ("facedown", "upright", "tap2", "shake", "pickup")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="seconds to wait for each action (default: 30; pickup uses at least 130)")
    parser.add_argument("--output", type=Path, default=Path("v1-gesture-evidence.jsonl"))
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
        return 2

    try:
        connection = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as exc:
        print(f"Cannot open {args.port}: {exc}", file=sys.stderr)
        return 2

    # Ask only for actions whose resulting event is actually read from USB.
    plan = (
        ("facedown", "Start with the device screen-up and still. Turn it screen-down and hold it still."),
        ("upright", "Turn it screen-up again and hold it still."),
        ("tap2", "Make two firm, separate desk knocks 0.15–1.2 seconds apart."),
        ("shake", "Give the device one brief, clear shake."),
        ("pickup", "Set it down untouched and still for at least two minutes, then pick it up."),
    )
    started = time.monotonic()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with args.output.open("w", encoding="utf-8") as log:
            with connection:
                connection.reset_input_buffer()
                print("Listening to device JSON lines. The board must have been rebooted screen-up and stationary.")
                print("Keep it screen-up and still for 4 seconds so the IMU can learn that baseline.")
                time.sleep(4)
                for expected, instruction in plan:
                    print(f"\nAction for {expected}: {instruction}")
                    timeout = max(args.timeout, 130.0) if expected == "pickup" else args.timeout
                    deadline = time.monotonic() + timeout
                    found = False
                    while time.monotonic() < deadline:
                        raw = connection.readline()
                        if not raw:
                            continue
                        line = raw.decode("utf-8", errors="replace").strip()
                        timestamp = time.monotonic() - started
                        try:
                            payload = json.loads(line)
                        except json.JSONDecodeError:
                            continue
                        log.write(json.dumps({"seconds": round(timestamp, 3), "json": payload},
                                             ensure_ascii=False) + "\n")
                        log.flush()
                        if payload.get("cmd") == "gesture":
                            gesture = payload.get("gesture")
                            if gesture in GESTURES:
                                print(f"Observed {gesture}: {line}")
                                if gesture == expected:
                                    found = True
                                    break
                    if not found:
                        print(f"No {expected} event observed within {timeout:.0f}s; evidence saved to {args.output}",
                              file=sys.stderr)
                        return 1
        print(f"Observed all five expected gesture events. Evidence: {args.output}")
        return 0
    finally:
        connection.close()


if __name__ == "__main__":
    raise SystemExit(main())
