# Handoff: Hermes Familiar on Waveshare ESP32-S3-Touch-LCD-3.49 V1

Current as of 2026-09-30, after the touch/framing milestones. Read this before continuing in a new chat; it supersedes older progress snapshots in the migration plan where they disagree.

## Checkout and user preferences

- Checkout: `/home/user/Projects/espherm`, branch `board/esp32-s3-lcd-349-v1`, based on Hermes commit `9d69262`.
- `origin` is the user's fork `ooodnakov/hermes`; `upstream` is `webdevtodayjason/hermes`.
- Work is committed locally: `d93cb31` preserves the initial migration, `ad5cc78` fixes stale touch state after read errors, and `9241344` hardens USB framing. Nothing has been pushed; there is no PR. Commits used command-local Codex author identity because Git user identity was unset.
- The user asked that implementation work be delegated to Luna subagents to conserve limits. Do not take over coding in the root chat; use Luna for implementation and keep status reports concise. The user explicitly said `rtk` is unnecessary.
- The user reported that the new portrait/avatar is visible and animated, and later that touch “seems to work.” Treat touch as promising but incompletely calibrated; no full corner matrix or extended test was recorded.

## What is implemented

`platformio.ini` now has three environments: the original 2.8-inch board, the primary 3.49 V1 familiar, and a separate V1 diagnostic image. The V1 environment pins pioarduino 55.03.30 / Arduino ESP32 3.3.0, 16 MB flash, OPI PSRAM, and `BOARD_HAS_PSRAM`. Keep the 2.8 environment isolated.

The V1 familiar source now brings up TCA9554 P6 power hold, AXS15231B QSPI display, GPIO8 active-low backlight, AXS touch on `Wire` GPIO17/18, and the peripheral bus on `Wire1` GPIO47/48. The panel uses its board-specific short initialization sequence, 640 × 172 landscape canvas, PSRAM frame buffers, and sequential DMA chunks. Touch uses the known working 0x08/8-byte packet, separate `Pressed`/`Released`/`Error` outcomes, and a two-empty-sample release rule. UI approval decisions require a live host and a nonempty active approval ID.

The seven-page UI, JSON protocol parser, 349 V1 SD/raw4 loader, and compact compiled fallback are integrated. The V1 pack contains 20 animation frames, 640 × 172 raw4, with a 144 × 144 character area. Storage validates pack metadata, dimensions, bytes, and palette indices; failed or absent SD assets fall back to compiled art without formatting the card. The V1 pack was generated, validated, copied to the user-provided FAT32 card, and verified as the runtime source for sleep/idle/thinking/waiting; the earlier boot used compiled fallback artwork because the V1 config was missing.

Host portability work is also present: Linux speech conversion uses FFmpeg while macOS retains `afconvert`; the bridge launcher no longer contains an author-specific interpreter path; `HERMES_ADVERTISED_HOST` can override WSL's route-derived address. The revised V1 ES8311 firmware reports `audio=ready`, and the user confirmed quiet idle. Gain `0x60` and `0x80` tests completed in software but were inaudible. One 200 ms `audiotest` at vendor-normalized maximum register value `0xBA` was audible; the user described it as a “mid volume beep.” Register readback was 186, the baseline was restored to `0x60`, codec mute and P7 LOW succeeded, and there were no I2S errors. This confirms a brief beep only; full PCM/HTTP playback and normal tap volume remain unvalidated. That audio milestone used firmware SHA-256 `d733378cdc4bd334ab51dd9dc1559cc141251a9501ab3e4dc7a8c6abe9df8fa6`; the current image is recorded in the continuation evidence below. The first audio image produced continuous white noise; a quiet safety image was flashed afterward before the revised candidate. WSL-to-board HTTP/TCP reachability remains unverified, and Wi-Fi/network features are not enabled in the current V1 familiar milestone.

