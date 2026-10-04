# Cloud sensor timing and recovery result

## Scope and result

This task reviewed only the V1 sensor module and its native harness. Two production defects were reproduced and fixed:

- Slightly late scheduler calls moved every sensor's next sample later, allowing persistent caller jitter to reduce the effective sampling rate. The scheduler now retains its cadence across `millis()` rollover, while a delay of two or more intervals resets the phase so one `update()` call never triggers a catch-up burst.
- If the first RTC STOP-control write failed and the first cleanup write also failed, `setRtcDateTime()` made no second attempt to restore the caller's original control byte. Cleanup now uses the same bounded two-attempt behavior as the later restart paths.

The existing production behavior for missing IMU samples and readiness recovery was validated rather than changed. A failed sample clears acceleration/readiness, cancels partial gesture qualification, and the next good sample restores readiness. A device missed during the boot probe is rediscovered and reconfigured by the bounded recovery path without reinitializing the shared bus.

Native coverage now checks:

- battery, RTC, and IMU cadence with late calls, `uint32_t` rollover, and a long scheduler pause;
- missing-at-boot IMU discovery, configuration recovery, and the first subsequent acceleration sample;
- transient missing IMU samples and gesture-state cancellation (existing regressions retained);
- RTC read failure timestamps, invalid BCD, oscillator STOP, 12/24-hour decoding, impossible calendar dates, strict write validation, short reads/writes, calendar verification, restart verification, originally stopped clocks, partial-calendar latching, and repeated cleanup failure.

## Software evidence

- `tests/native_sensors/run.sh`: passed.
- `python3 -m pytest tests/test_native_sensors.py -q`: 1 passed.
- `python3 -m pytest tests/ -q`: 112 passed, 4 skipped, and 20,807 subtests passed after the V1 build supplied ArduinoJson headers. An earlier pre-build run had 110 passed, 5 skipped, and one expected native-storage failure because those generated headers were absent.
- `uvx --from platformio platformio run -e waveshare_esp32_s3_touch_lcd_349_v1`: passed; the known nonfatal `esp_idf_size --ng` warning was emitted.
- `uvx --from platformio platformio run -e waveshare_esp32_s3_touch_lcd_349_v1_diagnostic -e waveshare_esp32_s3_touch_lcd_28`: both environments passed; existing dependency deprecation warnings and the diagnostic environment's known nonfatal size-helper warning were emitted.
- `git diff --check`: passed.

Context7 documentation tooling was not available in this environment, so no Context7 result is claimed. The implementation relies on the repository's pinned framework behavior and existing PCF85063A/QMI handling.

## Physical checks still required

No board was available to this cloud task. The native tests and firmware builds do not establish gesture calibration, gesture amplitude suitability, RTC oscillator accuracy, RTC retention after power loss, battery accuracy, or sustained board stability. They also do not explain the historical restarts. On-device follow-up should record sample cadence around a real uptime rollover or controlled clock seam where practical, remove/reconnect the IMU or inject bus faults without disturbing wiring, exercise RTC read/write failures and confirm STOP cleanup, and verify recovery diagnostics. Expander P1, wiring, orientation mapping, gesture thresholds, and battery calibration were intentionally unchanged.
