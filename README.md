# Hermes Familiar

A physical Hermes Agent familiar with animated artwork, touch controls, and a
native gateway plugin for agent state, messages, approvals, and device-started
actions. The current development and release target is the Waveshare
ESP32-S3-Touch-LCD-3.49 V1. The original 2.8-inch profile remains available as
a separate legacy build.

## Board profiles

| Board | PlatformIO environment | Display | Release guide |
| --- | --- | --- | --- |
| Waveshare ESP32-S3-Touch-LCD-3.49 V1 (current) | `waveshare_esp32_s3_touch_lcd_349_v1` | 640 × 172 landscape | [`docs/RELEASE_349_V1.md`](docs/RELEASE_349_V1.md) |
| Waveshare ESP32-S3-Touch-LCD-2.8 (legacy) | `waveshare_esp32_s3_touch_lcd_28` | 240 × 320 portrait | This repository's original 2.8 instructions below |

Build a named profile explicitly. Bare `pio run` builds all configured
environments, so select the desired board with `-e` when producing an image.

```sh
pio run -e waveshare_esp32_s3_touch_lcd_349_v1
pio run -e waveshare_esp32_s3_touch_lcd_28
```

The V1 release artifacts, SD card layout, checksums, build metadata, pin map,
serial setup, and recovery notes are in [`docs/RELEASE_349_V1.md`](docs/RELEASE_349_V1.md).
The latest bring-up evidence and accepted limitations are in
[`docs/HANDOFF_WAVESHARE_349_V1.md`](docs/HANDOFF_WAVESHARE_349_V1.md).

## Legacy 2.8 profile

The following original board, feature, build, and plugin notes describe the
Waveshare ESP32-S3-Touch-LCD-2.8 profile unless a section explicitly says V1.

## Verified hardware

- Board: Waveshare ESP32-S3-Touch-LCD-2.8
- USB serial: identify the connected board's port on your host before flashing.
- Display: ST7789, 240x320
- Touch: CST328 over I2C, verified with vendor `0xCACA` check
  - SDA GPIO1, SCL GPIO3, INT GPIO4, RST GPIO2, addr `0x1A`
- SD/TF card: SD_MMC 1-bit, verified with 128GB FAT32 card
  - CLK GPIO14, CMD GPIO17, D0 GPIO16, D3_EN GPIO21
- Power latch:
  - input GPIO6, hold GPIO7

## Current features

- SD-card `.raw4` portrait frame loading from `/hermes-buddy/frames/...`
- Fallback compiled frames if SD assets are missing
- Correct calibrated color path:
  - LCD inversion ON
  - RGB565 primitives normal
  - `pushImage()` buffers use `setSwapBytes(true)`
- Random idle blinking
- One-shot touch expressions:
  - wink hold -> neutral
  - smile/happy hold -> neutral
- Touch pages in the bottom terminal band:
  - Page 0: live familiar/Hermes status
  - Page 1: recent Hermes messages
  - Page 2: action controls
  - Page 3: device diagnostics
- Bidirectional USB JSON bridge:
  - device receives Hermes state/events
  - device sends touch/action/permission events
  - bridge can start/pause/resume/cancel one configured Hermes action job

## Build

```bash
pio run
```

Prepare a local release bundle for the 3.49 V1 board after building its image:

```bash
pio run -e waveshare_esp32_s3_touch_lcd_349_v1
python3 scripts/package_v1_release.py
```

The ignored `dist/waveshare-349-v1/` directory contains the V1-specific firmware
artifacts, a regenerated SD-card pack, source/toolchain metadata, and SHA-256
checksums. The pack is explicitly separate from the original 2.8-inch profile;
see [`docs/RELEASE_349_V1.md`](docs/RELEASE_349_V1.md) for its contents and the
board-specific upload notes.

## Flash

```bash
pio run -e waveshare_esp32_s3_touch_lcd_28 -t upload --upload-port <PORT>
```

Monitor boot:

```bash
pio device monitor -p <PORT> -b 115200
```

Expected legacy 2.8 boot lines:

```json
{"touch":"ok","driver":"cst328"}
{"hello":"hermes-buddy","transport":"serial+ble-nus"}
```