Battery/RTC/IMU support is in `src/peripherals/sensors.cpp`. The sensor module borrows the already initialized peripheral bus. Its GPIO4 ADC setup primes the Arduino 3.3 ADC channel before setting attenuation; the first sample is discarded. QMI reset uses the verified `RST_RESULT` handshake. Do not set TCA9554 P1 speculatively: the Waveshare schematic labels it `BL_EN`, while the known-working `rsvpnano` project uses P1 as an active-low battery ADC gate. The display has worked without P1 changes.

## Evidence collected

- The pre-migration flash was read as a full 16 MB image, verified against a second read of its first 64 KB, and stored privately outside the checkout. Recovery steps and its hash are in the host-only `RECOVERY.txt`; never add that image or hardware identifiers to Git.
- Both the 2.8 environment and V1 diagnostic build passed. The integrated V1 familiar build also passed and was flashed with an esptool-verified hash. A USB serial `hello` and `diag` exchange confirmed the running familiar image and diagnostic path; rely on the live identity rather than assuming a local build artifact is flashed.
- Runtime diagnostics reported ESP32-S3 rev 0.2, 16 MB flash, 8 MB PSRAM, a working external canvas and frame buffer, TCA9554 hold, display readiness, touch-controller presence, IMU readiness, and RTC presence with invalid time. The roughly 4.11 V ADC reading is unvalidated and is not a verified battery measurement.
- The user confirmed that the corrected backlight and panel initialization produces a clear, bright image with an animated avatar. Touch “seems to work,” but there is no recorded four-corner coordinate capture or exhaustive release/stability run.
- Host tests: 54 passed. Both asset profiles exported 20 frames; the generated compact V1 fallback matched all 20 V1 SD character crops byte for byte. Linux PCM conversion was exercised with a real local audio clip. PIO builds reported a nonfatal bundled `esp_idf_size --ng` warning, but completed successfully.
- Host-page rows open a modal for the selected content, allow vertical scrolling, and return to the originating page on dismissal; physical review of all page content remains incomplete.
- The V1 SD export was generated locally and checked: 20 frames, each 55,040 bytes, with metadata matching the firmware loader. It has now been copied to the FAT32 card with all 22 files hash-verified. Live diagnostics confirm SD-backed sleep/idle/thinking/waiting with expected frame counts and no asset errors; physical appearance remains awaiting user confirmation.
- Sensor evidence remains limited: the RTC is present but time is invalid; the IMU is detected, but axes and gestures have not been captured and validated. The roughly 4.11 V ADC value is not calibrated against a reference. Do not change TCA9554 P1; its role remains disputed.
- Current WSL/Windows inspection found no configured Familiar network transport or active listener on the audio/TCP/WebSocket ports. The advertised-host override is unset; WSL route detection is not proof of a LAN-reachable ESP32 address. No network settings were changed. The board's Wi-Fi/HTTP path remains unsupported and unverified.
- USB protocol smoke replay covered state, event, notify, deck, messages/history, host pages, transient page, and permission frames, followed by clear/ping. Ack/config/say were reviewed offline, but no live Hermes deck action or approval was exercised.

## Remaining work, in order

1. Reconnect/identify the ESP32 USB device in WSL if it has detached. Use `usbipd list`; the ESP32-S3 USB JTAG device has VID:PID `303A:1001`, but its BUSID can change. A reset/unplug can detach it from WSL.
2. USB `hello`/`diag` has been confirmed for the current image. Repeat the identity/diagnostic check after any reflash or reconnect; if the user is available, capture all four touch corners and review the seven tabs. Preserve the current orientation unless those points prove it wrong.
3. The V1 pack is installed and runtime-verified at `/hermes-buddy-349-v1/`. Obtain the user’s visual confirmation; absent/corrupt-card and combined-workload checks remain pending.
4. Set the RTC only from a trusted current clock, then validate retention; capture IMU axes/gestures and compare battery voltage with a reference. Battery presence, divider gating, and USB-only behavior remain open.
5. Test a harmless deck action, messages, notifications, approval ALLOW/DENY, reconnects, and the real Hermes gateway. Confirm invalid/stale touch samples never resolve approvals.
6. Validate full PCM playback, normal tap volume, and HTTP audio after the confirmed brief beep. Then validate Wi-Fi/HTTP speech, TCP, BLE, and WSL-to-board reachability; these network features remain unsupported in the V1 familiar build.
7. Run sustained and failure/power checks and refresh documentation and firmware checksums. Commit each verified milestone as requested by the user; publishing remains separate. Local milestone commits exist; no push or PR has been made.

