# Cloud audio recovery result

## Scope

This task inspected the V1 ES8311 and HTTP raw-PCM path only. It did not change
codec registers, gain, shared `Wire1` ownership, public audio APIs, or the speech
protocol. Context7 documentation tooling was not available in this environment,
so the review used the pinned Arduino-ESP32 interfaces and repository code.

## Reproduced defect and fix

The HTTP receive loop previously stopped as soon as `HTTPClient::connected()`
became false. A peer is allowed to close after sending its response, while the
underlying client still has unread bytes buffered. In that state, playback
discarded the final PCM bytes and subsequently reported a truncated response.

The receive decision now continues while either the connection is live or bytes
remain buffered, without reading past a known content length. Completion
classification is shared with the native regression tests. Unsupported transfer
or content encodings now stop before taking audio ownership; they no longer run
the otherwise unnecessary output cleanup path.

No other defect was changed. Existing behavior still bounds connect, idle, total
playback, mutex, and I2S-write waits. Quiet mode is checked on every receive-loop
iteration. Every path after audio ownership is acquired still calls output
cleanup, releases the mutex, ends HTTP, records diagnostics, and lets the task
release speech ownership. Consequently an HTTP status/encoding failure, timeout,
quiet cancellation, malformed/truncated PCM, I2S failure, or cleanup failure does
not retain the playback-busy flag, and a later request can be accepted.

## Automated evidence

Native audio coverage now checks fragmented and odd-length PCM, null/capacity
rejection without corrupting pending decoder state, buffered bytes after peer
close, known-length truncation, empty and malformed bodies, unknown-length
completion, quiet cancellation, idle and total timeouts, and a fresh successful
decoder after a failed attempt. The V1 firmware build compiles the production
HTTP/I2S path with the pinned board toolchain.

Commands run:

- `tests/native_audio/run.sh`
- `python3 -m pytest tests/test_native_audio.py -q`
- `uvx --from platformio platformio run -e waveshare_esp32_s3_touch_lcd_349_v1`
- `python3 -m pytest tests/ -q` (112 passed, 4 skipped, 20,807 subtests passed)
- `git diff --check`

The first full-suite attempt occurred before PlatformIO dependencies were
installed and reported the existing native-storage prerequisite. It passed after
the V1 build populated those headers.

## Limits

This cloud environment did not have the physical V1 board. No firmware was
flashed, and this work does not establish audibility, normal speech volume,
hardware-level I2S fault recovery, gesture calibration, battery accuracy, or
sustained board stability. Gateway deployment also remains outside this task.