## Install as a Hermes plugin (recommended)

The familiar is a native third-party Hermes plugin, built entirely on the
official plugin surface (`plugin.yaml` manifest v1, `register(ctx)`,
`ctx.register_hook/tool/command`, trust-gated `ctx.llm`) — nothing patched,
nothing forked. Any Hermes user installs it through the standard channel:

```bash
hermes plugins install webdevtodayjason/hermes/plugin --enable
~/.hermes/hermes-agent/venv/bin/pip install pyserial   # the one python dep
hermes gateway restart
```

From this working tree (development):

```bash
./install.sh          # pyserial into the gateway venv + hermes plugins install/enable
hermes gateway restart
```

That's it. The plugin auto-detects the device on any `/dev/cu.usbmodem*`
(macOS) or `/dev/ttyACM*` (Linux) port, survives unplug/replug, and idles
quietly when no device is present. `/familiar` in any Hermes session shows the
link status.

What the device shows/does in plugin mode:

- **Live state** — thinking/tool-activity/idle from gateway hooks across ALL
  platforms (telegram, slack, cron, kanban workers), not just one surface.
- **Message ticker** — your messages (`u:`) and Hermes replies (`a:`) land on
  Page 1 as they happen; every reply also toasts a gold `> HERMES:` banner in
  the bottom band for 4s on whatever page you're on.
- **Message cards (V1 firmware)** — Page 1 shows two recent-message cards,
  each 55 px high with two preview lines. Tap a card to read its retained text; swipe vertically
  to browse one entry at a time across batches of five, then tap to return to
  the same card position. Text may be shortened to fit the device protocol.
- **Latest response viewer** — Page 0 keeps the latest assistant response in
  its Markdown viewer. The two-card history reader is separate from that view.
- **Current rollout** — the V1 message-reader/themes/audio firmware is flashed and verified.
  The matching gateway plugin is deployed; previews show two lines, and stable
  IDs/chunked detail loading support scrolling through retained message text.
  See the handoff for exact image identities and pending hardware checks.
- **Inline emoji and icons (3.49 V1 firmware)** — messages, notifications,
  labels, and Markdown display colour emoji, including flags, skin tones, and
  joined sequences, plus Meslo Nerd Font icons. Twemoji artwork by Twitter, Inc.
  and other contributors is CC BY 4.0; font attribution and license texts ship
  in the release `notices/`.
- **`familiar_notify` agent tool** — Hermes itself can ping the desk: banner
  + chirp. "Ping my desk when the build finishes" now works, and cron jobs
  can reach the device the same way.
- **Real approvals** — when the gateway blocks on a dangerous-command
  approval, the device jumps to ALLOW/DENY with an alert chirp, pulses
  red/amber, and re-chirps every 60s until answered; a tap calls the same
  resolver as `/approve` / `/deny`.
- **Cron page (4)** — next runs + last result for active scheduled jobs.
- **Gateway page (5)** — uptime, platform health (telegram/slack), sessions,
  tools, and tokens today. Both pages refresh every minute, host-formatted.
- **Actions** — Page 2 START runs the first enabled action from
  `~/.hermes/familiar_actions.json` as a `hermes chat -q` subprocess;
  PAUSE/CANCEL signal it.
- **Wi-Fi provisioning over USB** — no SD-card shuffling:
  `{"type":"config","wifi":{"ssid":"...","password":"..."}}` on the serial
  port writes the SD config and connects live.

See [`docs/HERMES_INTEGRATION.md`](docs/HERMES_INTEGRATION.md) for the full
contract.

### V1 themes and device settings

The 3.49 V1 firmware has an eighth tab, **SET**, with Phosphor, Amber, Ocean,
Paper, and Gruvbox themes. Tap a theme tile to apply it. The same tab controls sound
mute, face animation, and brightness at 25%, 50%, 75%, or 100%. Settings are
saved in the `ui` section of the existing `/hermes-buddy-349-v1/config.json`
without replacing network settings or artwork metadata. Without a writable
SD card, changes apply for the current session and the device reports that
they could not be saved. Manual mute remains active after turning the device
face-up; face-down quiet mode also continues to suppress playback. The avatar
uses the selected palette; Phosphor preserves its original colors.