The detailed 38-task backlog is in [MIGRATION_ESP32_S3_TOUCH_LCD_349_V1.md](MIGRATION_ESP32_S3_TOUCH_LCD_349_V1.md). Start by reading this handoff plus the current working tree; do not redo the completed discovery or initial bring-up.

## Continuation evidence: touch and USB framing

- Two Luna agents implemented the focused changes. Touch errors now invalidate the retained press, cached point, and release debounce state. This prevents a later empty packet from recreating a stale press. The native regression fails against checkpoint `d93cb31` and passes against the fix; coordinate mapping and two-empty release behavior are preserved.
- USB framing is a dependency-free bounded accumulator with the existing 4,096-byte buffer (4,095 payload bytes). Overflow discards through newline; embedded NUL invalidates the whole line instead of executing a JSON prefix. Native coverage exercises fragmentation, multiple frames, CRLF/empty lines, exact capacity boundaries, overflow recovery, and NUL recovery.
- The full host suite now passes 56 tests, including native C++ harnesses. Host requirements are Python with pytest, pyserial, websockets, and a C++17 compiler (`CXX` or c++/g++). A reproducible runner is `uv run --with pytest --with pyserial --with websockets python -m pytest tests/ -q`; the touch harness can also run through `tests/native_touch/run.sh`. Bash syntax and whitespace checks passed. Context7 was attempted but returned tool unavailable.
- All three PlatformIO environments built successfully after the fixes. V1 familiar size: 54,224 / 327,680 bytes RAM and 1,536,555 / 13,631,488 bytes flash. The known nonfatal size-helper `--ng` warning remains. These were builds in the existing checkout, not clean-checkout/CI validation.
- The familiar image from source commit `9241344` was uploaded with esptool hash verification. Current firmware SHA-256: `0ae9d86104a035453b5a414f148192ad136585027993e638871d9951d273b4f1`. Touch error injection is native-test evidence; physical corner calibration and approvals remain unvalidated.
- Live USB checks on the flashed image passed: a ping prefix followed by embedded NUL returned `invalid_line` with no ping acknowledgement; a 4,096-byte oversized payload plus ping-looking suffix returned `line_too_long` with no ping acknowledgement; a subsequent fragmented CRLF ping returned ack/hello/diag. Diagnostics again reported display/canvas/SD ready, compiled fallback, RTC invalid, IMU/audio ready, and network unsupported.

## SD provisioning evidence (2026-09-30)

- The user supplied a FAT32 card via a Windows drive. The V1 pack was copied into `/hermes-buddy-349-v1/`; unrelated card contents were preserved and no existing V1 configuration was present. All 22 copied files matched source SHA-256 hashes. All 20 frame payloads were checked for 55,040-byte lengths and palette indices below 4; the copied config identifies profile `349-v1`.
- FAT32 refused Unix metadata copying, so provisioning used content-only copying and then verified every file. Writes were synchronized and the WSL mount was removed. The user subsequently returned the card to the board. Runtime validation now passes; visual confirmation remains pending.

## SD runtime evidence (2026-09-30)

- After the user returned the card, live hello/diag confirmed the familiar image, display/canvas/SD ready, `face_asset_source=sd`, and no SD/frame errors.
- Harmless USB state replay exercised sleep (3 frames), idle (1), thinking (4), and waiting (2). Each mood ran for 2.4 seconds before diagnostics confirmed the expected frame count, SD source, no asset errors, and display/canvas readiness. This confirms runtime loading, not a photographed or user-confirmed appearance. The temporary state was cleared afterward; no approval or deck action was submitted.
- RTC time remains invalid; IMU/audio readiness remains reported. Physical sensor calibration, full playback, network parity, SD failure cases, and sustained-load testing are still pending.
