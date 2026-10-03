# V1 physical gesture check

The host sensor test checks the detector against controlled acceleration samples. This board check separately records gestures emitted by the running firmware; it does not treat an operator confirmation as evidence.

## Current priority: identify the unexpected restarts

The latest 900-second capture on `627c9dd` recorded three restarts, but its JSON-only log discarded boot text, so their cause is unknown. The user confirmed no cable or button handling during that capture. The current firmware exposes `reset_reason` and `reset_reason_code` in hello/diag; its `usb` / code 11 value immediately after upload matches the explicit esptool RTS reset and does not explain those earlier restarts. A subsequent 15-second capture saw no further restart. Await confirmation whether the planned gentle screen-up/screen-down flip was performed. For the next capture, keep USB physically stable and leave the board buttons untouched while recording the raw USB serial output. Save the raw byte stream (`.bin`) and timestamped JSONL sidecar as private evidence outside the checkout:

```sh
python3 scripts/capture_v1_usb_raw.py --port /dev/ttyACM0 --duration 120 --probe --output ~/.local/state/espherm/2026-10-03/v1-usb-reset-120s
```

`--probe` sends familiar-safe `ping` and `touchdiag` requests once per connection so the sidecar can include hello/diag reset-reason fields; the raw byte stream remains authoritative. The default mode sends no serial writes, and neither mode makes DTR/RTS ioctls. Opening the tty through the OS/driver may still affect modem signals. Do not change gesture thresholds or initiate a reboot for this capture. Do not use the gesture script for this step: it clears the serial input buffer and only saves parsed JSON lines.

## Gesture sequence

After the reset cause has been identified, connect the board over USB, confirm its normal image is running, and close any serial monitor or bridge that has the port open. For a separate gesture run, start with the device screen-up and stationary so startup calibration can learn that resting orientation. Install `pyserial` if needed, then run:

```sh
python3 scripts/check_v1_gestures.py --port /dev/ttyACM0
```

Keep the device screen-up and still through startup calibration (about four seconds after starting the script). Follow each prompt in order: turn it screen-down and hold, turn it screen-up and hold, make two firm desk knocks 150–1200 ms apart, give it one brief shake, then leave it untouched and still for at least two minutes before picking it up. The pickup phase allows at least 130 seconds for the still interval and event. The script succeeds only after it reads the expected JSON gesture event for every action. On timeout it exits nonzero and retains the observations already read.

By default, observed JSON lines are saved to `v1-gesture-evidence.jsonl` in the current directory. Use `--output PATH` to choose another location. The expected serial events are `facedown`, `upright`, `tap2`, `shake`, and `pickup`. These checks establish emitted gesture events only; they do not prove approval resolution, audio audibility, gesture comfort across users, long-term sensor reliability, or network reachability.

The check is read-only and opens the serial port at 115200 baud. Do not run it alongside the Familiar bridge or another serial monitor. It needs `pyserial` (`python3 -m pip install pyserial`).