FACE and MSGS card previews use the same styled Markdown renderer as the full
message reader. Supported headings, emphasis, and code retain their styling;
code uses the theme's code accent and panel background. Preview text remains
limited by the host's supplied message excerpt.

### Voice and TTS providers

To trigger a spoken test in a Hermes chat, use `/familiar say Hello from Hermes`.
Keep the board face-up with SET → SOUND ON. `/familiar ping Test` sends a banner
and chirp; `/familiar` reports link status. These are gateway chat commands.

Voice is enabled by default and uses the Hermes gateway's configured TTS
provider. Configure the optional renderer in
`~/.hermes/familiar_actions.json` under `voice`:

```json
{
  "voice": {
    "enabled": true,
    "provider": "vertex-gemini",
    "model": "gemini-2.5-flash-tts",
    "voice": "Kore"
  }
}
```

Supported providers are `hermes` (default), `vertex-gemini` and `yandex`.
Gemini also accepts `gemini` as an alias; Yandex accepts `yandex-speechkit`.
For Vertex Express Mode Gemini TTS, set `G_API_KEY` in the environment used by
the Hermes gateway. For Yandex SpeechKit, set `YANDEX_AI_API_KEY`; its default
voice and language are `filipp` and `ru-RU`. Yandex configuration can specify
`"voice": "filipp"` and `"language": "ru-RU"`. Do not put API keys in the
JSON config. Provider API access and device playback require network access
from the gateway host and a reachable advertised host address for the device.
For WSL/NAT deployments, set `HERMES_ADVERTISED_HOST` in the Hermes gateway
process environment to an IPv4 address reachable from the ESP32; route-derived
WSL addresses may be private to WSL. Configure routing and firewall for the
actual host network, keeping HTTP audio (8765) and device TCP (8767) limited
to a trusted LAN. A host-local HTTP request is not proof of board-to-host
reachability. The V1 supports playback; microphone capture and speech-to-text
input are not implemented.

### Network transports (TCP + WebSocket)

Besides USB, the plugin listens on two network legs speaking the identical
newline-JSON protocol — configured in the `transport` block of
`~/.hermes/familiar_actions.json`:

```json
"transport": {"enabled": true, "port": 8767, "ws_port": 8768, "token": "<secret>"}
```

- **TCP :8767** — the device dials home when its USB host goes silent
  (untethered mode).
- **WebSocket :8768** — phones / browsers (the Pocket Familiar iOS app);
  every frame is broadcast to all connected clients, even while USB is up.
- **Token auth** — with `token` set, a client's **first** line/message must be
  `{"type":"auth","token":"…"}`; anything else drops the connection (server
  replies `{"type":"auth","ok":true}` on success). Mandatory in practice: a
  network client can approve dangerous commands. No `token` in the config
  leaves both legs open (logged loudly) — old-firmware back-compat only.

## Standalone bridge (dev fallback)

For development without a gateway (or on a machine without Hermes), the
original polling bridge still works:

```bash
python3 scripts/hermes_serial_bridge.py --port <PORT> --interval 1
```

It reads `~/.hermes/state.db` read-only and shares the same
`~/.hermes/familiar_actions.json`. With `--api-url` + `--api-key` it can also
drive `/v1/runs`, SSE events, and `/api/jobs/*` on a remote Hermes API server
— see the integration doc.

Default action slot:

```json
{
  "id": "status_brief",
  "label": "Status brief",
  "enabled": true,
  "type": "run",
  "prompt": "Give me a concise current Hermes work/status brief. Include active tasks, blockers, and next best action. Keep it under 120 words.",
  "command": [
    "hermes",
    "chat",
    "-q",
    "Give me a concise current Hermes work/status brief. Include active tasks, blockers, and next best action. Keep it under 120 words."
  ]
}
```

Action config semantics:

