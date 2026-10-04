# V1 physical gesture check

This procedure is retained as diagnostic documentation. At migration closeout
on 2026-10-04, the user accepted the unperformed extended gesture, workload,
and failure/power checks as limitations; no new physical run is implied by
the checked migration tasks. Use this procedure only when physical gesture
evidence is requested again. Earlier reset causes described below remain
unknown.

The host sensor test checks the detector against controlled acceleration samples. This board check separately records gestures emitted by the running firmware; it does not treat an operator confirmation as evidence.

## Current priority: resolve the silent serial capture

The earlier 900-second capture on `627c9dd` recorded three restarts whose cause remains unknown; its JSON-only log discarded boot text. A later operator-confirmed gentle flip kept the display on, and touch changed pages, but the face appeared static. Its 180-second raw capture (`gentle-flip-0ea8187-01`) saved 5,577 bytes, all received by 0.151 s. The initial pickup event at 0.0025 s may have been buffered before capture. Two diagnostics reported uptime 1,867,537/1,867,553 ms and `reset_reason=usb` / code 11 from the same boot, with IMU errors at zero and screen-up +Z values. No disconnect or uptime rollback was observed, but the lack of later USB receive means this is not evidence of sustained uptime or a completed gesture check. The initial writer sent unknown `cmd=diag` before correction to `touchdiag`; neither the later probe sequence nor a separate 15-second reopen capture received replies. No reset or reflash occurred during this continuation.

The capture utility opens a raw tty and makes no explicit DTR/RTS ioctl. This firmware selects Arduino `HWCDC` USB Serial/JTAG; inspection of the selected core path found no DTR-triggered reset handling. That does not explain the missing later bytes, and no tool defect has been demonstrated. Physical checks are paused while the operator is away. Resume with one controlled serial session before any further flips: close other port users, start one reader, then send a single `ping` and one `touchdiag`, keeping USB stable and buttons untouched. Save raw bytes and the timestamped JSONL sidecar outside the checkout. Do not change gesture thresholds or claim the older restarts are explained.

The firmware reports the immutable `reset_reason` and `reset_reason_code` in hello/diag. Its `usb` / code 11 value matches the explicit esptool RTS reset used for upload; it does not explain the three older restarts. The files `gentle-flip-0ea8187-01.bin` / `.jsonl`, `gentle-flip-0ea8187-reopen.bin` / `.jsonl`, and `gentle-flip-summary.json` are private evidence under `~/.local/state/espherm/2026-10-03/`.

```sh
python3 scripts/capture_v1_usb_raw.py --port /dev/ttyACM0 --duration 120 --probe --output ~/.local/state/espherm/2026-10-03/v1-usb-controlled-01
```

The capture tool refuses to overwrite existing output files; choose a new base name for each run. `--probe` sends familiar-safe `ping` and `touchdiag` requests once per connection so the sidecar can include hello/diag reset-reason fields; the raw byte stream remains authoritative. The default mode sends no serial writes, and neither mode makes DTR/RTS ioctls. Opening the tty through the OS/driver may still affect modem signals. Do not change gesture thresholds or initiate a reboot for this capture. Do not use the gesture script for this step: it clears the serial input buffer and only saves parsed JSON lines.

## Gesture sequence

After the reset cause has been identified, connect the board over USB, confirm its normal image is running, and close any serial monitor or bridge that has the port open. For a separate gesture run, start with the device screen-up and stationary so startup calibration can learn that resting orientation. Install `pyserial` if needed, then run:

```sh
python3 scripts/check_v1_gestures.py --port /dev/ttyACM0
```

Keep the device screen-up and still through startup calibration (about four seconds after starting the script). Follow each prompt in order: turn it screen-down and hold, turn it screen-up and hold, make two firm desk knocks 150–1200 ms apart, give it one brief shake, then leave it untouched and still for at least two minutes before picking it up. The pickup phase allows at least 130 seconds for the still interval and event. The script succeeds only after it reads the expected JSON gesture event for every action. On timeout it exits nonzero and retains the observations already read.

By default, observed JSON lines are saved to `v1-gesture-evidence.jsonl` in the current directory. Use `--output PATH` to choose another location. The expected serial events are `facedown`, `upright`, `tap2`, `shake`, and `pickup`. These checks establish emitted gesture events only; they do not prove approval resolution, audio audibility, gesture comfort across users, long-term sensor reliability, or network reachability.

The check is read-only and opens the serial port at 115200 baud. Do not run it alongside the Familiar bridge or another serial monitor. It needs `pyserial` (`python3 -m pip install pyserial`).
