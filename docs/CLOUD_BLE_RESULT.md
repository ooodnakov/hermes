# Cloud BLE transport result

## Implemented

- Added the standard Nordic UART Service UUIDs to the V1 familiar firmware.
- Pinned NimBLE-Arduino 2.5.1 for the V1 build. The legacy 2.8 environment's
  existing 2.5.1 pin is unchanged.
- Required bonded LE Secure Connections with authenticated, encrypted,
  128-bit-key GATT access. A random six-digit passkey is generated on every
  boot and emitted only on the attached USB console; it is not stored in the
  repository or accepted over BLE.
- BLE callback code only copies bounded data and connection events. JSON
  framing and protocol dispatch run later on the application loop.
- Input is bounded to an 8 KiB queue and 4,095-byte lines. Oversized, embedded
  NUL, and input-queue-overflow frames are discarded through a newline before
  parsing resumes. Output is an 8 KiB queue and notifications are split to the
  negotiated ATT MTU.
- Disconnect resets partial input and pending output, and advertising restarts
  automatically. Only an encrypted, authenticated connection can enqueue
  protocol input, so an unauthenticated client cannot reach approval handling.
- Diagnostics now report readiness, connection/authentication/subscription,
  MTU, connects/disconnects, authentication failures, rejected writes,
  framing/notification errors, queue drops, and queue high-water marks.

The native transport test covers fragmented and multiple frames, CRLF and
empty lines, NUL rejection and recovery, line overflow and recovery, callback
queue overflow and recovery, bounded output, chunk consumption, and session
reset. Firmware builds provide compile validation only; this cloud environment
cannot exercise a radio or physical board.

Validation in the task checkout passed the complete host suite (116 tests, one
skipped, and 20,807 subtests), the V1 familiar firmware build (94,876 bytes RAM
and 5,118,735 bytes flash), and the legacy 2.8 firmware build (55,612 bytes RAM
and 1,507,925 bytes flash). The V1 build retained the pre-existing nonfatal
`esp_idf_size --ng` warning. Legacy ArduinoJson and NimBLE service-start
deprecation warnings are likewise pre-existing and were not changed in this
V1-only task.

## Documentation basis

Context7 was requested but is not available in this environment. Fresh upstream
NimBLE-Arduino 2.5.1 API documentation was consulted instead, including the
security-authentication settings, encrypted/authenticated connection state,
server callback signatures, per-peer notification API, and automatic
advertising after disconnect.

## Physical checks still pending

Do not treat the firmware build or native queue test as evidence for any of
these checks:

1. Pair from a fresh phone/computer using the six-digit value printed over USB;
   verify a wrong passkey and an unpaired write cannot submit a command.
2. Subscribe to NUS TX only after authenticated encryption, send fragmented,
   coalesced, NUL-containing, and oversized RX frames, and verify recovery with
   a subsequent ping.
3. Negotiate the default MTU and at least one larger MTU; verify long messages
   reassemble byte-for-byte and diagnostics show the actual negotiated MTU.
4. Disconnect during a partial input frame and with queued output, reconnect,
   reauthenticate, and verify neither stale data nor an approval survives as a
   newly actionable BLE command.
5. Repeat connection loss, host/application restart, bond reuse, bond removal,
   and board reboot. Confirm advertising recovery and new per-boot passkey
   behavior.
6. Exercise harmless protocol traffic and explicit ALLOW/DENY test approvals,
   including stale IDs and approval resolution on another transport.
7. Run concurrent USB, TCP, and BLE traffic to confirm existing USB/TCP
   behavior and record queue high-water/drop/notification counters under load.
8. Perform sustained physical-board stability testing. No claim about
   audibility, gesture calibration, battery accuracy, or sustained board
   stability is made here.