- `type: "run"` / `"api_run"`: in API mode, starts `/v1/runs` using `prompt`; otherwise falls back to `command`.
- `type: "cron_job"` / `"api_job"`: uses `job_id` with `/api/jobs/{job_id}/run`, and Page 2 pause/resume toggles `/pause` and `/resume`.
- `type: "command"`: always uses the local subprocess `command` fallback.
- Legacy `command: ["hermes", "chat", "-q", "..."]` entries are treated as `run` in API mode unless you explicitly set `type: "command"`.

Example cron-backed action:

```json
{
  "id": "nightly_review",
  "label": "Nightly review",
  "enabled": true,
  "type": "cron_job",
  "job_id": "aabbccddeeff"
}
```

Disable or edit that config if you do not want the device to be able to spawn a Hermes run/job.

## Touch controls

### Portrait area

Tap the portrait area for local personality reaction.

### Bottom terminal band and swipes

There are no required side buttons. Use the touchscreen:

- Swipe left anywhere on the screen: next page.
- Swipe right anywhere on the screen: previous page.
- Tap the bottom band on non-action pages: next page.
- Tap the portrait area: local personality reaction, or jump to approval controls if Hermes is waiting.

Page 0: status

```text
FAMILIAR:AWAKE/THINK/WAIT
sessions/running/waiting/tokens
latest message or touch
```

Page 1: recent messages

Shows the latest user and assistant messages as two cards. Tap either card to
open its retained text. Swipe vertically to move one message at a time through
the five-entry history batches; tap to return to Page 1 at the same position.
Older host records that only have a compact preview may not have full text.

Page 2: actions

```text
START    PAUSE    CANCEL
```

Touch zones on the bottom action band:

- x < 80: start first enabled action from `~/.hermes/familiar_actions.json`
- 80 <= x < 160: pause/resume running action
- x >= 160: cancel running action

When Hermes is waiting for approval, Page 2 becomes:

```text
TAP: ALLOW        DENY
```

- x < 120: allow once
- x >= 120: deny

Swipe left/right to leave the action page.

Page 3: device diagnostics

Shows battery voltage, RTC status/time if present, Wi-Fi/IP, IMU availability, and I2S audio availability. If `/hermes-buddy/config.json` on the SD card contains Wi-Fi credentials, the device also exposes:

```text
GET /status
GET /action?name=start|pause|cancel
GET /chirp
```

Example SD Wi-Fi config:

```json
{"wifi":{"ssid":"YourNetwork","password":"YourPassword"}}
```

## Bridge protocol examples

Host to device:

```json
{"type":"state","running":1,"waiting":0,"msg":"...","entries":["12:04 u: …"],"entry_ids":["7c9a4fd33e6744c9a1b2c3d4e5f60718-1"],"job_state":"running","job_label":"Status brief"}
{"type":"msgs","off":0,"total":9,"lines":["12:04 u: …"],"ids":["7c9a4fd33e6744c9a1b2c3d4e5f60718-1"]}
{"type":"msg","id":"7c9a4fd33e6744c9a1b2c3d4e5f60718-1","body":"Full retained message text","role":"user","truncated":false}
{"type":"event","event":"message","role":"assistant","msg":"..."}
{"type":"ack","msg":"started: Status brief"}
```

`entry_ids` and `msgs.ids` are optional parallel arrays for the existing
`entries` and `lines` fields. Firmware can request a selected record with
`{"cmd":"msg","id":"…"}`. The host retains at most 40 detail records and
caps each stored body at 16 KiB. Detail requests optionally include a UTF-8
byte `offset`; replies carry `body_offset`, `body_total`, `has_more`, and
`retained_truncated`. The V1 reader fetches successive chunks to display all
retained text. Each chunk is capped at 3200 UTF-8 bytes and a 4095-byte escaped
JSON frame. `truncated` also marks remaining chunks for older clients; the
retention flag marks original bodies longer than 16 KiB.
An evicted or unknown ID returns an empty body with `error:"stale"`. A
preview-only record returns `error:"unavailable"` rather than presenting its
compact preview as full text.

Device to host:

```json
{"cmd":"touch","x":58,"y":121}
{"cmd":"swipe","dx":-72,"dy":4}
{"cmd":"action","action":"start"}
{"cmd":"action","action":"pause"}
{"cmd":"action","action":"cancel"}
{"cmd":"msgs","off":0}
{"cmd":"msg","id":"7c9a4fd33e6744c9a1b2c3d4e5f60718-1"}
{"cmd":"permission","decision":"once"}
{"event":"local_auto","mood":"blink"}
```

## SD asset pipeline

The V1 release exporter uses the checked-in `assets/sd_preview/` artwork by
default, so its 349 V1 pack can be regenerated from this repository without an
author's download directory.

For the legacy 2.8-inch profile, the original source art folder is supplied by
the operator and is not part of this repository. Pass its path explicitly to
the exporter; no author-specific path is assumed:

```text
<source artwork directory>
```

Export SD pack:

```bash
python3 scripts/export_sd_pack.py \
  --src "<source artwork directory>" \
  --out sdcard/hermes-buddy \
  --preview assets/sd_preview
```

Copy `sdcard/hermes-buddy` to the root of the FAT32 SD card.

The V1 firmware instead reads `sdcard/hermes-buddy-349-v1/` from the card
root. Its profile, SD paths, and verified frame hashes are documented in the
V1 release guide; do not put the legacy and V1 asset packs under an ambiguous
shared name.

## Waveshare 3.49 V1 setup

The V1 serial device normally appears as `/dev/ttyACM*` on Linux/WSL or
`/dev/cu.usbmodem*` on macOS. In WSL, attach the ESP32 USB device to the WSL
distribution before opening the serial bridge. After an upload or USB reset,
check that it is still attached and reopen the port. The Hermes gateway owns
the serial connection; stop other serial monitors while using plugin mode.
Build/upload commands, board-specific pin mapping, and recovery guidance are
in the V1 release guide.

For Wi-Fi audio or TCP dial-home from a board to Hermes running under WSL,
configure the board with the gateway host's reachable IPv4 address and set
`HERMES_ADVERTISED_HOST` in the gateway's environment to the IPv4 address the
board can reach. WSL's route-derived address may be private to WSL, so verify
the address from the board's network and allow the documented LAN ports
(HTTP audio 8765 and device TCP 8767) through the host firewall. USB-local
speech conversion does not prove this board-to-host route works.

The V1 landscape UI has eight tabs:

| Tab | Contents |
| --- | --- |
| FACE | Animated avatar, familiar state, host status, latest assistant response with supported Markdown |
| MSGS | Two recent message cards with two preview lines; open a retained message and scroll through bounded chunks, or browse five-entry history batches |
| OPS | Six configurable actions and live approval ALLOW/DENY controls |
| FLEET | Gateway worker/agent status |
| CRON | Scheduled jobs and recent results |
| NET | Gateway, transport, and network status |
| DEV | Device diagnostics for SD, touch, battery estimate, RTC/IMU, audio, Wi-Fi, and active links |
| SET | Phosphor, Amber, Ocean, Paper, or Gruvbox theme; sound mute; animation; 25/50/75/100% brightness |

SET preferences are saved under `ui` in
`/hermes-buddy-349-v1/config.json`; without writable SD storage they are
session-only. The current image and complete recorded limitations are listed
in the handoff. In particular, the three older restarts on a superseded image
remain unexplained, battery ADC accuracy is not calibrated, and the full
two-hour workload, overnight idle, and power/failure matrix were accepted as
unperformed at migration closure. User signoff does not turn those checks into
recorded hardware evidence.

## Native extension status

The familiar ships as a first-class Hermes plugin (`plugin/`, installed name
`familiar`): in-gateway hooks drive the device in real time and device taps
resolve real gateway approvals — no API server or polling required. The
standalone bridge remains for gateway-less development and remote API mode.

Historical 2.8-inch board bring-up notes (not the current V1 status):

- Touch: verified OK.
- SD: verified OK.
- Audio/I2S chirps: verified firmware init OK.
- Wi-Fi: implemented; waiting for SD `config.json` credentials.
- Battery ADC: implemented and shown on Page 3.
- RTC/IMU: firmware probes vendor I2C pins GPIO11/GPIO10 and reports `off` on this unit/boot; code path remains present for boards where those parts respond.
