# Handoff: Hermes Familiar on Waveshare ESP32-S3-Touch-LCD-3.49 V1

Current as of 2026-09-30. Read this before continuing in a new chat; it supersedes older progress snapshots in the migration plan where they disagree.

## Checkout and user preferences

- Checkout: `/home/user/Projects/espherm`, branch `board/esp32-s3-lcd-349-v1`, based on Hermes commit `9d69262`.
- `origin` is the user's fork `ooodnakov/hermes`; `upstream` is `webdevtodayjason/hermes`.
- Work is uncommitted and has not been pushed. There is no PR.
- The user asked that implementation work be delegated to Luna subagents to conserve limits. Do not take over coding in the root chat; use Luna for implementation and keep status reports concise. The user explicitly said `rtk` is unnecessary.
- The user reported that the new portrait/avatar is visible and animated, and later that touch “seems to work.” Treat touch as promising but incompletely calibrated; no full corner matrix or extended test was recorded.

## What is implemented

`platformio.ini` now has three environments: the original 2.8-inch board, the primary 3.49 V1 familiar, and a separate V1 diagnostic image. The V1 environment pins pioarduino 55.03.30 / Arduino ESP32 3.3.0, 16 MB flash, OPI PSRAM, and `BOARD_HAS_PSRAM`. Keep the 2.8 environment isolated.

The V1 familiar source now brings up TCA9554 P6 power hold, AXS15231B QSPI display, GPIO8 active-low backlight, AXS touch on `Wire` GPIO17/18, and the peripheral bus on `Wire1` GPIO47/48. The panel uses its board-specific short initialization sequence, 640 × 172 landscape canvas, PSRAM frame buffers, and sequential DMA chunks. Touch uses the known working 0x08/8-byte packet, separate `Pressed`/`Released`/`Error` outcomes, and a two-empty-sample release rule. UI approval decisions require a live host and a nonempty active approval ID.

The seven-page UI, JSON protocol parser, 349 V1 SD/raw4 loader, and compact compiled fallback are integrated. The V1 pack contains 20 animation frames, 640 × 172 raw4, with a 144 × 144 character area. Storage validates pack metadata, dimensions, bytes, and palette indices; failed or absent SD assets fall back to compiled art without formatting the card. The V1 pack was generated and validated locally, but has not been copied to the card or verified on the board; the earlier boot used compiled fallback artwork because the V1 config was missing.

Host portability work is also present: Linux speech conversion uses FFmpeg while macOS retains `afconvert`; the bridge launcher no longer contains an author-specific interpreter path; `HERMES_ADVERTISED_HOST` can override WSL's route-derived address. The revised V1 ES8311 firmware reports `audio=ready`, and the user confirmed quiet idle. Gain `0x60` and `0x80` tests completed in software but were inaudible. One 200 ms `audiotest` at vendor-normalized maximum register value `0xBA` was audible; the user described it as a “mid volume beep.” Register readback was 186, the baseline was restored to `0x60`, codec mute and P7 LOW succeeded, and there were no I2S errors. This confirms a brief beep only; full PCM/HTTP playback and normal tap volume remain unvalidated. The current flashed firmware SHA-256 is `d733378cdc4bd334ab51dd9dc1559cc141251a9501ab3e4dc7a8c6abe9df8fa6`. The first audio image produced continuous white noise; a quiet safety image was flashed afterward before the revised candidate. WSL-to-board HTTP/TCP reachability remains unverified, and Wi-Fi/network features are not enabled in the current V1 familiar milestone.

Battery/RTC/IMU support is in `src/peripherals/sensors.cpp`. The sensor module borrows the already initialized peripheral bus. Its GPIO4 ADC setup primes the Arduino 3.3 ADC channel before setting attenuation; the first sample is discarded. QMI reset uses the verified `RST_RESULT` handshake. Do not set TCA9554 P1 speculatively: the Waveshare schematic labels it `BL_EN`, while the known-working `rsvpnano` project uses P1 as an active-low battery ADC gate. The display has worked without P1 changes.

## Evidence collected

