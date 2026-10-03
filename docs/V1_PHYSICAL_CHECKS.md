# V1 physical gesture check

The host sensor test checks the detector against controlled acceleration samples. This board check separately records gestures emitted by the running firmware; it does not treat an operator confirmation as evidence.

Connect the board over USB, confirm its normal image is running, and close any serial monitor or bridge that has the port open. Reboot the board while it is screen-up and still so the sensor learns that orientation as its resting baseline. Install `pyserial` if needed, then run:

```sh
python3 scripts/check_v1_gestures.py --port /dev/ttyACM0
```

Keep the device screen-up and still through startup calibration (about four seconds after starting the script). Follow each prompt in order: turn it screen-down and hold, turn it screen-up and hold, make two firm desk knocks 150–1200 ms apart, give it one brief shake, then leave it untouched and still for at least two minutes before picking it up. The pickup phase allows at least 130 seconds for the still interval and event. The script succeeds only after it reads the expected JSON gesture event for every action. On timeout it exits nonzero and retains the observations already read.

By default, observed JSON lines are saved to `v1-gesture-evidence.jsonl` in the current directory. Use `--output PATH` to choose another location. The expected serial events are `facedown`, `upright`, `tap2`, `shake`, and `pickup`. These checks establish emitted gesture events only; they do not prove approval resolution, audio audibility, gesture comfort across users, long-term sensor reliability, or network reachability.

The check is read-only and opens the serial port at 115200 baud. Do not run it alongside the Familiar bridge or another serial monitor. It needs `pyserial` (`python3 -m pip install pyserial`).