- The pre-migration flash was read as a full 16 MB image, verified against a second read of its first 64 KB, and stored privately outside the checkout. Recovery steps and its hash are in the host-only `RECOVERY.txt`; never add that image or hardware identifiers to Git.
- Both the 2.8 environment and V1 diagnostic build passed. The integrated V1 familiar build also passed and was flashed with an esptool-verified hash. A USB serial `hello` and `diag` exchange confirmed the running familiar image and diagnostic path; rely on the live identity rather than assuming a local build artifact is flashed.
- Runtime diagnostics reported ESP32-S3 rev 0.2, 16 MB flash, 8 MB PSRAM, a working external canvas and frame buffer, TCA9554 hold, display readiness, touch-controller presence, IMU readiness, and RTC presence with invalid time. The roughly 4.11 V ADC reading is unvalidated and is not a verified battery measurement.
- The user confirmed that the corrected backlight and panel initialization produces a clear, bright image with an animated avatar. Touch “seems to work,” but there is no recorded four-corner coordinate capture or exhaustive release/stability run.
- Host tests: 54 passed. Both asset profiles exported 20 frames; the generated compact V1 fallback matched all 20 V1 SD character crops byte for byte. Linux PCM conversion was exercised with a real local audio clip. PIO builds reported a nonfatal bundled `esp_idf_size --ng` warning, but completed successfully.
- Host-page rows open a modal for the selected content, allow vertical scrolling, and return to the originating page on dismissal; physical review of all page content remains incomplete.
- The V1 SD export was generated locally and checked: 20 frames, each 55,040 bytes, with metadata matching the firmware loader. It has not been copied to the card and SD-backed animation has not been observed.
- Sensor evidence remains limited: the RTC is present but time is invalid; the IMU is detected, but axes and gestures have not been captured and validated. The roughly 4.11 V ADC value is not calibrated against a reference. Do not change TCA9554 P1; its role remains disputed.
- Current WSL/Windows inspection found no configured Familiar network transport or active listener on the audio/TCP/WebSocket ports. The advertised-host override is unset; WSL route detection is not proof of a LAN-reachable ESP32 address. No network settings were changed. The board's Wi-Fi/HTTP path remains unsupported and unverified.
- USB protocol smoke replay covered state, event, notify, deck, messages/history, host pages, transient page, and permission frames, followed by clear/ping. Ack/config/say were reviewed offline, but no live Hermes deck action or approval was exercised.

## Remaining work, in order

1. Reconnect/identify the ESP32 USB device in WSL if it has detached. Use `usbipd list`; the ESP32-S3 USB JTAG device has VID:PID `303A:1001`, but its BUSID can change. A reset/unplug can detach it from WSL.
2. USB `hello`/`diag` has been confirmed for the current image. Repeat the identity/diagnostic check after any reflash or reconnect; if the user is available, capture all four touch corners and review the seven tabs. Preserve the current orientation unless those points prove it wrong.
3. Confirm the V1 SD pack is copied to `/hermes-buddy-349-v1/` on a FAT32 card and verify SD animations replace the compiled fallback.
4. Set the RTC only from a trusted current clock, then validate retention; capture IMU axes/gestures and compare battery voltage with a reference. Battery presence, divider gating, and USB-only behavior remain open.
5. Test a harmless deck action, messages, notifications, approval ALLOW/DENY, reconnects, and the real Hermes gateway. Confirm invalid/stale touch samples never resolve approvals.
6. Validate full PCM playback, normal tap volume, and HTTP audio after the confirmed brief beep. Then validate Wi-Fi/HTTP speech, TCP, BLE, and WSL-to-board reachability; these network features remain unsupported in the V1 familiar build.
7. Run sustained and failure/power checks, refresh documentation and firmware checksums, and only then consider committing or publishing. No commit, push, or PR has been made.

The detailed 38-task backlog is in [MIGRATION_ESP32_S3_TOUCH_LCD_349_V1.md](MIGRATION_ESP32_S3_TOUCH_LCD_349_V1.md). Start by reading this handoff plus the current working tree; do not redo the completed discovery or initial bring-up.
